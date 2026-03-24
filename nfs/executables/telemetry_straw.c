#include "boilerplate.h"

#define MAX_FLOWS 65536
#define EXPIRATION_TIME_NS 1000000000 // 1 seconds
#define SKETCH_WIDTH 65536
#define SKETCH_HEIGHT 7
#define MAX_CLIENTS 65536

#define LAN 0
#define WAN 1

int main(int argc, char **argv) {
  nf_setup(argc, argv);
  worker_loop_batched_straw();
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
