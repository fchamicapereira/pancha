#include "ether.h"

bool rte_ether_addr_eq(void *a, void *b) {
  struct rte_ether_addr *id1 = (struct rte_ether_addr *)a;
  struct rte_ether_addr *id2 = (struct rte_ether_addr *)b;

  return (id1->addr_bytes[0] == id2->addr_bytes[0]) &&
         (id1->addr_bytes[1] == id2->addr_bytes[1]) &&
         (id1->addr_bytes[2] == id2->addr_bytes[2]) &&
         (id1->addr_bytes[3] == id2->addr_bytes[3]) &&
         (id1->addr_bytes[4] == id2->addr_bytes[4]) &&
         (id1->addr_bytes[5] == id2->addr_bytes[5]);
}

void rte_ether_addr_allocate(void *obj) {
  struct rte_ether_addr *id = (struct rte_ether_addr *)obj;
  id->addr_bytes[0]         = 0;
  id->addr_bytes[1]         = 0;
  id->addr_bytes[2]         = 0;
  id->addr_bytes[3]         = 0;
  id->addr_bytes[4]         = 0;
  id->addr_bytes[5]         = 0;
}

#ifndef KLEE_VERIFICATION
unsigned rte_ether_addr_hash(void *obj) {
  struct rte_ether_addr *id = (struct rte_ether_addr *)obj;

  uint8_t addr_bytes_0 = id->addr_bytes[0];
  uint8_t addr_bytes_1 = id->addr_bytes[1];
  uint8_t addr_bytes_2 = id->addr_bytes[2];
  uint8_t addr_bytes_3 = id->addr_bytes[3];
  uint8_t addr_bytes_4 = id->addr_bytes[4];
  uint8_t addr_bytes_5 = id->addr_bytes[5];

  unsigned hash = 0;
  hash          = __builtin_ia32_crc32si(hash, addr_bytes_0);
  hash          = __builtin_ia32_crc32si(hash, addr_bytes_1);
  hash          = __builtin_ia32_crc32si(hash, addr_bytes_2);
  hash          = __builtin_ia32_crc32si(hash, addr_bytes_3);
  hash          = __builtin_ia32_crc32si(hash, addr_bytes_4);
  hash          = __builtin_ia32_crc32si(hash, addr_bytes_5);
  return hash;
}
#endif // KLEE_VERIFICATION
