#include "boilerplate.h"

#define MAX_FLOWS 65536
#define EXPIRATION_TIME_NS 1000000000 // 1 seconds
#define SKETCH_WIDTH 65536
#define SKETCH_HEIGHT 7
#define MAX_CLIENTS 65536

#define LAN 0
#define WAN 1

struct flow {
  uint32_t src_ip;
  uint32_t dst_ip;
  uint16_t src_port;
  uint16_t dst_port;
};

struct pkt_tag_t {
  uint8_t i;
  struct flow id;
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

struct flow flow_from_pkt(uint8_t *pkt, uint32_t pkt_len) {
  struct flow id = {0};

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

void worker_loop_batched_sorted() {
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
      struct pkt_tag_t tagged_pkts[BATCH_SIZE];

      for (uint16_t n = 0; n < rx_count; n++) {
        pkts[n]           = rte_pktmbuf_mtod(mbufs[n], uint8_t *);
        pkt_lens[n]       = mbufs[n]->pkt_len;
        tagged_pkts[n].i  = n;
        tagged_pkts[n].id = flow_from_pkt(pkts[n], pkt_lens[n]);
      }

      // printf("Before: ");
      // for (int i = 0; i < rx_count; i++) {
      //   printf("%u,", tagged_pkts[i].i);
      // }
      // printf("\n");

      sort_tagged_packets(tagged_pkts, rx_count);

      // printf("Sorted batch of %u packets: ", rx_count);
      // for (int i = 0; i < rx_count; i++) {
      //   printf("%u,", tagged_pkts[i].i);
      // }
      // printf("\n");
      // exit(0);

      uint8_t current_batch = 0;

      uint8_t strides[BATCH_SIZE];
      memset(strides, 1, sizeof(strides));
      for (uint16_t n = 0; n < rx_count - 1; n++) {
        if (memcmp(&tagged_pkts[n].id, &tagged_pkts[n + 1].id, sizeof(struct flow)) == 0) {
          strides[current_batch]++;
        } else {
          current_batch++;
        }
      }

      time_ns_t now = current_time();

      current_batch = 0;
      for (uint16_t n = 0; n < rx_count; current_batch++) {
        uint8_t i           = tagged_pkts[n].i;
        uint8_t *pkt        = pkts[i];
        uint32_t pkt_len    = pkt_lens[i];
        uint16_t src_device = mbufs[i]->port;

        packet_state_total_length(pkt, &(mbufs[i]->pkt_len));

        uint16_t dst_device = nf_process(src_device, &pkt, pkt_len, now, mbufs[i]);
        nf_return_all_chunks(pkt);

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
          rte_pktmbuf_free(mbufs[tagged_pkts[n].i]);
        }
        tx_batch_per_port[dst_device].tx_count = 0;
      }
    }
  }
}

int main(int argc, char **argv) {
  nf_setup(argc, argv);
  worker_loop_batched_sorted();
  return 0;
}

struct State {
  struct CMS *flow_counter_5tuple;
  struct CMS *flow_counter_ips;
  struct CMS *flow_counter_ports;
};

struct State state;

struct flow_5tuple {
  uint32_t src_ip;
  uint32_t dst_ip;
  uint16_t src_port;
  uint16_t dst_port;
};

struct flow_ips {
  uint32_t src_ip;
  uint32_t dst_ip;
};

struct flow_ports {
  uint16_t src_port;
  uint16_t dst_port;
};

bool nf_init(void) {
  if (cms_allocate(SKETCH_HEIGHT, SKETCH_WIDTH, sizeof(struct flow_5tuple), EXPIRATION_TIME_NS,
                   &(state.flow_counter_5tuple)) == 0) {
    return false;
  }

  if (cms_allocate(SKETCH_HEIGHT, SKETCH_WIDTH, sizeof(struct flow_ips), EXPIRATION_TIME_NS,
                   &(state.flow_counter_ips)) == 0) {
    return false;
  }

  if (cms_allocate(SKETCH_HEIGHT, SKETCH_WIDTH, sizeof(struct flow_ports), EXPIRATION_TIME_NS,
                   &(state.flow_counter_ports)) == 0) {
    return false;
  }

  return true;
}

void expire_entries(time_ns_t now) {
  assert(now >= 0); // we don't support the past
  assert(sizeof(time_ns_t) <= sizeof(uint64_t));
  uint64_t time_u     = (uint64_t)now; // OK because of the two asserts
  time_ns_t last_time = time_u - EXPIRATION_TIME_NS;
  cms_periodic_cleanup(state.flow_counter_5tuple, now);
  cms_periodic_cleanup(state.flow_counter_ips, now);
  cms_periodic_cleanup(state.flow_counter_ports, now);
}

int nf_process(uint16_t device, uint8_t **buffer, uint16_t packet_length, time_ns_t now, struct rte_mbuf *mbuf) {
  // expire_entries(now);

  struct rte_ether_hdr *rte_ether_header = nf_then_get_ether_header(buffer);
  struct rte_ipv4_hdr *rte_ipv4_header   = nf_then_get_ipv4_header(rte_ether_header, buffer);
  if (rte_ipv4_header == NULL) {
    NF_DEBUG("Not IPv4, dropping");
    return DROP;
  }

  struct tcpudp_hdr *tcpudp_header = nf_then_get_tcpudp_header(rte_ipv4_header, buffer);
  if (tcpudp_header == NULL) {
    NF_DEBUG("Not TCP/UDP, dropping");
    return DROP;
  }

  struct flow_5tuple flow_5tuple = {
      .src_ip   = rte_ipv4_header->src_addr,
      .dst_ip   = rte_ipv4_header->dst_addr,
      .src_port = tcpudp_header->src_port,
      .dst_port = tcpudp_header->dst_port,
  };

  struct flow_ips flow_ips = {
      .src_ip = rte_ipv4_header->src_addr,
      .dst_ip = rte_ipv4_header->dst_addr,
  };

  struct flow_ports flow_ports = {
      .src_port = tcpudp_header->src_port,
      .dst_port = tcpudp_header->dst_port,
  };

  cms_increment(state.flow_counter_5tuple, &flow_5tuple);
  cms_increment(state.flow_counter_ips, &flow_ips);
  cms_increment(state.flow_counter_ports, &flow_ports);

  if (device == LAN) {
    return WAN;
  } else {
    return LAN;
  }
}
