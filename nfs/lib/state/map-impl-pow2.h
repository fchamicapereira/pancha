#ifndef _MAP_IMPL_H_INCLUDED_
#define _MAP_IMPL_H_INCLUDED_

#include "map-util.h"

#include <stdbool.h>

// Values and keys are void*, and the actual keys and values should be managed
// by the client application.
// I could not use integer keys, because need to operate with keys like
// int_key/ext_key that are much bigger than a 32bit integer.
void map_impl_init(int *busybits, unsigned key_size, void **keyps, unsigned *khs, int *chns, int *vals, unsigned capacity);

int map_impl_get(int *busybits, void **keyps, unsigned *k_hashes, int *chns, int *values, void *keyp, unsigned key_size, unsigned hash,
                 int *value, unsigned capacity);

void map_impl_put(int *busybits, void **keyps, unsigned *k_hashes, int *chns, int *values, void *keyp, unsigned hash, int value,
                  unsigned capacity);

// TODO: Keep track of the key pointers, in order to preserve the pointer value
//  when releasing it with map_impl_erase.
void map_impl_erase(int *busybits, void **keyps, unsigned *key_hashes, int *chns, void *keyp, unsigned key_size, unsigned hash,
                    unsigned capacity, void **keyp_out);

unsigned map_impl_size(int *busybits, unsigned capacity);

#endif //_MAP_IMPL_H_INCLUDED_
