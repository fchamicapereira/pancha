#pragma once

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <rte_byteorder.h>
#include <rte_common.h>
#include <rte_ethdev.h>
#include <rte_ip.h>
#include <rte_mbuf.h>
#include <rte_tcp.h>
#include <rte_udp.h>

#include "util/packet-io.h"
#include "util/tcpudp_hdr.h"

// rte_ether
struct rte_ether_addr;
struct rte_ether_hdr;

#define IP_MIN_SIZE_WORDS 5
#define WORD_SIZE 4

void nf_log_pkt(struct rte_ether_hdr *rte_ether_header, struct rte_ipv4_hdr *rte_ipv4_header, struct tcpudp_hdr *tcpudp_header);

bool nf_has_ipv4_header(struct rte_ether_hdr *header);
bool nf_has_tcpudp_header(struct rte_ipv4_hdr *header);
bool nf_has_tcp_header(struct rte_ipv4_hdr *header);
bool nf_has_udp_header(struct rte_ipv4_hdr *header);

int nf_set_rte_ipv4_udptcp_checksum(struct rte_ipv4_hdr *ip_header, void *l4_header, void *packet);

uintmax_t nf_util_parse_int(const char *str, const char *name, int base, char next);

struct int_list_t {
  uintmax_t *list;
  size_t n;
};

struct int_list_t nf_util_parse_int_list(const char *str, const char *name, int base, char separator);

char *nf_mac_to_str(struct rte_ether_addr *addr);

char *nf_rte_ipv4_to_str(uint32_t addr);

#define MAX_N_CHUNKS 100
extern void *chunks_borrowed[];
extern size_t chunks_borrowed_num;

static inline void *nf_borrow_next_chunk(uint8_t **p, size_t length) {
  assert(chunks_borrowed_num < MAX_N_CHUNKS);
  void *chunk;
  packet_borrow_next_chunk(*p, length, &chunk);
  chunks_borrowed[chunks_borrowed_num] = chunk;
  chunks_borrowed_num++;
  return chunk;
}

#define CHUNK_LAYOUT_IMPL(pkt, len, fields, n_fields, nests, n_nests, tag) /*nothing*/

#define CHUNK_LAYOUT_N(pkt, str_name, fields, nests)                                                                                                 \
  CHUNK_LAYOUT_IMPL(pkt, sizeof(struct str_name), fields, sizeof(fields) / sizeof(fields[0]), nests, sizeof(nests) / sizeof(nests[0]), #str_name);

#define CHUNK_LAYOUT(pkt, str_name, fields)                                                                                                          \
  CHUNK_LAYOUT_IMPL(pkt, sizeof(struct str_name), fields, sizeof(fields) / sizeof(fields[0]), NULL, 0, #str_name);

static inline void *nf_shrink_chunk(uint8_t **p, size_t length, struct rte_mbuf *mbuf) {
  assert(chunks_borrowed_num < MAX_N_CHUNKS);
  assert(chunks_borrowed_num);
  packet_shrink_chunk((void **)p, length, chunks_borrowed, chunks_borrowed_num, mbuf);
  return chunks_borrowed[chunks_borrowed_num - 1];
}

static inline void *nf_insert_new_chunk(uint8_t **p, size_t length, struct rte_mbuf *mbuf) {
  assert(chunks_borrowed_num < MAX_N_CHUNKS);
  assert(chunks_borrowed_num);

  // Do not really trace the ip options chunk, as it's length
  // is unknown statically
  CHUNK_LAYOUT_IMPL(*p, 1, NULL, 0, NULL, 0, "new_hdr");
  packet_insert_new_chunk((void **)p, length, chunks_borrowed, &chunks_borrowed_num, mbuf);

  return chunks_borrowed[chunks_borrowed_num - 1];
}

static inline void *nf_get_borrowed_chunk(uint32_t chunk_i) {
  assert(chunk_i < chunks_borrowed_num);
  return chunks_borrowed[chunk_i];
}

static inline void nf_return_all_chunks(void *p) {
  while (chunks_borrowed_num != 0) {
    packet_return_chunk(p, chunks_borrowed[chunks_borrowed_num - 1]);
    chunks_borrowed_num--;
  }
}

static inline void nf_return_chunk(uint8_t **p) {
  if (chunks_borrowed_num != 0) {
    packet_return_chunk(*p, chunks_borrowed[chunks_borrowed_num - 1]);
    chunks_borrowed_num--;
  }
}

static inline struct rte_ether_hdr *nf_then_get_ether_header(uint8_t **p) {
  CHUNK_LAYOUT_N(*p, rte_ether_hdr, rte_ether_fields, rte_ether_nested_fields);
  void *hdr = nf_borrow_next_chunk(p, sizeof(struct rte_ether_hdr));
  return (struct rte_ether_hdr *)hdr;
}

static inline struct rte_ipv4_hdr *nf_then_get_ipv4_header(void *rte_ether_header_, uint8_t **p) {
  struct rte_ether_hdr *rte_ether_header = (struct rte_ether_hdr *)rte_ether_header_;

  uint16_t unread_len = packet_get_unread_length(p);
  if ((!nf_has_ipv4_header(rte_ether_header)) | (unread_len < sizeof(struct rte_ipv4_hdr))) {
    return NULL;
  }

  CHUNK_LAYOUT(p, rte_ipv4_hdr, rte_ipv4_fields);
  struct rte_ipv4_hdr *hdr = (struct rte_ipv4_hdr *)nf_borrow_next_chunk(p, sizeof(struct rte_ipv4_hdr));

  return hdr;
}

static inline struct rte_ipv4_hdr *nf_then_get_ipv4_header_with_options(void *rte_ether_header_, uint8_t **p, uint8_t **ip_options) {
  struct rte_ether_hdr *rte_ether_header = (struct rte_ether_hdr *)rte_ether_header_;
  *ip_options                            = NULL;

  uint16_t unread_len = packet_get_unread_length(p);
  if ((!nf_has_ipv4_header(rte_ether_header)) | (unread_len < sizeof(struct rte_ipv4_hdr))) {
    return NULL;
  }

  CHUNK_LAYOUT(p, rte_ipv4_hdr, rte_ipv4_fields);
  struct rte_ipv4_hdr *hdr = (struct rte_ipv4_hdr *)nf_borrow_next_chunk(p, sizeof(struct rte_ipv4_hdr));

  uint8_t ihl = hdr->version_ihl & 0x0f;
  if ((ihl < IP_MIN_SIZE_WORDS) | (unread_len < rte_be_to_cpu_16(hdr->total_length))) {
    return NULL;
  }
  uint16_t ip_options_length = (ihl - IP_MIN_SIZE_WORDS) * WORD_SIZE;
  if ((ip_options_length != 0) & (unread_len - sizeof(struct rte_ipv4_hdr) >= ip_options_length)) {
    // Do not really trace the ip options chunk, as it's length
    // is unknown statically
    CHUNK_LAYOUT_IMPL(*p, 1, NULL, 0, NULL, 0, "ipv4_options");
    *ip_options = (uint8_t *)nf_borrow_next_chunk(p, ip_options_length);
  }
  return hdr;
}

static inline struct tcpudp_hdr *nf_then_get_tcpudp_header(struct rte_ipv4_hdr *ip_header, uint8_t **p) {
  if ((!nf_has_tcpudp_header(ip_header)) | (packet_get_unread_length(p) < sizeof(struct tcpudp_hdr))) {
    return NULL;
  }
  CHUNK_LAYOUT(*p, tcpudp_hdr, tcpudp_fields);
  return (struct tcpudp_hdr *)nf_borrow_next_chunk(p, sizeof(struct tcpudp_hdr));
}

static inline struct rte_tcp_hdr *nf_then_get_tcp_header(struct rte_ipv4_hdr *ip_header, uint8_t **p) {
  if ((!nf_has_tcp_header(ip_header)) | (packet_get_unread_length(p) < sizeof(struct rte_tcp_hdr))) {
    return NULL;
  }
  CHUNK_LAYOUT(*p, rte_tcp_hdr, tcp_fields);
  return (struct rte_tcp_hdr *)nf_borrow_next_chunk(p, sizeof(struct rte_tcp_hdr));
}

static inline struct rte_udp_hdr *nf_then_get_udp_header(struct rte_ipv4_hdr *ip_header, uint8_t **p) {
  if ((!nf_has_udp_header(ip_header)) | (packet_get_unread_length(p) < sizeof(struct rte_udp_hdr))) {
    return NULL;
  }
  CHUNK_LAYOUT(*p, rte_udp_hdr, udp_fields);
  return (struct rte_udp_hdr *)nf_borrow_next_chunk(p, sizeof(struct rte_udp_hdr));
}
