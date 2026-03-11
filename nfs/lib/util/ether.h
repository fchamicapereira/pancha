#ifndef _ETHER_ADDR_H_INCLUDED_
#define _ETHER_ADDR_H_INCLUDED_

#include <stdbool.h>
#include <rte_ether.h>

#define DEFAULT_RTE_ETHER_ADDR rte_ether_addrc(0, 0, 0, 0, 0, 0)

unsigned rte_ether_addr_hash(void *obj);
bool rte_ether_addr_eq(void *a, void *b);
void rte_ether_addr_allocate(void *obj);

#define LOG_ETHER_ADDR(obj, p)                                                 \
  p("{");                                                                      \
  p("addr_bytes[0]: %d", (obj)->addr_bytes[0]);                                \
  p("addr_bytes[1]: %d", (obj)->addr_bytes[1]);                                \
  p("addr_bytes[2]: %d", (obj)->addr_bytes[2]);                                \
  p("addr_bytes[3]: %d", (obj)->addr_bytes[3]);                                \
  p("addr_bytes[4]: %d", (obj)->addr_bytes[4]);                                \
  p("addr_bytes[5]: %d", (obj)->addr_bytes[5]);                                \
  p("}");

#endif //_ETHER_ADDR_H_INCLUDED_
