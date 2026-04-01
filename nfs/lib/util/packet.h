#pragma once

#include "tcpudp_hdr.h"

#include <rte_ether.h>
#include <rte_ip.h>

struct tcpudp_hdrs_t {
  struct rte_ether_hdr *ether_hdr;
  struct rte_ipv4_hdr *ipv4_hdr;
  struct tcpudp_hdr *tcpudp_hdr;
};

static inline struct tcpudp_hdrs_t nf_get_tcpudp_hdrs(uint8_t *pkt, uint32_t pkt_len) {
  struct tcpudp_hdrs_t hdrs = {0};

  hdrs.ether_hdr = (struct rte_ether_hdr *)pkt;

  if (pkt_len < sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr) + sizeof(struct tcpudp_hdr)) {
    return hdrs;
  }

  if (hdrs.ether_hdr->ether_type != rte_be_to_cpu_16(RTE_ETHER_TYPE_IPV4)) {
    return hdrs;
  }

  hdrs.ipv4_hdr = (struct rte_ipv4_hdr *)(hdrs.ether_hdr + 1);

  if (hdrs.ipv4_hdr->next_proto_id != IPPROTO_TCP && hdrs.ipv4_hdr->next_proto_id != IPPROTO_UDP) {
    return hdrs;
  }

  hdrs.tcpudp_hdr = (struct tcpudp_hdr *)(hdrs.ipv4_hdr + 1);

  return hdrs;
}