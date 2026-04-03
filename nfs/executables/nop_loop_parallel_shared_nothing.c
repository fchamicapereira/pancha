#include "loop_parallel_shared_nothing.h"

#define LAN 0
#define WAN 1

int main(int argc, char **argv) {
  nf_setup(argc, argv);
  worker_loop();
  return 0;
}

bool nf_init(void) { return true; }

int nf_process(uint16_t device, uint8_t *pkt, uint32_t pkt_len, time_ns_t now) {
  if (device == LAN) {
    return WAN;
  } else {
    return LAN;
  }
}
