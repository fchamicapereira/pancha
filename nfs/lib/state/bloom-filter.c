#include "bloom-filter.h"

#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

#include "vector.h"
#include "../util/time.h"
#include "../util/hash.h"
#include "../util/compute.h"

#include <rte_malloc.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define BF_MAX_SALTS_BANK_SIZE 64

const uint32_t BF_SALTS[BF_MAX_SALTS_BANK_SIZE] = {
    0x9b78350f, 0x9bcf144c, 0x8ab29a3e, 0x34d48bf5, 0x78e47449, 0xd6e4af1d, 0x32ed75e2, 0xb1eb5a08,
    0x9cc7fbdf, 0x65b811ea, 0x41fd5ed9, 0x2e6a6782, 0x3549661d, 0xbb211240, 0x78daa2ae, 0x8ce2d11f,
    0x52911493, 0xc2497bd5, 0x83c232dd, 0x3e413e9f, 0x8831d191, 0x6770ac67, 0xcd1c9141, 0xad35861a,
    0xb79cd83d, 0xce3ec91f, 0x360942d1, 0x905000fa, 0x28bb469a, 0xdb239a17, 0x615cf3ae, 0xec9f7807,
    0x271dcc3c, 0x47b98e44, 0x33ff4a71, 0x02a063f8, 0xb051ebf2, 0x6f938d98, 0x2279abc3, 0xd55b01db,
    0xaa99e301, 0x95d0587c, 0xaee8684e, 0x24574971, 0x4b1e79a6, 0x4a646938, 0xa68d67f4, 0xb87839e6,
    0x8e3d388b, 0xed2af964, 0x541b83e3, 0xcb7fc8da, 0xe1140f8c, 0xe9724fd6, 0x616a78fa, 0x610cd51c,
    0x10f9173e, 0x8e180857, 0xa8f0b843, 0xd429a973, 0xceee91e5, 0x1d4c6b18, 0x2a80e6df, 0x396f4d23,
};

struct BloomFilter {
  struct Vector *buckets;

  uint32_t height;
  uint32_t width;
  uint32_t key_size;
  time_ns_t cleanup_interval;

  time_ns_t last_cleanup;
};

struct hash {
  uint32_t value;
};

struct bf_bucket {
  uint8_t value;
};

int bf_allocate(uint32_t height, uint32_t width, uint32_t key_size, time_ns_t periodic_cleanup_interval,
                struct BloomFilter **bf_out) {
  assert(height > 0);
  assert(width > 0);
  assert(height < BF_MAX_SALTS_BANK_SIZE);

  struct BloomFilter *bf_alloc = (struct BloomFilter *)rte_malloc("struct BloomFilter", sizeof(struct BloomFilter), 64);
  if (bf_alloc == NULL) {
    return 0;
  }

  (*bf_out) = bf_alloc;

  (*bf_out)->height           = height;
  (*bf_out)->width            = width;
  (*bf_out)->key_size         = key_size;
  (*bf_out)->cleanup_interval = periodic_cleanup_interval;

  (*bf_out)->last_cleanup = 0;

  uint32_t capacity = ensure_power_of_two(height * width);

  (*bf_out)->buckets = NULL;
  if (vector_allocate(sizeof(struct bf_bucket), capacity, &((*bf_out)->buckets)) == 0) {
    rte_free(bf_alloc);
    return 0;
  }

  return 1;
}

void bf_set(struct BloomFilter *bf, void *key) {
  for (uint32_t h = 0; h < bf->height; h++) {
    unsigned hash   = __builtin_ia32_crc32si(BF_SALTS[h], hash_obj(key, bf->key_size));
    uint32_t offset = h * bf->width + (hash % bf->width);

    struct bf_bucket *bucket = 0;
    vector_borrow(bf->buckets, offset, (void **)&bucket);
    bucket->value = 1;
    vector_return(bf->buckets, offset, bucket);
  }
}

int bf_query(struct BloomFilter *bf, void *key) {
  uint32_t count = 0;

  for (uint32_t h = 0; h < bf->height; h++) {
    unsigned hash   = __builtin_ia32_crc32si(BF_SALTS[h], hash_obj(key, bf->key_size));
    uint32_t offset = h * bf->width + (hash % bf->width);

    struct bf_bucket *bucket = 0;
    vector_borrow(bf->buckets, offset, (void **)&bucket);
    count += bucket->value;
    vector_return(bf->buckets, offset, bucket);
  }

  if (count == bf->height) {
    return 1;
  }

  return 0;
}

int bf_periodic_cleanup(struct BloomFilter *bf, time_ns_t now) {
  if (bf->last_cleanup == 0) {
    bf->last_cleanup = now;
    return 0;
  }

  if (now - bf->last_cleanup < bf->cleanup_interval) {
    return 0;
  }

  vector_clear(bf->buckets);
  bf->last_cleanup = now;

  return 1;
}
