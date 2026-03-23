#include "boilerplate.h"

#define MAX_FLOWS 65536
#define EXPIRATION_TIME_NS 1000000000 // 1 seconds
#define LAN 0
#define WAN 1

int main(int argc, char **argv) {
  nf_setup(argc, argv);
  worker_loop_batched_straw();
  return 0;
}

struct State {
  struct Map *fm;
  struct Vector *fv;
  struct DoubleChain *heap;
};

struct State state;

struct FlowId {
  uint32_t src_ip;
  uint32_t dst_ip;
  uint16_t src_port;
  uint16_t dst_port;
};

bool nf_init(void) {
  if (map_allocate(MAX_FLOWS, sizeof(struct FlowId), &(state.fm)) == 0) {
    return false;
  }

  if (vector_allocate(sizeof(struct FlowId), MAX_FLOWS, &(state.fv)) == 0) {
    return false;
  }

  if (dchain_allocate(MAX_FLOWS, &(state.heap)) == 0) {
    return false;
  }

  return true;
}

void flow_manager_expire(time_ns_t time) {
  assert(time >= 0); // we don't support the past
  assert(sizeof(time_ns_t) <= sizeof(uint64_t));
  uint64_t time_u     = (uint64_t)time; // OK because of the two asserts
  time_ns_t last_time = time_u - 1000000000;
  expire_items_single_map(state.heap, state.fv, state.fm, last_time);
}

void flow_manager_allocate_or_refresh_flow(struct FlowId *id, time_ns_t time) {
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

  struct FlowId *key = 0;
  vector_borrow(state.fv, index, (void **)&key);
  memcpy((void *)key, (void *)id, sizeof(struct FlowId));
  map_put(state.fm, key, index);
  vector_return(state.fv, index, key);
}

bool flow_manager_get_refresh_flow(struct FlowId *id, time_ns_t time) {
  int index;
  if (map_get(state.fm, id, &index) == 0) {
    return false;
  }

  dchain_rejuvenate_index(state.heap, index, time);
  return true;
}

int nf_process(uint16_t device, uint8_t **buffer, uint16_t packet_length, time_ns_t now, struct rte_mbuf *mbuf) {
  flow_manager_expire(now);

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

  if (device == LAN) {
    NF_DEBUG("Seen packet from LAN, allocating/rejuvenating flow and sending to WAN");
    struct FlowId id = {
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
    struct FlowId id = {
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
