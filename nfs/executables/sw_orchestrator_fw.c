#include "../lib/libvig.h"

#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>

#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_malloc.h>
#include <rte_mbuf.h>
#include <rte_random.h>
#include <rte_hash_crc.h>

#include <rte_flow.h>
#include <rte_ring.h>

#include <stdio.h>
#include <signal.h>
#include <unistd.h>

#define BATCH_SIZE 32

#define DROP ((uint16_t)-1)
#define FLOOD ((uint16_t)-2)

static const uint16_t RX_QUEUE_SIZE = 1024;
static const uint16_t TX_QUEUE_SIZE = 1024;

// Buffer count for mempools
static const unsigned MEMPOOL_BUFFER_COUNT = 32768;

bool nf_init(void);
int nf_process(uint16_t device, uint8_t *pkt, uint32_t pkt_len, time_ns_t now);

#define RING_SIZE 16384 /* Must be a power of 2 */
#define POOL_CACHE_SIZE 256

#define MAX_FLOWS 65536
#define EXPIRATION_TIME_NS 1000000000ll // 1 seconds
#define LAN 0
#define WAN 1

#define ORCHESTRATOR_RELATIVE_TRAFFIC_THRESHOLD 0.02
#define ELEPHANT_QUEUE_ID 0 // Let's pick queue 0 for Elephant flows

struct rte_ring *flow_feedback_ring;
struct rte_mempool *flow_id_pool;

struct lcore_conf {
  uint16_t queue_id;
} lcores_conf[RTE_MAX_LCORE];

struct flow_t {
  uint32_t src_ip;
  uint32_t dst_ip;
  uint16_t src_port;
  uint16_t dst_port;
};

struct rte_flow *generate_elephant_rule(uint16_t port_id, struct flow_t *id, uint16_t rx_q) {
  struct rte_flow_attr attr = {.ingress = 1};
  struct rte_flow_item pattern[4];
  struct rte_flow_action action[2];
  struct rte_flow_error error;

  // Zero-out to prevent garbage values in pointers
  memset(pattern, 0, sizeof(pattern));
  memset(action, 0, sizeof(action));

  // 1. ETH: Must be present for many drivers, even if empty
  pattern[0].type = RTE_FLOW_ITEM_TYPE_ETH;

  // 2. IPv4: Provide both SPEC and MASK
  struct rte_flow_item_ipv4 ip_spec = {.hdr = {
                                           .src_addr      = id->src_ip,
                                           .dst_addr      = id->dst_ip,
                                           .next_proto_id = IPPROTO_UDP // Good practice to be specific
                                       }};
  struct rte_flow_item_ipv4 ip_mask = {
      .hdr = {.src_addr = RTE_BE32(0xFFFFFFFF), .dst_addr = RTE_BE32(0xFFFFFFFF), .next_proto_id = 0xFF}};

  pattern[1].type = RTE_FLOW_ITEM_TYPE_IPV4;
  pattern[1].spec = &ip_spec;
  pattern[1].mask = &ip_mask;

  // 3. END: Terminate pattern list
  pattern[2].type = RTE_FLOW_ITEM_TYPE_END;

  // 4. ACTION: Queue
  struct rte_flow_action_queue queue = {.index = rx_q};
  action[0].type                     = RTE_FLOW_ACTION_TYPE_QUEUE;
  action[0].conf                     = &queue;
  action[1].type                     = RTE_FLOW_ACTION_TYPE_END;

  struct rte_flow *flow = rte_flow_create(port_id, &attr, pattern, action, &error);
  if (!flow) {
    rte_exit(EXIT_FAILURE, "Flow can't be created %d message: %s\n", error.type,
             error.message ? error.message : "(no message)");
  }
  return flow;
}

void orchestrator_loop() {
  NF_INFO("Orchestrator core %u started.", rte_lcore_id());

  unsigned nb_devices = rte_eth_dev_count_avail();

  struct CMS *flow_counter          = NULL;
  struct Map *elephant_flows        = NULL;
  struct DoubleChain *elephant_heap = NULL;
  struct Vector *elephant_storage   = NULL;
  uint64_t total_packets            = 0;

  if (cms_allocate(5, 65536, sizeof(struct flow_t), EXPIRATION_TIME_NS, &flow_counter) == 0) {
    rte_exit(EXIT_FAILURE, "Failed to allocate orchestrator Count-Min Sketch\n");
  }

  if (map_allocate(MAX_FLOWS, sizeof(struct flow_t), &elephant_flows) == 0) {
    rte_exit(EXIT_FAILURE, "Failed to allocate orchestrator Map for elephant flows\n");
  }

  if (dchain_allocate(MAX_FLOWS, &elephant_heap) == 0) {
    rte_exit(EXIT_FAILURE, "Failed to allocate orchestrator DoubleChain for elephant flows\n");
  }

  if (vector_allocate(sizeof(struct flow_t), MAX_FLOWS, &elephant_storage) == 0) {
    rte_exit(EXIT_FAILURE, "Failed to allocate orchestrator Vector for elephant flows\n");
  }

  void *dequeued_ptrs[BATCH_SIZE];
  int progress = 0;
  while (total_packets < 10000000) {
    unsigned int n = rte_ring_dequeue_burst(flow_feedback_ring, dequeued_ptrs, BATCH_SIZE, NULL);

    if (n == 0) {
      continue;
    }

    for (unsigned int i = 0; i < n; i++) {
      struct flow_t *id = (struct flow_t *)dequeued_ptrs[i];
      cms_increment(flow_counter, id);
    }

    rte_mempool_put_bulk(flow_id_pool, dequeued_ptrs, n);

    int current_progress = (total_packets * 100) / 10000000;
    if (current_progress >= progress + 10) {
      NF_INFO("Processed %d%% of packets...", current_progress);
      progress = current_progress;
    }

    total_packets += n;
  }

  NF_INFO("Orchestrator finished processing 10 million packets.");

  while (1) {
    unsigned int n = rte_ring_dequeue_burst(flow_feedback_ring, dequeued_ptrs, BATCH_SIZE, NULL);

    if (n == 0) {
      continue;
    }

    for (unsigned int i = 0; i < n; i++) {
      struct flow_t *id = (struct flow_t *)dequeued_ptrs[i];
      int value;
      if (map_get(elephant_flows, id, &value) == 1) {
        continue;
      }

      int flow_count = cms_count_min(flow_counter, id);
      // NF_INFO("Flow count: %d, total packets: %lu (%.2f%%)", flow_count, total_packets,
      //         (float)flow_count / total_packets * 100);

      if (flow_count > total_packets * ORCHESTRATOR_RELATIVE_TRAFFIC_THRESHOLD) {
        NF_INFO("New elephant flow found!");
        int index;
        if (dchain_allocate_new_index(elephant_heap, &index, current_time()) == 0) {
          rte_exit(EXIT_FAILURE, "Failed to allocate new index for elephant flow\n");
        }
        void *vector_value = 0;
        vector_borrow(elephant_storage, index, (void **)&vector_value);
        memcpy(vector_value, id, sizeof(struct flow_t));
        map_put(elephant_flows, vector_value, index);
        vector_return(elephant_storage, index, vector_value);

        for (uint16_t dev = 0; dev < nb_devices; dev++) {
          generate_elephant_rule(dev, id, ELEPHANT_QUEUE_ID);
        }
      }
    }

    rte_mempool_put_bulk(flow_id_pool, dequeued_ptrs, n);
  }
}

struct pkt_tag_t {
  uint8_t i;
  struct flow_t id;
};

bool pkt_tag_lt(struct pkt_tag_t *a, struct pkt_tag_t *b) {
  if (a->id.src_ip != b->id.src_ip) {
    return a->id.src_ip < b->id.src_ip;
  }

  if (a->id.dst_ip != b->id.dst_ip) {
    return a->id.dst_ip < b->id.dst_ip;
  }

  if (a->id.src_port != b->id.src_port) {
    return a->id.src_port < b->id.src_port;
  }

  return a->id.dst_port < b->id.dst_port;
}

void sort_tagged_packets(struct pkt_tag_t *pkts, uint16_t n) {
  for (int i = 1; i < n; i++) {
    struct pkt_tag_t curr = pkts[i];
    int j                 = i - 1;

    while (j >= 0 && pkt_tag_lt(&pkts[j], &curr)) {
      pkts[j + 1] = pkts[j];
      j           = j - 1;
    }
    pkts[j + 1] = curr;
  }
}

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

      if (rx_count == 0) {
        continue;
      }

      for (uint16_t n = 0; n < rx_count; n++) {
        uint16_t src_device = mbufs[n]->port;
        uint8_t *pkt        = rte_pktmbuf_mtod(mbufs[n], uint8_t *);
        uint32_t pkt_len    = mbufs[n]->pkt_len;

        struct flow_t *id_entry;

        if (rte_mempool_get(flow_id_pool, (void **)&id_entry) == 0) {
          *id_entry = flow_from_pkt(pkt, pkt_len);
          if (rte_ring_enqueue(flow_feedback_ring, id_entry) < 0) {
            rte_mempool_put(flow_id_pool, id_entry);
          }
        }

        time_ns_t now       = current_time();
        uint16_t dst_device = nf_process(src_device, pkt, pkt_len, now);

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
          rte_pktmbuf_free(mbufs[n]); // should not happen, but we're in
                                      // the unverified case anyway
        }
        tx_batch_per_port[dst_device].tx_count = 0;
      }
    }
  }
}

uint64_t stride_sizes[BATCH_SIZE];

void elephant_worker_loop() {
  if (!nf_init()) {
    rte_exit(EXIT_FAILURE, "Error initializing NF");
  }

  unsigned lcore_id = rte_lcore_id();
  uint16_t queue_id = lcores_conf[lcore_id].queue_id;

  NF_INFO("Elephant worker core %u started, listening on queue %u.", lcore_id, queue_id);

  memset(stride_sizes, 0, sizeof(stride_sizes));

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

      // NF_INFO("Received batch of %u packets on Elephant queue!", rx_count);

      uint8_t *pkts[BATCH_SIZE];
      uint32_t pkt_lens[BATCH_SIZE];
      struct pkt_tag_t tagged_pkts[BATCH_SIZE];

      for (uint16_t n = 0; n < rx_count; n++) {
        pkts[n]           = rte_pktmbuf_mtod(mbufs[n], uint8_t *);
        pkt_lens[n]       = mbufs[n]->pkt_len;
        tagged_pkts[n].i  = n;
        tagged_pkts[n].id = flow_from_pkt(pkts[n], pkt_lens[n]);
      }

      sort_tagged_packets(tagged_pkts, rx_count);

      uint8_t current_batch = 0;

      uint8_t strides[BATCH_SIZE];
      for (uint8_t i = 0; i < BATCH_SIZE; i++) {
        strides[i] = 1;
      }

      for (uint16_t n = 0; n < rx_count - 1; n++) {
        if (memcmp(&tagged_pkts[n].id, &tagged_pkts[n + 1].id, sizeof(struct flow_t)) == 0) {
          strides[current_batch]++;
        } else {
          current_batch++;
        }
      }

      time_ns_t now = current_time();

      current_batch = 0;
      for (uint16_t n = 0; n < rx_count; current_batch++) {
        stride_sizes[strides[current_batch] - 1]++;

        uint8_t i           = tagged_pkts[n].i;
        uint8_t *pkt        = pkts[i];
        uint32_t pkt_len    = pkt_lens[i];
        uint16_t src_device = mbufs[i]->port;

        struct flow_t *id_entry;

        if (rte_mempool_get(flow_id_pool, (void **)&id_entry) == 0) {
          *id_entry = tagged_pkts[n].id;
          if (rte_ring_enqueue(flow_feedback_ring, id_entry) < 0) {
            rte_mempool_put(flow_id_pool, id_entry);
          }
        }

        uint16_t dst_device = nf_process(src_device, pkt, pkt_len, now);

        if (dst_device == DROP) {
          for (uint8_t stride_size = 0; stride_size < strides[current_batch]; stride_size++) {
            rte_pktmbuf_free(mbufs[tagged_pkts[n + stride_size].i]);
          }
        } else {
          for (uint8_t stride_size = 0; stride_size < strides[current_batch]; stride_size++) {
            uint16_t tx_count                             = tx_batch_per_port[dst_device].tx_count;
            tx_batch_per_port[dst_device].batch[tx_count] = mbufs[tagged_pkts[n + stride_size].i];
            tx_batch_per_port[dst_device].tx_count++;
          }
        }

        n += strides[current_batch];
      }

      for (uint16_t dst_device = 0; dst_device < devices_count; dst_device++) {
        uint16_t sent_count = rte_eth_tx_burst(dst_device, queue_id, tx_batch_per_port[dst_device].batch,
                                               tx_batch_per_port[dst_device].tx_count);
        for (uint16_t n = sent_count; n < tx_batch_per_port[dst_device].tx_count; n++) {
          rte_pktmbuf_free(mbufs[tagged_pkts[n].i]);
        }
        tx_batch_per_port[dst_device].tx_count = 0;
      }
    }
  }
}

void handle_sigint(int sig) {
  printf("\nCaught signal %d (SIGINT). Cleaning up...\n", sig);

  for (size_t i = 0; i < BATCH_SIZE; i++) {
    printf("Stride size %zu: %lu packets\n", i + 1, stride_sizes[i]);
  }

  _exit(0);
}

int main(int argc, char **argv) {
  signal(SIGINT, handle_sigint);

  // Initialize the DPDK Environment Abstraction Layer (EAL)
  int ret = rte_eal_init(argc, argv);
  if (ret < 0) {
    rte_exit(EXIT_FAILURE, "Error with EAL initialization, ret=%d\n", ret);
  }
  argc -= ret;
  argv += ret;

  unsigned nb_devices = rte_eth_dev_count_avail();

  // cache size (per-core, not useful in a single-threaded app)
  unsigned cache_size = 0;

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

  struct rte_mempool *mbuf_pool = rte_pktmbuf_pool_create("MEMPOOL", MEMPOOL_BUFFER_COUNT * nb_devices, cache_size,
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

  uint16_t worker_id;
  uint16_t worker_idx = 0;
  RTE_LCORE_FOREACH_WORKER(worker_id) {
    if (worker_idx == 0) {
      // This is our Elephant core
      lcores_conf[worker_id].queue_id = ELEPHANT_QUEUE_ID;
      rte_eal_remote_launch((lcore_function_t *)elephant_worker_loop, NULL, worker_id);
    } else {
      // Generic workers
      lcores_conf[worker_id].queue_id = generic_queues_start + (worker_idx - 1);
      rte_eal_remote_launch((lcore_function_t *)mice_worker_loop, NULL, worker_id);
    }
    worker_idx++;
  }

  orchestrator_loop();

  return 0;
}

struct state_t {
  struct Map *fm;
  struct Vector *fv;
  struct DoubleChain *heap;
};

RTE_DEFINE_PER_LCORE(struct state_t, state);

bool nf_init(void) {
  struct state_t *state = &RTE_PER_LCORE(state);

  if (map_allocate(MAX_FLOWS, sizeof(struct flow_t), &(state->fm)) == 0) {
    return false;
  }

  if (vector_allocate(sizeof(struct flow_t), MAX_FLOWS, &(state->fv)) == 0) {
    return false;
  }

  if (dchain_allocate(MAX_FLOWS, &(state->heap)) == 0) {
    return false;
  }

  return true;
}

void flow_manager_expire(time_ns_t time) {
  struct state_t *state = &RTE_PER_LCORE(state);
  expire_items_single_map(state->heap, state->fv, state->fm, time);
}

void flow_manager_allocate_or_refresh_flow(struct flow_t *id, time_ns_t time) {
  struct state_t *state = &RTE_PER_LCORE(state);

  int index;
  if (map_get(state->fm, id, &index)) {
    NF_DEBUG("Rejuvenated flow");
    dchain_rejuvenate_index(state->heap, index, time);
    return;
  }
  if (!dchain_allocate_new_index(state->heap, &index, time)) {
    // No luck, the flow table is full, but we can at least let the
    // outgoing traffic out.
    return;
  }

  NF_DEBUG("Allocating new flow");

  struct flow_t *key = 0;
  vector_borrow(state->fv, index, (void **)&key);
  memcpy((void *)key, (void *)id, sizeof(struct flow_t));
  map_put(state->fm, key, index);
  vector_return(state->fv, index, key);
}

bool flow_manager_get_refresh_flow(struct flow_t *id, time_ns_t time) {
  struct state_t *state = &RTE_PER_LCORE(state);

  int index;
  if (map_get(state->fm, id, &index) == 0) {
    return false;
  }

  dchain_rejuvenate_index(state->heap, index, time);
  return true;
}

int nf_process(uint16_t device, uint8_t *pkt, uint32_t pkt_len, time_ns_t now) {
  flow_manager_expire(now);

  struct rte_ether_hdr *rte_ether_header = (struct rte_ether_hdr *)pkt;

  if (rte_ether_header->ether_type != rte_be_to_cpu_16(RTE_ETHER_TYPE_IPV4)) {
    NF_DEBUG("Not IPv4, dropping");
    return DROP;
  }

  struct rte_ipv4_hdr *rte_ipv4_header = (struct rte_ipv4_hdr *)(rte_ether_header + 1);

  if (rte_ipv4_header->next_proto_id != IPPROTO_TCP && rte_ipv4_header->next_proto_id != IPPROTO_UDP) {
    NF_DEBUG("Not TCP or UDP, dropping");
    return DROP;
  }

  struct tcpudp_hdr *tcpudp_header = (struct tcpudp_hdr *)(rte_ipv4_header + 1);

  if (device == LAN) {
    NF_DEBUG("Seen packet from LAN, allocating/rejuvenating flow and sending to WAN");
    struct flow_t id = {
        .src_ip   = rte_ipv4_header->src_addr,
        .dst_ip   = rte_ipv4_header->dst_addr,
        .src_port = tcpudp_header->src_port,
        .dst_port = tcpudp_header->dst_port,
    };

    flow_manager_allocate_or_refresh_flow(&id, now);

    return WAN;
  } else {
    NF_DEBUG("Seen packet from WAN, checking if flow is known and sending to LAN if so");
    // Inverse the src and dst for the "reply flow"
    struct flow_t id = {
        .src_ip   = rte_ipv4_header->dst_addr,
        .dst_ip   = rte_ipv4_header->src_addr,
        .src_port = tcpudp_header->dst_port,
        .dst_port = tcpudp_header->src_port,
    };

    if (!flow_manager_get_refresh_flow(&id, now)) {
      NF_DEBUG("Unknown external flow, dropping");
      return DROP;
    }

    return LAN;
  }
}
