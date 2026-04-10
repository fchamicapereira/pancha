#pragma once

#include "../lib/libvig.h"

#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>

#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_malloc.h>
#include <rte_mbuf.h>
#include <rte_random.h>
#include <rte_hash_crc.h>
#include <rte_flow.h>
#include <rte_ring.h>

#include <stdio.h>
#include <signal.h>
#include <unistd.h>

#define BATCH_SIZE 32

#define DROP ((uint16_t)-1)
#define FLOOD ((uint16_t)-2)

#define MAX_FLOWS 524288
#define EXPIRATION_TIME_NS 3000000000 // 3 seconds
#define LAN 0
#define WAN 1

static const uint32_t MBUF_CACHE_SIZE = 256;

static const uint16_t RX_QUEUE_SIZE = 1024;
static const uint16_t TX_QUEUE_SIZE = 1024;

// Buffer count for mempools
static const unsigned MEMPOOL_BUFFER_COUNT = 16384;

// Send the given packet to all devices except the packet's own
static void flood(struct rte_mbuf *packet, uint16_t nb_devices) {
  rte_mbuf_refcnt_set(packet, nb_devices - 1);
  int total_sent       = 0;
  uint16_t skip_device = packet->port;
  for (uint16_t device = 0; device < nb_devices; device++) {
    if (device != skip_device) {
      total_sent += rte_eth_tx_burst(device, 0, &packet, 1);
    }
  }

  if (total_sent != nb_devices - 1) {
    rte_mbuf_refcnt_set(packet, 1);
    rte_pktmbuf_free(packet);
  }
}

bool nf_init(void);
int nf_process(uint16_t device, uint8_t *pkt, uint32_t pkt_len, time_ns_t now);
