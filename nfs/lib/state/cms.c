#include "cms.h"

#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

#include "vector.h"
#include "../util/time.h"
#include "../util/hash.h"
#include "../util/compute.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))

struct CMS {
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

struct cms_bucket {
  uint64_t value;
};

int cms_allocate(uint32_t height, uint32_t width, uint32_t key_size,
                 time_ns_t periodic_cleanup_interval, struct CMS **cms_out) {
  assert(height > 0);
  assert(width > 0);
  assert(height < CMS_MAX_SALTS_BANK_SIZE);

  struct CMS *cms_alloc = (struct CMS *)malloc(sizeof(struct CMS));
  if (cms_alloc == NULL) {
    return 0;
  }

  (*cms_out) = cms_alloc;

  (*cms_out)->height           = height;
  (*cms_out)->width            = width;
  (*cms_out)->key_size         = key_size;
  (*cms_out)->cleanup_interval = periodic_cleanup_interval;

  (*cms_out)->last_cleanup = 0;

  uint32_t capacity = ensure_power_of_two(height * width);

  (*cms_out)->buckets = NULL;
  if (vector_allocate(sizeof(struct cms_bucket), capacity,
                      &((*cms_out)->buckets)) == 0) {
    return 0;
  }

  return 1;
}

void cms_increment(struct CMS *cms, void *key) {
  for (uint32_t h = 0; h < cms->height; h++) {
    unsigned hash =
        __builtin_ia32_crc32si(CMS_SALTS[h], hash_obj(key, cms->key_size));
    uint32_t offset = h * cms->width + (hash % cms->width);

    struct cms_bucket *bucket = 0;
    vector_borrow(cms->buckets, offset, (void **)&bucket);
    bucket->value++;
    vector_return(cms->buckets, offset, bucket);
  }
}

int cms_count_min(struct CMS *cms, void *key) {
  int min_val = INT32_MAX;

  for (uint32_t h = 0; h < cms->height; h++) {
    unsigned hash =
        __builtin_ia32_crc32si(CMS_SALTS[h], hash_obj(key, cms->key_size));
    uint32_t offset = h * cms->width + (hash % cms->width);

    struct cms_bucket *bucket = 0;
    vector_borrow(cms->buckets, offset, (void **)&bucket);
    min_val = MIN(min_val, bucket->value);
    vector_return(cms->buckets, offset, bucket);
  }

  return min_val;
}

int cms_periodic_cleanup(struct CMS *cms, time_ns_t now) {
  if (cms->last_cleanup == 0) {
    cms->last_cleanup = now;
    return 0;
  }

  if (now - cms->last_cleanup < cms->cleanup_interval) {
    return 0;
  }

  vector_clear(cms->buckets);
  cms->last_cleanup = now;

  return 1;
}
