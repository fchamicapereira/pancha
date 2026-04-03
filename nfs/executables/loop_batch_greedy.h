#pragma once

#include "boilerplate.h"

#ifndef TRACK_STRIDE_SIZES
#define TRACK_STRIDE_SIZES 0
#endif

bool flow_match(uint8_t *pkt0, uint32_t pkt_len0, uint8_t *pkt1, uint32_t pkt_len1);

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

      if (rx_count == 0) {
        continue;
      }

      uint8_t *pkts[BATCH_SIZE];
      uint32_t pkt_lens[BATCH_SIZE];

      uint8_t current_batch = 0;

      uint8_t strides[BATCH_SIZE];
      for (uint8_t i = 0; i < BATCH_SIZE; i++) {
        strides[i] = 1;
      }

      for (uint16_t n = 0; n < rx_count - 1; n++) {
        pkts[n]         = rte_pktmbuf_mtod(mbufs[n], uint8_t *);
        pkts[n + 1]     = rte_pktmbuf_mtod(mbufs[n + 1], uint8_t *);
        pkt_lens[n]     = mbufs[n]->pkt_len;
        pkt_lens[n + 1] = mbufs[n + 1]->pkt_len;
        if (flow_match(pkts[n], pkt_lens[n], pkts[n + 1], pkt_lens[n + 1])) {
          strides[current_batch]++;
        } else {
          current_batch++;
        }
      }

      time_ns_t now = current_time();

      current_batch = 0;
      for (uint16_t n = 0; n < rx_count; current_batch++) {
#if TRACK_STRIDE_SIZES
        stride_sizes[strides[current_batch] - 1]++;
#endif

        uint16_t src_device = mbufs[n]->port;
        uint8_t *pkt        = pkts[n];
        uint32_t pkt_len    = pkt_lens[n];
        uint16_t dst_device = nf_process(src_device, pkt, pkt_len, now);

        if (dst_device == DROP) {
          for (uint8_t i = 0; i < strides[current_batch]; i++) {
            rte_pktmbuf_free(mbufs[n + i]);
          }
        } else {
          for (uint8_t i = 0; i < strides[current_batch]; i++) {
            uint16_t tx_count                             = tx_batch_per_port[dst_device].tx_count;
            tx_batch_per_port[dst_device].batch[tx_count] = mbufs[n + i];
            tx_batch_per_port[dst_device].tx_count++;
          }
        }

        n += strides[current_batch];
      }

      for (uint16_t dst_device = 0; dst_device < devices_count; dst_device++) {
        uint16_t sent_count = rte_eth_tx_burst(dst_device, 0, tx_batch_per_port[dst_device].batch,
                                               tx_batch_per_port[dst_device].tx_count);
        for (uint16_t n = sent_count; n < tx_batch_per_port[dst_device].tx_count; n++) {
          rte_pktmbuf_free(mbufs[n]);
        }
        tx_batch_per_port[dst_device].tx_count = 0;
      }
    }
  }
}