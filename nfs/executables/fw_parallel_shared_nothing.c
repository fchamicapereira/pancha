#define TRACK_STRIDE_SIZES 0

#include "loop_parallel_shared_nothing.h"

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
  expire_items_single_map(state->heap, state->fv, state->fm, time - EXPIRATION_TIME_NS);
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
    NF_INFO("Not IPv4, dropping");
    return DROP;
  }

  struct rte_ipv4_hdr *rte_ipv4_header = (struct rte_ipv4_hdr *)(rte_ether_header + 1);

  if (rte_ipv4_header->next_proto_id != IPPROTO_TCP && rte_ipv4_header->next_proto_id != IPPROTO_UDP) {
    NF_INFO("Not TCP or UDP, dropping");
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
      NF_INFO("Unknown external flow, dropping");
      return DROP;
    }

    return LAN;
  }
}
