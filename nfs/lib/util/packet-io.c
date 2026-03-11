#include <stdlib.h>
#include <assert.h>
#include <rte_ip.h>
#include <rte_mbuf.h>
#include <rte_memcpy.h>

#include "packet-io.h"

size_t global_total_length;
size_t global_read_length = 0;

void packet_state_total_length(void *p, uint32_t *len) { global_total_length = *len; }

// The main IO primitive.
void packet_borrow_next_chunk(void *p, size_t length, void **chunk) {
  // TODO: support mbuf chains.
  *chunk = (char *)p + global_read_length;
  global_read_length += length;
}

void packet_return_chunk(void *p, void *chunk) { global_read_length = (uint32_t)((int8_t *)chunk - (int8_t *)p); }

void packet_shrink_chunk(void **p, size_t length, void **chunks, size_t num_chunks, struct rte_mbuf *mbuf) {
  uint8_t *data = (uint8_t *)(*p);

  void *last_chunk          = chunks[num_chunks - 1];
  size_t last_chunk_length  = packet_get_chunk_length(data, last_chunk);
  uint8_t *last_chunk_limit = (uint8_t *)last_chunk + last_chunk_length;

  assert(length <= last_chunk_length);
  size_t offset = last_chunk_length - length;

  if (offset == 0) {
    return;
  }

  uint8_t *current = last_chunk_limit - 1;

  while (current >= data + offset) {
    rte_memcpy(current, current - offset, 1);
    current--;
  }

  data = (uint8_t *)rte_pktmbuf_adj(mbuf, offset);
  assert(data);

  global_read_length -= offset;
  global_total_length -= offset;

  for (int i = 0; i < num_chunks; i++) {
    ((uint8_t **)chunks)[i] += offset;
  }

  (*p) = data;
}

void packet_insert_new_chunk(void **p, size_t length, void **chunks, size_t *num_chunks, struct rte_mbuf *mbuf) {
  uint8_t *data             = (uint8_t *)(*p);
  uint8_t *last_chunk_limit = data + global_read_length;

  data = (uint8_t *)rte_pktmbuf_prepend(mbuf, length);
  assert(data);

  uint8_t *current = data;

  while (current + length < last_chunk_limit) {
    rte_memcpy(current, current + length, 1);
    current++;
  }

  for (int i = 0; i < (*num_chunks); i++) {
    ((uint8_t **)chunks)[i] -= length;
  }

  assert(chunks[0] == data);

  (*num_chunks)++;
  (*p) = data;

  chunks[(*num_chunks) - 1] = data + global_read_length;

  global_read_length += length;
  global_total_length += length;
}

uint32_t packet_get_unread_length(void *p) { return global_total_length - global_read_length; }

size_t packet_get_chunk_length(void *p, void *chunk) { return (uint32_t)(((char *)p + global_read_length) - (char *)chunk); }