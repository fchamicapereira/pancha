#ifndef _EXPIRATOR_H_INCLUDED_
#define _EXPIRATOR_H_INCLUDED_

#include "../state/double-chain.h"
#include "../state/map.h"
#include "../state/vector.h"

typedef void entry_extract_key(void *entry, void **key);
typedef void entry_pack_key(void *entry, void *key);

// The function takes "coherent" chain allocator vector and hash map,
// and current time.
// It removes items older than time simultaneously from the allocator, vector
// and the map.
// @param chain - DoubleChain index allocator. Items in the allocator are
//                tagged with timestamps.
// @param vector - the Vector of the keys, synchronized with the allocator.
// @param map - Map hash table that keeps mapping of the keys -> indexes,
//               that are synchronized with the allocator.
// @param time - Current number of seconds since the Epoch.
// @returns the number of expired items.
int expire_items_single_map(struct DoubleChain *chain, struct Vector *vector, struct Map *map, time_ns_t time);

// The function takes "coherent" chain vector and hash map,
// and a given number of elements.
// It removes items from 0 to n_elems (inclusive) simultaneously from the vector
// and the map.
int expire_items_single_map_iteratively(struct Vector *vector, struct Map *map, int start, int n_elems);

#endif //_EXPIRATOR_H_INCLUDED_
