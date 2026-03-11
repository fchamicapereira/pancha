#include "bloom-filter.h"

#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

#include "vector.h"
#include "../util/time.h"
#include "../util/hash.h"
#include "../util/compute.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))

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

int bf_allocate(uint32_t height, uint32_t width, uint32_t key_size,
                time_ns_t periodic_cleanup_interval,
                struct BloomFilter **bf_out) {
  assert(height > 0);
  assert(width > 0);
  assert(height < BF_MAX_SALTS_BANK_SIZE);

  struct BloomFilter *bf_alloc =
      (struct BloomFilter *)malloc(sizeof(struct BloomFilter));
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
  if (vector_allocate(sizeof(struct bf_bucket), capacity,
                      &((*bf_out)->buckets)) == 0) {
    return 0;
  }

  return 1;
}

void bf_set(struct BloomFilter *bf, void *key) {
  for (uint32_t h = 0; h < bf->height; h++) {
    unsigned hash =
        __builtin_ia32_crc32si(BF_SALTS[h], hash_obj(key, bf->key_size));
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
    unsigned hash =
        __builtin_ia32_crc32si(BF_SALTS[h], hash_obj(key, bf->key_size));
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
