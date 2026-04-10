#pragma once

#include "boilerplate.h"

#ifndef TRACK_STRIDE_SIZES
#define TRACK_STRIDE_SIZES 0
#endif

#define RING_SIZE 16384 /* Must be a power of 2 */
#define POOL_CACHE_SIZE 256

#define ORCHESTRATOR_MAX_FLOWS 32
#define ORCHESTRATOR_TELEMETRY_PHASE_PACKETS 10000000ll
#define ORCHESTRATOR_CMS_HEIGHT 9
#define ORCHESTRATOR_CMS_WIDTH 524288

// We assume a single elephant core, and thus a single elephant queue.
// Queue 0 is arbitrarily chosen for the elephant queue.
// DO NOT CHANGE THIS!
#define ELEPHANT_QUEUE_ID 0

struct rte_ring *flow_feedback_ring;
struct rte_mempool *flow_id_pool;
volatile int orchestrator_done = 0;

struct lcore_conf {
  uint16_t queue_id;
} lcores_conf[RTE_MAX_LCORE];

struct flow_t {
  uint32_t src_ip;
  uint32_t dst_ip;
  uint16_t src_port;
  uint16_t dst_port;
};

struct pkt_tag_t {
  uint8_t processed;
  struct flow_t id;
};

struct flow_t flow_from_pkt(uint8_t *pkt, uint32_t pkt_len) {
  struct flow_t id = {0};

  if (pkt_len < sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr) + sizeof(struct tcpudp_hdr)) {
    return id;
  }

  struct rte_ether_hdr *ether_hdr = (struct rte_ether_hdr *)pkt;

  if (ether_hdr->ether_type != rte_be_to_cpu_16(RTE_ETHER_TYPE_IPV4)) {
    return id;
  }

  struct rte_ipv4_hdr *ipv4_hdr = (struct rte_ipv4_hdr *)(ether_hdr + 1);

  if (ipv4_hdr->next_proto_id != IPPROTO_TCP && ipv4_hdr->next_proto_id != IPPROTO_UDP) {
    return id;
  }

  struct tcpudp_hdr *tcpudp_hdr = (struct tcpudp_hdr *)(ipv4_hdr + 1);

  id.src_ip   = ipv4_hdr->src_addr;
  id.dst_ip   = ipv4_hdr->dst_addr;
  id.src_port = tcpudp_hdr->src_port;
  id.dst_port = tcpudp_hdr->dst_port;

  return id;
}

void generate_elephant_rule(struct flow_t *id, uint16_t rx_q) {
  struct rte_flow_attr attr = {.ingress = 1};
  struct rte_flow_item pattern[5];
  struct rte_flow_action action[2];
  struct rte_flow_error error;

  // Zero-out to prevent garbage values in pointers
  memset(pattern, 0, sizeof(pattern));
  memset(action, 0, sizeof(action));

  // 1. ETH: Must be present for many drivers, even if empty
  pattern[0].type = RTE_FLOW_ITEM_TYPE_ETH;

  // 2. IPv4: Provide both SPEC and MASK
  struct rte_flow_item_ipv4 ip_spec = {
      .hdr = {.src_addr = id->src_ip, .dst_addr = id->dst_ip, .next_proto_id = IPPROTO_UDP}};
  struct rte_flow_item_ipv4 ip_mask = {
      .hdr = {.src_addr = RTE_BE32(0xFFFFFFFF), .dst_addr = RTE_BE32(0xFFFFFFFF), .next_proto_id = 0xFF}};

  pattern[1].type = RTE_FLOW_ITEM_TYPE_IPV4;
  pattern[1].spec = &ip_spec;
  pattern[1].mask = &ip_mask;

  // 3. UDP: Match on src/dst ports
  struct rte_flow_item_udp udp_spec = {.hdr = {.src_port = id->src_port, .dst_port = id->dst_port}};
  struct rte_flow_item_udp udp_mask = {.hdr = {.src_port = RTE_BE16(0xFFFF), .dst_port = RTE_BE16(0xFFFF)}};

  pattern[2].type = RTE_FLOW_ITEM_TYPE_UDP;
  pattern[2].spec = &udp_spec;
  pattern[2].mask = &udp_mask;

  // 4. END: Terminate pattern list
  pattern[3].type = RTE_FLOW_ITEM_TYPE_END;

  // 4. ACTION: Queue
  struct rte_flow_action_queue queue = {.index = rx_q};
  action[0].type                     = RTE_FLOW_ACTION_TYPE_QUEUE;
  action[0].conf                     = &queue;
  action[1].type                     = RTE_FLOW_ACTION_TYPE_END;

  for (uint16_t dev = 0; dev < rte_eth_dev_count_avail(); dev++) {
    struct rte_flow *flow = rte_flow_create(dev, &attr, pattern, action, &error);
    if (!flow) {
      // Some flows (e.g. UDP port 4500 / IPsec NAT-T) trigger special NIC
      // firmware profiles that fail to create. Skip rather than crash.
      NF_INFO("Warning: failed to create FDIR rule (type %d: %s), skipping.", error.type,
              error.message ? error.message : "(no message)");
      return;
    }
  }
}

void orchestrator_loop() {
  NF_INFO("Orchestrator core %u started.", rte_lcore_id());

  unsigned nb_devices = rte_eth_dev_count_avail();

  struct elephant_t {
    struct flow_t id;
    uint64_t weight;
  };

  struct CMS *flow_counter = NULL;
  uint64_t total_packets   = 0;

  struct elephant_t elephants[ORCHESTRATOR_MAX_FLOWS];
  memset(elephants, 0, sizeof(elephants));

  if (cms_allocate(ORCHESTRATOR_CMS_HEIGHT, ORCHESTRATOR_CMS_WIDTH, sizeof(struct flow_t), 0, &flow_counter) == 0) {
    rte_exit(EXIT_FAILURE, "Failed to allocate orchestrator Count-Min Sketch\n");
  }

  void *dequeued_ptrs[BATCH_SIZE];
  int progress = 0;
  while (total_packets < ORCHESTRATOR_TELEMETRY_PHASE_PACKETS) {
    unsigned int n = rte_ring_dequeue_burst(flow_feedback_ring, dequeued_ptrs, BATCH_SIZE, NULL);

    if (n == 0) {
      continue;
    }

    for (unsigned int i = 0; i < n; i++) {
      struct flow_t *id = (struct flow_t *)dequeued_ptrs[i];
      cms_increment(flow_counter, id);
      uint64_t current_count = cms_count_min(flow_counter, id);

      struct elephant_t *smallest_elephant = &elephants[0];
      bool already_tracked                 = false;
      for (int i = 0; i < ORCHESTRATOR_MAX_FLOWS; i++) {
        if (memcmp(&elephants[i].id, id, sizeof(struct flow_t)) == 0) {
          elephants[i].weight = current_count;
          already_tracked     = true;
          break;
        }
        if (elephants[i].weight < smallest_elephant->weight) {
          smallest_elephant = &elephants[i];
        }
      }

      if (!already_tracked && current_count > smallest_elephant->weight) {
        smallest_elephant->id     = *id;
        smallest_elephant->weight = current_count;
      }
    }

    rte_mempool_put_bulk(flow_id_pool, dequeued_ptrs, n);

    int current_progress = (total_packets * 100) / ORCHESTRATOR_TELEMETRY_PHASE_PACKETS;
    if (current_progress >= progress + 10) {
      NF_INFO("Processed %d%% of packets...", current_progress);
      progress = current_progress;
    }

    total_packets += n;
  }

  NF_INFO("Orchestrator finished processing 10 million packets.");

  for (int i = 0; i < ORCHESTRATOR_MAX_FLOWS; i++) {
    if (elephants[i].weight == 0) {
      continue;
    }
    NF_INFO("Installing elephant rule: src_ip=%" PRIu32 ", dst_ip=%" PRIu32 ", src_port=%" PRIu16 ", dst_port=%" PRIu16
            ", weight=%" PRIu64 " (%5.2f%%).",
            elephants[i].id.src_ip, elephants[i].id.dst_ip, elephants[i].id.src_port, elephants[i].id.dst_port,
            elephants[i].weight, (float)elephants[i].weight / total_packets * 100);
    generate_elephant_rule(&elephants[i].id, ELEPHANT_QUEUE_ID);
  }

  uint64_t total_elephant_packets = 0;
  for (int i = 0; i < ORCHESTRATOR_MAX_FLOWS; i++) {
    total_elephant_packets += elephants[i].weight;
  }
  NF_INFO("Total elephant packets: %" PRIu64 " (%5.2f%% of total traffic).", total_elephant_packets,
          (float)total_elephant_packets / total_packets * 100);

  orchestrator_done = 1;

  while (1) {
    rte_pause();
  }
}

void mice_worker_loop() {
  if (!nf_init()) {
    rte_exit(EXIT_FAILURE, "Error initializing NF");
  }

  unsigned lcore_id = rte_lcore_id();
  uint16_t queue_id = lcores_conf[lcore_id].queue_id;

  NF_INFO("Mice worker core %u started, listening on queue %u.", lcore_id, queue_id);

  struct tx_mbuf_batch {
    struct rte_mbuf *batch[BATCH_SIZE];
    uint16_t tx_count;
  };

  uint16_t devices_count                  = rte_eth_dev_count_avail();
  struct tx_mbuf_batch *tx_batch_per_port = (struct tx_mbuf_batch *)calloc(devices_count, sizeof(struct tx_mbuf_batch));

  while (1) {
    for (uint16_t device = 0; device < devices_count; device++) {
      struct rte_mbuf *mbufs[BATCH_SIZE];

      uint16_t rx_count = rte_eth_rx_burst(device, queue_id, mbufs, BATCH_SIZE);

      for (uint16_t n = 0; n < rx_count; n++) {
        uint16_t src_device = mbufs[n]->port;
        uint8_t *pkt        = rte_pktmbuf_mtod(mbufs[n], uint8_t *);
        uint32_t pkt_len    = mbufs[n]->pkt_len;
        struct flow_t id    = flow_from_pkt(pkt, pkt_len);

        uint16_t dst_device = DROP;

        if (!orchestrator_done) {
          struct flow_t *id_entry;
          if (rte_mempool_get(flow_id_pool, (void **)&id_entry) == 0) {
            *id_entry = id;
            if (rte_ring_enqueue(flow_feedback_ring, id_entry) < 0) {
              rte_mempool_put(flow_id_pool, id_entry);
            }
          }
        } else {
          time_ns_t now = current_time();
          dst_device    = nf_process(src_device, pkt, pkt_len, now);
        }

        if (dst_device == DROP) {
          rte_pktmbuf_free(mbufs[n]);
        } else {
          uint16_t tx_count                             = tx_batch_per_port[dst_device].tx_count;
          tx_batch_per_port[dst_device].batch[tx_count] = mbufs[n];
          tx_batch_per_port[dst_device].tx_count++;
        }
      }

      for (uint16_t dst_device = 0; dst_device < devices_count; dst_device++) {
        uint16_t sent_count = rte_eth_tx_burst(dst_device, queue_id, tx_batch_per_port[dst_device].batch,
                                               tx_batch_per_port[dst_device].tx_count);
        for (uint16_t n = sent_count; n < tx_batch_per_port[dst_device].tx_count; n++) {
          rte_pktmbuf_free(tx_batch_per_port[dst_device].batch[n]);
        }
        tx_batch_per_port[dst_device].tx_count = 0;
      }
    }
  }
}

#if TRACK_STRIDE_SIZES
uint64_t stride_sizes[BATCH_SIZE];
void handle_sigint(int sig) {
  printf("\nCaught signal %d (SIGINT). Cleaning up...\n", sig);

  for (size_t i = 0; i < BATCH_SIZE; i++) {
    printf("Stride size %zu: %lu packets\n", i + 1, stride_sizes[i]);
  }

  _exit(0);
}
#endif

void elephant_worker_loop() {
  if (!nf_init()) {
    rte_exit(EXIT_FAILURE, "Error initializing NF");
  }

  unsigned lcore_id = rte_lcore_id();
  uint16_t queue_id = lcores_conf[lcore_id].queue_id;

  NF_INFO("Elephant worker core %u started, listening on queue %u.", lcore_id, queue_id);

  struct tx_mbuf_batch {
    struct rte_mbuf *batch[BATCH_SIZE];
    uint16_t tx_count;
  };

  uint16_t devices_count                  = rte_eth_dev_count_avail();
  struct tx_mbuf_batch *tx_batch_per_port = (struct tx_mbuf_batch *)calloc(devices_count, sizeof(struct tx_mbuf_batch));

  while (1) {
    for (uint16_t device = 0; device < devices_count; device++) {
      struct rte_mbuf *mbufs[BATCH_SIZE];
      uint16_t rx_count = rte_eth_rx_burst(device, queue_id, mbufs, BATCH_SIZE);

      if (rx_count == 0) {
        continue;
      }

      uint8_t *pkts[BATCH_SIZE];
      uint32_t pkt_lens[BATCH_SIZE];
      struct pkt_tag_t tagged_pkts[BATCH_SIZE];

      for (uint16_t n = 0; n < rx_count; n++) {
        pkts[n]                  = rte_pktmbuf_mtod(mbufs[n], uint8_t *);
        pkt_lens[n]              = mbufs[n]->pkt_len;
        tagged_pkts[n].processed = 0;
        tagged_pkts[n].id        = flow_from_pkt(pkts[n], pkt_lens[n]);
      }

      for (uint16_t n = 0; n < rx_count; n++) {
        if (tagged_pkts[n].processed) {
          continue;
        }

        time_ns_t now       = current_time();
        uint16_t dst_device = nf_process(device, pkts[n], pkt_lens[n], now);

        if (dst_device == DROP) {
          rte_pktmbuf_free(mbufs[n]);
        } else {
          uint16_t tx_count                             = tx_batch_per_port[dst_device].tx_count;
          tx_batch_per_port[dst_device].batch[tx_count] = mbufs[n];
          tx_batch_per_port[dst_device].tx_count++;
        }

        uint8_t logical_batch_size = 0;
        for (uint16_t i = n + 1; i < rx_count; i++) {
          if (memcmp(&tagged_pkts[n].id, &tagged_pkts[i].id, sizeof(struct flow_t)) == 0) {
            tagged_pkts[i].processed = 1;
            logical_batch_size++;

            if (dst_device == DROP) {
              rte_pktmbuf_free(mbufs[i]);
            } else {
              uint16_t tx_count                             = tx_batch_per_port[dst_device].tx_count;
              tx_batch_per_port[dst_device].batch[tx_count] = mbufs[i];
              tx_batch_per_port[dst_device].tx_count++;
            }
          }
        }
      }
    }
  }
}

int nf_setup(int argc, char **argv) {
  // Initialize the DPDK Environment Abstraction Layer (EAL)
  int ret = rte_eal_init(argc, argv);
  if (ret < 0) {
    rte_exit(EXIT_FAILURE, "Error with EAL initialization, ret=%d\n", ret);
  }
  argc -= ret;
  argv += ret;

  unsigned nb_devices = rte_eth_dev_count_avail();

  // application private area size
  uint16_t priv_size      = 0;
  uint16_t data_room_size = RTE_MBUF_DEFAULT_BUF_SIZE;

  if (rte_lcore_count() < 3) {
    rte_exit(
        EXIT_FAILURE,
        "At least 3 cores are required (1 for the orchestrator, 1 for the elephant core, the others for workers)\n");
  }

  uint16_t nb_workers           = rte_lcore_count() - 1;
  uint16_t generic_queues_start = 1;
  uint16_t nb_generic_workers   = nb_workers - 1;

  struct rte_mempool *mbuf_pool = rte_pktmbuf_pool_create("MEMPOOL", MEMPOOL_BUFFER_COUNT * nb_devices, MBUF_CACHE_SIZE,
                                                          priv_size, data_room_size, rte_socket_id());
  if (mbuf_pool == NULL) {
    rte_exit(EXIT_FAILURE, "Cannot create pool: %s\n", rte_strerror(rte_errno));
  }

  // Initialize all devices
  for (uint16_t device = 0; device < nb_devices; device++) {
    int retval;

    struct rte_eth_conf device_conf = {
        .rxmode = {.mq_mode = RTE_ETH_MQ_RX_RSS},
        .rx_adv_conf =
            {
                .rss_conf =
                    {
                        .rss_key = NULL,
                        .rss_hf  = RTE_ETH_RSS_IPV4 | RTE_ETH_RSS_NONFRAG_IPV4_TCP | RTE_ETH_RSS_NONFRAG_IPV4_UDP,
                    },
            },
    };

    retval = rte_eth_dev_configure(device, nb_workers, nb_workers, &device_conf);
    if (retval != 0) {
      rte_exit(EXIT_FAILURE, "Cannot init device %" PRIu16 ": %d\n", device, retval);
    }

    uint16_t queue = 0;
    uint16_t worker_id;
    RTE_LCORE_FOREACH_WORKER(worker_id) {
      NF_INFO("Assigning device %" PRIu16 " queue %" PRIu16 " to worker %u.", device, queue, worker_id);

      // Allocate and set up a TX queue (NULL == default config)
      retval = rte_eth_tx_queue_setup(device, queue, TX_QUEUE_SIZE, rte_eth_dev_socket_id(device), NULL);
      if (retval != 0) {
        rte_exit(EXIT_FAILURE, "Cannot setup TX queue for device %" PRIu16 ": %d\n", device, retval);
      }

      // Allocate and set up RX queues (NULL == default config)
      retval = rte_eth_rx_queue_setup(device, queue, RX_QUEUE_SIZE, rte_eth_dev_socket_id(device), NULL, mbuf_pool);
      if (retval != 0) {
        rte_exit(EXIT_FAILURE, "Cannot setup RX queue for device %" PRIu16 ": %d\n", device, retval);
      }

      lcores_conf[worker_id].queue_id = queue;

      queue++;
    }

    struct rte_eth_rss_reta_entry64 reta_conf[RTE_ETH_RSS_RETA_SIZE_512 / RTE_ETH_RETA_GROUP_SIZE];

    for (int i = 0; i < 512; i++) {
      uint16_t idx   = i / RTE_ETH_RETA_GROUP_SIZE;
      uint16_t shift = i % RTE_ETH_RETA_GROUP_SIZE;
      // Map RSS traffic only to generic worker queues
      uint16_t q_dest = (ELEPHANT_QUEUE_ID + 1) + (i % nb_generic_workers);
      reta_conf[idx].mask |= (1ULL << shift);
      reta_conf[idx].reta[shift] = q_dest;
    }
    rte_eth_dev_rss_reta_update(device, reta_conf, 512);

    retval = rte_eth_dev_start(device);
    if (retval != 0) {
      rte_exit(EXIT_FAILURE, "Cannot start device %" PRIu16 ": %d\n", device, retval);
    }

    rte_eth_promiscuous_enable(device);
    if (rte_eth_promiscuous_get(device) != 1) {
      rte_exit(EXIT_FAILURE, "Cannot enable promiscuous mode for device %" PRIu16 ": %d\n", device, retval);
    }

    NF_INFO("Initialized device %" PRIu16 ".", device);
  }

  flow_id_pool = rte_mempool_create("FLOW_ID_POOL",
                                    RING_SIZE * 2,         // Total elements
                                    sizeof(struct flow_t), // Size of each element
                                    POOL_CACHE_SIZE,       // Per-lcore local cache
                                    0, NULL, NULL, NULL, NULL, rte_socket_id(), 0);

  flow_feedback_ring = rte_ring_create("FLOW_FEEDBACK_RING", RING_SIZE, rte_socket_id(),
                                       RING_F_SC_DEQ); // Multi-Producer, Single-Consumer

#if TRACK_STRIDE_SIZES
  memset(stride_sizes, 0, sizeof(stride_sizes));
  signal(SIGINT, handle_sigint);
#endif

  return 0;
}

static inline void worker_loop() {
  uint16_t nb_workers           = rte_lcore_count() - 1;
  uint16_t generic_queues_start = 1;

  uint16_t worker_id;
  uint16_t worker_idx = 0;
  RTE_LCORE_FOREACH_WORKER(worker_id) {
    if (worker_idx == 0) {
      // This is our Elephant core
      lcores_conf[worker_id].queue_id = ELEPHANT_QUEUE_ID;
      rte_eal_remote_launch((lcore_function_t *)elephant_worker_loop, NULL, worker_id);
    } else {
      // Generic workers. They start at queue 1, since queue 0 is reserved for the elephant core.
      lcores_conf[worker_id].queue_id = 1 + (worker_idx - 1);
      rte_eal_remote_launch((lcore_function_t *)mice_worker_loop, NULL, worker_id);
    }
    worker_idx++;
  }

  orchestrator_loop();
}