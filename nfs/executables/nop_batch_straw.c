#include "boilerplate.h"

#define LAN 0
#define WAN 1

int main(int argc, char **argv) {
  nf_setup(argc, argv);
  worker_loop_batched_straw();
  return 0;
}

bool nf_init(void) { return true; }

int nf_process(uint16_t device, uint8_t **buffer, uint16_t packet_length, time_ns_t now, struct rte_mbuf *mbuf) {
  if (device == LAN) {
    return WAN;
  } else {
    return LAN;
  }
}
