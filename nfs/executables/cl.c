#include "boilerplate.h"

#define MAX_FLOWS 65536
#define EXPIRATION_TIME_NS 1000000000 // 1 seconds
#define SKETCH_WIDTH 65536
#define SKETCH_HEIGHT 5
#define MAX_CLIENTS 65536

// These two are swapped compared to the other NFs, this NF only does useful work on the WAN size.
#define LAN 1
#define WAN 0

int main(int argc, char **argv) {
  nf_setup(argc, argv);
  worker_loop();
  return 0;
}

struct State {
  struct Map *flows;
  struct Vector *flows_keys;
  struct DoubleChain *flow_allocator;
  struct CMS *cms;
};

struct State state;

struct flow {
  uint32_t src_ip;
  uint32_t dst_ip;
  uint16_t src_port;
  uint16_t dst_port;
};

struct client {
  uint32_t src_ip;
  uint32_t dst_ip;
};

bool nf_init(void) {
  if (map_allocate(MAX_FLOWS, sizeof(struct flow), &(state.flows)) == 0) {
    return false;
  }

  if (vector_allocate(sizeof(struct flow), MAX_FLOWS, &(state.flows_keys)) == 0) {
    return false;
  }

  if (dchain_allocate(MAX_FLOWS, &(state.flow_allocator)) == 0) {
    return false;
  }

  if (cms_allocate(SKETCH_HEIGHT, SKETCH_WIDTH, sizeof(struct client), EXPIRATION_TIME_NS, &(state.cms)) == 0) {
    return false;
  }

  return true;
}

void expire_entries(time_ns_t now) {
  assert(now >= 0); // we don't support the past
  assert(sizeof(time_ns_t) <= sizeof(uint64_t));
  uint64_t time_u     = (uint64_t)now; // OK because of the two asserts
  time_ns_t last_time = time_u - EXPIRATION_TIME_NS;
  expire_items_single_map(state.flow_allocator, state.flows_keys, state.flows, last_time);
  cms_periodic_cleanup(state.cms, now);
}

int allocate_flow(struct flow *flow, time_ns_t time) {
  int flow_index = -1;

  int allocated = dchain_allocate_new_index(state.flow_allocator, &flow_index, time);

  if (!allocated) {
    // Nothing we can do...
    NF_DEBUG("No more space in the flow table");
    return false;
  }

  NF_DEBUG("Allocating %u.%u.%u.%u:%u => %u.%u.%u.%u:%u", (flow->src_ip >> 0) & 0xff, (flow->src_ip >> 8) & 0xff,
           (flow->src_ip >> 16) & 0xff, (flow->src_ip >> 24) & 0xff, flow->src_port, (flow->dst_ip >> 0) & 0xff,
           (flow->dst_ip >> 8) & 0xff, (flow->dst_ip >> 16) & 0xff, (flow->dst_ip >> 24) & 0xff, flow->dst_port);

  struct flow *new_flow = NULL;
  vector_borrow(state.flows_keys, flow_index, (void **)&new_flow);
  memcpy((void *)new_flow, (void *)flow, sizeof(struct flow));
  map_put(state.flows, new_flow, flow_index);
  vector_return(state.flows_keys, flow_index, new_flow);

  return true;
}

// Return false if packet should be dropped
int limit_clients(struct flow *flow, time_ns_t now) {
  int flow_index = -1;
  int present    = map_get(state.flows, flow, &flow_index);

  struct client client = {
      .src_ip = flow->src_ip,
      .dst_ip = flow->dst_ip,
  };

  if (present) {
    dchain_rejuvenate_index(state.flow_allocator, flow_index, now);
    return 1;
  }

  cms_increment(state.cms, &client);
  int count = cms_count_min(state.cms, &client);

  if (count > MAX_CLIENTS) {
    return 0;
  }

  int allocated_flow = allocate_flow(flow, now);

  if (!allocated_flow) {
    // Reached the maximum number of allowed flows.
    // Just forward and don't limit...
    return 1;
  }

  return 1;
}

int nf_process(uint16_t device, uint8_t **buffer, uint16_t packet_length, time_ns_t now, struct rte_mbuf *mbuf) {
  expire_entries(now);

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
    // Simply forward outgoing packets.
    NF_DEBUG("Outgoing packet. Not limiting clients.");
    return WAN;
  } else {
    struct flow flow = {
        .src_ip   = rte_ipv4_header->src_addr,
        .dst_ip   = rte_ipv4_header->dst_addr,
        .src_port = tcpudp_header->src_port,
        .dst_port = tcpudp_header->dst_port,
    };

    if (limit_clients(&flow, now) == 0) {
      // Drop packet.
      NF_DEBUG("Limiting   %u.%u.%u.%u:%u => %u.%u.%u.%u:%u", (flow.src_ip >> 0) & 0xff, (flow.src_ip >> 8) & 0xff,
               (flow.src_ip >> 16) & 0xff, (flow.src_ip >> 24) & 0xff, flow.src_port, (flow.dst_ip >> 0) & 0xff,
               (flow.dst_ip >> 8) & 0xff, (flow.dst_ip >> 16) & 0xff, (flow.dst_ip >> 24) & 0xff, flow.dst_port);
      return DROP;
    }
    return LAN;
  }
}
