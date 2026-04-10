#pragma once

#include "boilerplate.h"

#ifndef TRACK_STRIDE_SIZES
#define TRACK_STRIDE_SIZES 0
#endif

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

// Initializes the given device using the given memory pool
int nf_init_device(uint16_t device, struct rte_mempool *mbuf_pool) {
  int retval;

  // device_conf passed to rte_eth_dev_configure cannot be NULL
  struct rte_eth_conf device_conf = {0};
  // device_conf.rxmode.hw_strip_crc = 1;

  // Configure the device (1, 1 == number of RX/TX queues)
  retval = rte_eth_dev_configure(device, 1, 1, &device_conf);
  if (retval != 0) {
    return retval;
  }

  // Allocate and set up a TX queue (NULL == default config)
  retval = rte_eth_tx_queue_setup(device, 0, TX_QUEUE_SIZE, rte_eth_dev_socket_id(device), NULL);
  if (retval != 0) {
    return retval;
  }

  // Allocate and set up RX queues (NULL == default config)
  retval = rte_eth_rx_queue_setup(device, 0, RX_QUEUE_SIZE, rte_eth_dev_socket_id(device), NULL, mbuf_pool);
  if (retval != 0) {
    return retval;
  }

  retval = rte_eth_dev_start(device);
  if (retval != 0) {
    return retval;
  }

  rte_eth_promiscuous_enable(device);
  if (rte_eth_promiscuous_get(device) != 1) {
    return retval;
  }

  return 0;
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
  uint16_t priv_size = 0;

  uint16_t data_room_size = RTE_MBUF_DEFAULT_BUF_SIZE;

  struct rte_mempool *mbuf_pool = rte_pktmbuf_pool_create("MEMPOOL", MEMPOOL_BUFFER_COUNT * nb_devices, MBUF_CACHE_SIZE,
                                                          priv_size, data_room_size, rte_socket_id());
  if (mbuf_pool == NULL) {
    rte_exit(EXIT_FAILURE, "Cannot create pool: %s\n", rte_strerror(rte_errno));
  }

  // Initialize all devices
  for (uint16_t device = 0; device < nb_devices; device++) {
    ret = nf_init_device(device, mbuf_pool);
    if (ret == 0) {
      NF_INFO("Initialized device %" PRIu16 ".", device);
    } else {
      rte_exit(EXIT_FAILURE, "Cannot init device %" PRIu16 ": %d", device, ret);
    }
  }

  if (!nf_init()) {
    rte_exit(EXIT_FAILURE, "Error initializing NF");
  }

#if TRACK_STRIDE_SIZES
  memset(stride_sizes, 0, sizeof(stride_sizes));
  signal(SIGINT, handle_sigint);
#endif

  return 0;
}

static inline void worker_loop() {
  NF_INFO("Core %u forwarding packets.", rte_lcore_id());

  struct tx_mbuf_batch {
    struct rte_mbuf *batch[BATCH_SIZE];
    uint16_t tx_count;
  };

  uint16_t devices_count                  = rte_eth_dev_count_avail();
  struct tx_mbuf_batch *tx_batch_per_port = (struct tx_mbuf_batch *)calloc(devices_count, sizeof(struct tx_mbuf_batch));

  while (1) {
    for (uint16_t device = 0; device < devices_count; device++) {
      struct rte_mbuf *mbufs[BATCH_SIZE];
      uint16_t rx_count = rte_eth_rx_burst(device, 0, mbufs, BATCH_SIZE);

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

#if TRACK_STRIDE_SIZES
        stride_sizes[logical_batch_size]++;
#endif
      }

      uint16_t sent_count =
          rte_eth_tx_burst(device, 0, tx_batch_per_port[device].batch, tx_batch_per_port[device].tx_count);
      for (uint16_t n = sent_count; n < tx_batch_per_port[device].tx_count; n++) {
        rte_pktmbuf_free(tx_batch_per_port[device].batch[n]);
      }
      tx_batch_per_port[device].tx_count = 0;
    }
  }
}