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

struct pkt_tag_t {
  uint8_t i;
  struct flow_t id;
};

struct pkt_tag_t build_pkt_tag(uint8_t *pkt, uint32_t pkt_len, uint16_t i) {
  struct pkt_tag_t tag;
  tag.i = i;

  if (pkt_len < sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr) + sizeof(struct tcpudp_hdr)) {
    return tag;
  }

  struct rte_ether_hdr *ether_hdr = (struct rte_ether_hdr *)pkt;

  if (ether_hdr->ether_type != rte_be_to_cpu_16(RTE_ETHER_TYPE_IPV4)) {
    return tag;
  }

  struct rte_ipv4_hdr *ipv4_hdr = (struct rte_ipv4_hdr *)(ether_hdr + 1);

  if (ipv4_hdr->next_proto_id != IPPROTO_TCP && ipv4_hdr->next_proto_id != IPPROTO_UDP) {
    return tag;
  }

  struct tcpudp_hdr *tcpudp_hdr = (struct tcpudp_hdr *)(ipv4_hdr + 1);

  tag.id.src_ip   = ipv4_hdr->src_addr;
  tag.id.dst_ip   = ipv4_hdr->dst_addr;
  tag.id.src_port = tcpudp_hdr->src_port;
  tag.id.dst_port = tcpudp_hdr->dst_port;

  return tag;
}

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

    while (j >= 0 && pkt_tag_lt(pkts + j, &curr)) {
      memcpy(pkts + j + 1, pkts + j, sizeof(struct pkt_tag_t));
      j = j - 1;
    }
    memcpy(pkts + j + 1, &curr, sizeof(struct pkt_tag_t));
  }
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
void handle_sigusr1(int sig) {
  for (size_t i = 0; i < BATCH_SIZE; i++) {
    printf("Stride size %zu: %lu packets\n", i + 1, stride_sizes[i]);
  }
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

  uint16_t nb_workers = rte_lcore_count();
  uint32_t pool_size = nb_workers * nb_devices * (RX_QUEUE_SIZE + MBUF_CACHE_SIZE) * 2;
  struct rte_mempool *mbuf_pool = rte_pktmbuf_pool_create("MEMPOOL", pool_size, MBUF_CACHE_SIZE,
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
  signal(SIGUSR1, handle_sigusr1);
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

      uint8_t *pkts[BATCH_SIZE];
      uint32_t pkt_lens[BATCH_SIZE];
      struct pkt_tag_t tagged_pkts[BATCH_SIZE];

      for (uint16_t n = 0; n < rx_count; n++) {
        pkts[n]        = rte_pktmbuf_mtod(mbufs[n], uint8_t *);
        pkt_lens[n]    = mbufs[n]->pkt_len;
        tagged_pkts[n] = build_pkt_tag(pkts[n], pkt_lens[n], n);
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
#if TRACK_STRIDE_SIZES
        stride_sizes[strides[current_batch] - 1]++;
#endif
        uint8_t i           = tagged_pkts[n].i;
        uint16_t src_device = mbufs[i]->port;
        uint8_t *pkt        = pkts[i];
        uint32_t pkt_len    = pkt_lens[i];
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
        uint16_t sent_count = rte_eth_tx_burst(dst_device, 0, tx_batch_per_port[dst_device].batch,
                                               tx_batch_per_port[dst_device].tx_count);
        for (uint16_t n = sent_count; n < tx_batch_per_port[dst_device].tx_count; n++) {
          rte_pktmbuf_free(tx_batch_per_port[dst_device].batch[n]);
        }
        tx_batch_per_port[dst_device].tx_count = 0;
      }
    }
  }
}