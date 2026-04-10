#include "loop_batch_straw.h"

#define SKETCH_WIDTH 65536
#define SKETCH_HEIGHT 7
#define MAX_CLIENTS 65536

int main(int argc, char **argv) {
  nf_setup(argc, argv);
  worker_loop();
  return 0;
}

struct state_t {
  struct CMS *flow_counter_5tuple;
  struct CMS *flow_counter_ips;
  struct CMS *flow_counter_ports;
};

struct state_t state;

struct flow_5tuple_t {
  uint32_t src_ip;
  uint32_t dst_ip;
  uint16_t src_port;
  uint16_t dst_port;
};

struct flow_ips_t {
  uint32_t src_ip;
  uint32_t dst_ip;
};

struct flow_ports_t {
  uint16_t src_port;
  uint16_t dst_port;
};

bool nf_init(void) {
  if (cms_allocate(SKETCH_HEIGHT, SKETCH_WIDTH, sizeof(struct flow_5tuple_t), EXPIRATION_TIME_NS,
                   &(state.flow_counter_5tuple)) == 0) {
    return false;
  }

  if (cms_allocate(SKETCH_HEIGHT, SKETCH_WIDTH, sizeof(struct flow_ips_t), EXPIRATION_TIME_NS,
                   &(state.flow_counter_ips)) == 0) {
    return false;
  }

  if (cms_allocate(SKETCH_HEIGHT, SKETCH_WIDTH, sizeof(struct flow_ports_t), EXPIRATION_TIME_NS,
                   &(state.flow_counter_ports)) == 0) {
    return false;
  }

  return true;
}

void expire_entries(time_ns_t now) {
  cms_periodic_cleanup(state.flow_counter_5tuple, now);
  cms_periodic_cleanup(state.flow_counter_ips, now);
  cms_periodic_cleanup(state.flow_counter_ports, now);
}

int nf_process(uint16_t device, uint8_t *pkt, uint32_t pkt_len, time_ns_t now) {
  expire_entries(now);

  struct tcpudp_hdrs_t hdrs = nf_get_tcpudp_hdrs(pkt, pkt_len);

  if (hdrs.ipv4_hdr == NULL) {
    NF_DEBUG("Not IPv4, dropping");
    return DROP;
  }

  if (hdrs.tcpudp_hdr == NULL) {
    NF_DEBUG("Not TCP/UDP, dropping");
    return DROP;
  }

  struct flow_5tuple_t flow_5tuple = {
      .src_ip   = hdrs.ipv4_hdr->src_addr,
      .dst_ip   = hdrs.ipv4_hdr->dst_addr,
      .src_port = hdrs.tcpudp_hdr->src_port,
      .dst_port = hdrs.tcpudp_hdr->dst_port,
  };

  struct flow_ips_t flow_ips = {
      .src_ip = hdrs.ipv4_hdr->src_addr,
      .dst_ip = hdrs.ipv4_hdr->dst_addr,
  };

  struct flow_ports_t flow_ports = {
      .src_port = hdrs.tcpudp_hdr->src_port,
      .dst_port = hdrs.tcpudp_hdr->dst_port,
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
