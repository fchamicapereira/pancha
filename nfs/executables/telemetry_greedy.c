#include "boilerplate.h"

#define MAX_FLOWS 65536
#define EXPIRATION_TIME_NS 1000000000 // 1 seconds
#define SKETCH_WIDTH 65536
#define SKETCH_HEIGHT 7
#define MAX_CLIENTS 65536

#define LAN 0
#define WAN 1

bool flow_match(uint8_t *pkt0, uint32_t pkt_len0, uint8_t *pkt1, uint32_t pkt_len1) {
  if (pkt_len0 != pkt_len1) {
    return false;
  }

  if (pkt_len0 < sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr) + sizeof(struct tcpudp_hdr)) {
    return false;
  }

  struct rte_ether_hdr *ether_hdr_0 = (struct rte_ether_hdr *)pkt0;
  struct rte_ether_hdr *ether_hdr_1 = (struct rte_ether_hdr *)pkt1;

  if (ether_hdr_0->ether_type != ether_hdr_1->ether_type) {
    return false;
  }

  if (ether_hdr_0->ether_type != rte_be_to_cpu_16(RTE_ETHER_TYPE_IPV4)) {
    return false;
  }

  struct rte_ipv4_hdr *ipv4_hdr_0 = (struct rte_ipv4_hdr *)(ether_hdr_0 + 1);
  struct rte_ipv4_hdr *ipv4_hdr_1 = (struct rte_ipv4_hdr *)(ether_hdr_1 + 1);

  if (ipv4_hdr_0->next_proto_id != ipv4_hdr_1->next_proto_id) {
    return false;
  }

  if (ipv4_hdr_0->next_proto_id != IPPROTO_TCP && ipv4_hdr_0->next_proto_id != IPPROTO_UDP) {
    return false;
  }

  struct tcpudp_hdr *tcpudp_hdr_0 = (struct tcpudp_hdr *)(ipv4_hdr_0 + 1);
  struct tcpudp_hdr *tcpudp_hdr_1 = (struct tcpudp_hdr *)(ipv4_hdr_1 + 1);

  return (ipv4_hdr_0->src_addr == ipv4_hdr_1->src_addr) && (ipv4_hdr_0->dst_addr == ipv4_hdr_1->dst_addr) &&
         (tcpudp_hdr_0->src_port == tcpudp_hdr_1->src_port) && (tcpudp_hdr_0->dst_port == tcpudp_hdr_1->dst_port);
}

void worker_loop_batched_greedy() {
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
      memset(strides, 1, sizeof(strides));

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
        uint8_t *pkt        = pkts[n];
        uint32_t pkt_len    = pkt_lens[n];
        uint16_t src_device = mbufs[n]->port;

        packet_state_total_length(pkt, &(mbufs[n]->pkt_len));

        uint16_t dst_device = nf_process(src_device, &pkt, pkt_len, now, mbufs[n]);
        nf_return_all_chunks(pkt);

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

int main(int argc, char **argv) {
  nf_setup(argc, argv);
  worker_loop_batched_greedy();
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
