#pragma once

#include "boilerplate.h"

struct flow_t {
  uint32_t src_ip;
  uint32_t dst_ip;
  uint16_t src_port;
  uint16_t dst_port;
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

struct lcore_conf {
  uint16_t queue_id;
} lcores_conf[RTE_MAX_LCORE];

void sub_worker_loop() {
  if (!nf_init()) {
    rte_exit(EXIT_FAILURE, "Error initializing NF");
  }

  unsigned lcore_id = rte_lcore_id();
  uint16_t queue_id = lcores_conf[lcore_id].queue_id;

  NF_INFO("Worker core %u started, listening on queue %u.", lcore_id, queue_id);

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

      struct flow_t flows[BATCH_SIZE];

      for (uint16_t n = 0; n < rx_count; n++) {
        uint8_t *pkt     = rte_pktmbuf_mtod(mbufs[n], uint8_t *);
        uint32_t pkt_len = mbufs[n]->pkt_len;
        flows[n]         = flow_from_pkt(pkt, pkt_len);
      }

      uint16_t n = 0;
      while (n < rx_count) {
        time_ns_t now       = current_time();
        uint8_t *pkt        = rte_pktmbuf_mtod(mbufs[n], uint8_t *);
        uint32_t pkt_len    = mbufs[n]->pkt_len;
        uint16_t dst_device = nf_process(device, pkt, pkt_len, now);

        if (dst_device == DROP) {
          rte_pktmbuf_free(mbufs[n]);
        } else {
          uint16_t tx_count                             = tx_batch_per_port[dst_device].tx_count;
          tx_batch_per_port[dst_device].batch[tx_count] = mbufs[n];
          tx_batch_per_port[dst_device].tx_count++;
        }

        uint8_t logical_batch_size = 0;
        for (uint16_t i = n + 1; i < rx_count; i++) {
          if (memcmp(flows + n, flows + i, sizeof(struct flow_t)) == 0) {
            logical_batch_size++;

            if (dst_device == DROP) {
              rte_pktmbuf_free(mbufs[i]);
            } else {
              uint16_t tx_count                             = tx_batch_per_port[dst_device].tx_count;
              tx_batch_per_port[dst_device].batch[tx_count] = mbufs[i];
              tx_batch_per_port[dst_device].tx_count++;
            }
          } else {
            break;
          }
        }

        n += logical_batch_size + 1;
      }

      uint16_t sent_count =
          rte_eth_tx_burst(device, queue_id, tx_batch_per_port[device].batch, tx_batch_per_port[device].tx_count);
      for (uint16_t n = sent_count; n < tx_batch_per_port[device].tx_count; n++) {
        rte_pktmbuf_free(tx_batch_per_port[device].batch[n]);
      }
      tx_batch_per_port[device].tx_count = 0;
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

  uint16_t nb_workers = rte_lcore_count();

  // Need enough mbufs for all RX rings (nb_workers * nb_devices * RX_QUEUE_SIZE)
  // plus headroom for in-flight packets and per-core caches.
  uint32_t pool_size = nb_workers * nb_devices * (RX_QUEUE_SIZE + MBUF_CACHE_SIZE) * 2;
  struct rte_mempool *mbuf_pool =
      rte_pktmbuf_pool_create("MEMPOOL", pool_size, MBUF_CACHE_SIZE, priv_size, data_room_size, rte_socket_id());
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
    RTE_LCORE_FOREACH(worker_id) {
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

  return 0;
}

static inline void worker_loop() {
  uint16_t worker_id;
  RTE_LCORE_FOREACH_WORKER(worker_id) { rte_eal_remote_launch((lcore_function_t *)sub_worker_loop, NULL, worker_id); }
  sub_worker_loop();
}