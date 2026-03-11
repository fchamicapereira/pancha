#include "map-impl-pow2.h"

#include <string.h>
#include <stdint.h>

static int keq(void *key1, void *key2, unsigned keys_size) { return memcmp(key1, key2, keys_size) == 0; }

static unsigned loop(unsigned k, unsigned capacity) { return k & (capacity - 1); }

static int find_key(int *busybits, void **keyps, unsigned *k_hashes, int *chns, void *keyp, unsigned key_size, unsigned key_hash,
                    unsigned capacity) {
  unsigned start = loop(key_hash, capacity);
  unsigned i     = 0;
  for (; i < capacity; ++i) {
    unsigned index = loop(start + i, capacity);
    int bb         = busybits[index];
    unsigned kh    = k_hashes[index];
    int chn        = chns[index];
    void *kp       = keyps[index];
    if (bb != 0 && kh == key_hash) {
      if (keq(kp, keyp, key_size)) {
        return (int)index;
      }
    } else {
      if (chn == 0) {
        return -1;
      }
    }
  }
  return -1;
}

static unsigned find_key_remove_chain(int *busybits, void **keyps, unsigned *k_hashes, int *chns, void *keyp, unsigned key_size,
                                      unsigned key_hash, unsigned capacity, void **keyp_out) {
  unsigned i     = 0;
  unsigned start = loop(key_hash, capacity);

  for (; i < capacity; ++i) {
    unsigned index = loop(start + i, capacity);
    int bb         = busybits[index];
    unsigned kh    = k_hashes[index];
    int chn        = chns[index];
    void *kp       = keyps[index];
    if (bb != 0 && kh == key_hash) {
      if (keq(kp, keyp, key_size)) {
        busybits[index] = 0;
        *keyp_out       = keyps[index];
        return index;
      }
    }
    chns[index] = chn - 1;
  }
  return -1;
}

static unsigned find_empty(int *busybits, int *chns, unsigned start, unsigned capacity) {
  unsigned i = 0;
  for (; i < capacity; ++i) {
    unsigned index = loop(start + i, capacity);
    int bb         = busybits[index];
    if (0 == bb) {
      return index;
    }
    int chn     = chns[index];
    chns[index] = chn + 1;
  }
  return -1;
}

void map_impl_init(int *busybits, unsigned key_size, void **keyps, unsigned *khs, int *chns, int *vals, unsigned capacity) {
  (uintptr_t) keyps;
  (uintptr_t) khs;
  (uintptr_t) vals;

  unsigned i = 0;
  for (; i < capacity; ++i) {
    busybits[i] = 0;
    chns[i]     = 0;
  }
}

int map_impl_get(int *busybits, void **keyps, unsigned *k_hashes, int *chns, int *values, void *keyp, unsigned key_size, unsigned hash,
                 int *value, unsigned capacity) {
  int index = find_key(busybits, keyps, k_hashes, chns, keyp, key_size, hash, capacity);

  if (-1 == index) {
    return 0;
  }
  *value = values[index];
  return 1;
}

void map_impl_put(int *busybits, void **keyps, unsigned *k_hashes, int *chns, int *values, void *keyp, unsigned hash, int value,
                  unsigned capacity) {
  unsigned start = loop(hash, capacity);
  unsigned index = find_empty(busybits, chns, start, capacity);

  busybits[index] = 1;
  keyps[index]    = keyp;
  k_hashes[index] = hash;
  values[index]   = value;
}

void map_impl_erase(int *busybits, void **keyps, unsigned *k_hashes, int *chns, void *keyp, unsigned key_size, unsigned hash,
                    unsigned capacity, void **keyp_out) {
  find_key_remove_chain(busybits, keyps, k_hashes, chns, keyp, key_size, hash, capacity, keyp_out);
}

unsigned map_impl_size(int *busybits, unsigned capacity) {
  unsigned s = 0;
  unsigned i = 0;
  for (; i < capacity; ++i) {
    if (busybits[i] != 0) {
      ++s;
    }
  }
  return s;
}
