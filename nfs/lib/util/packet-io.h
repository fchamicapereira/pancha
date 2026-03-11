#ifndef _PACKET_IO_H_INCLUDED_
#define _PACKET_IO_H_INCLUDED_

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <rte_ether.h> //for sizeof(struct rte_ether_hdr)

struct rte_mempool;
struct rte_mbuf;

// The main IO primitive.
void packet_borrow_next_chunk(void *p, size_t length, void **chunk);
void packet_return_chunk(void *p, void *chunk);
uint32_t packet_get_unread_length(void *p);
void packet_state_total_length(void *p, uint32_t *len);
void packet_shrink_chunk(void **p, size_t length, void **chunks, size_t num_chunks, struct rte_mbuf *mbuf);
void packet_insert_new_chunk(void **p, size_t length, void **chunks, size_t *num_chunks, struct rte_mbuf *mbuf);
size_t packet_get_chunk_length(void *p, void *chunk);
bool packet_receive(uint16_t src_device, void **p, uint32_t *len);
void packet_send(void *p, uint16_t dst_device);
void packet_free(void *p);
void packet_broadcast(void *p, uint16_t src_device);

#endif // _PACKET_IO_H_INCLUDED_
