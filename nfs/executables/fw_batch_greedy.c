#define TRACK_STRIDE_SIZES 0

#include "loop_batch_greedy.h"

#define MAX_FLOWS 65536
#define EXPIRATION_TIME_NS 1000000000 // 1 seconds
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

int main(int argc, char **argv) {
  nf_setup(argc, argv);
  worker_loop();
  return 0;
}

struct state_t {
  struct Map *fm;
  struct Vector *fv;
  struct DoubleChain *heap;
};

struct state_t state;

struct flow_t {
  uint32_t src_ip;
  uint32_t dst_ip;
  uint16_t src_port;
  uint16_t dst_port;
};

bool nf_init(void) {
  if (map_allocate(MAX_FLOWS, sizeof(struct flow_t), &(state.fm)) == 0) {
    return false;
  }

  if (vector_allocate(sizeof(struct flow_t), MAX_FLOWS, &(state.fv)) == 0) {
    return false;
  }

  if (dchain_allocate(MAX_FLOWS, &(state.heap)) == 0) {
    return false;
  }

  return true;
}
void flow_manager_allocate_or_refresh_flow(struct flow_t *id, time_ns_t time) {
  int index;
  if (map_get(state.fm, id, &index)) {
    NF_DEBUG("Rejuvenated flow");
    dchain_rejuvenate_index(state.heap, index, time);
    return;
  }
  if (!dchain_allocate_new_index(state.heap, &index, time)) {
    // No luck, the flow table is full, but we can at least let the
    // outgoing traffic out.
    return;
  }

  NF_DEBUG("Allocating new flow");

  struct flow_t *key = 0;
  vector_borrow(state.fv, index, (void **)&key);
  memcpy((void *)key, (void *)id, sizeof(struct flow_t));
  map_put(state.fm, key, index);
  vector_return(state.fv, index, key);
}

bool flow_manager_get_refresh_flow(struct flow_t *id, time_ns_t time) {
  int index;
  if (map_get(state.fm, id, &index) == 0) {
    return false;
  }

  dchain_rejuvenate_index(state.heap, index, time);
  return true;
}

int nf_process(uint16_t device, uint8_t *pkt, uint32_t pkt_len, time_ns_t now) {
  expire_items_single_map(state.heap, state.fv, state.fm, now - EXPIRATION_TIME_NS);

  struct tcpudp_hdrs_t hdrs = nf_get_tcpudp_hdrs(pkt, pkt_len);

  if (hdrs.ipv4_hdr == NULL) {
    NF_DEBUG("Not IPv4, dropping");
    return DROP;
  }

  if (hdrs.tcpudp_hdr == NULL) {
    NF_DEBUG("Not TCP/UDP, dropping");
    return DROP;
  }

  if (device == LAN) {
    NF_DEBUG("Seen packet from LAN, allocating/rejuvenating flow and sending to WAN");
    struct flow_t id = {
        .src_ip   = hdrs.ipv4_hdr->src_addr,
        .dst_ip   = hdrs.ipv4_hdr->dst_addr,
        .src_port = hdrs.tcpudp_hdr->src_port,
        .dst_port = hdrs.tcpudp_hdr->dst_port,
    };

    flow_manager_allocate_or_refresh_flow(&id, now);

    return WAN;
  } else {
    NF_DEBUG("Seen packet from WAN, checking if flow is known and sending to LAN if so");
    // Inverse the src and dst for the "reply flow"
    struct flow_t id = {
        .src_ip   = hdrs.ipv4_hdr->dst_addr,
        .dst_ip   = hdrs.ipv4_hdr->src_addr,
        .src_port = hdrs.tcpudp_hdr->dst_port,
        .dst_port = hdrs.tcpudp_hdr->src_port,
    };

    if (!flow_manager_get_refresh_flow(&id, now)) {
      NF_DEBUG("Unknown external flow, dropping");
      return DROP;
    }

    return LAN;
  }
}
