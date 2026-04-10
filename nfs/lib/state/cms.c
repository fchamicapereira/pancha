#include "cms.h"

#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

#include "vector.h"
#include "../util/time.h"
#include "../util/hash.h"
#include "../util/compute.h"

#include <rte_malloc.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define CMS_MAX_SALTS_BANK_SIZE 64

uint32_t CMS_SALTS[CMS_MAX_SALTS_BANK_SIZE] = {
    0x9b78350f, 0x9bcf144c, 0x8ab29a3e, 0x34d48bf5, 0x78e47449, 0xd6e4af1d, 0x32ed75e2, 0xb1eb5a08,
    0x9cc7fbdf, 0x65b811ea, 0x41fd5ed9, 0x2e6a6782, 0x3549661d, 0xbb211240, 0x78daa2ae, 0x8ce2d11f,
    0x52911493, 0xc2497bd5, 0x83c232dd, 0x3e413e9f, 0x8831d191, 0x6770ac67, 0xcd1c9141, 0xad35861a,
    0xb79cd83d, 0xce3ec91f, 0x360942d1, 0x905000fa, 0x28bb469a, 0xdb239a17, 0x615cf3ae, 0xec9f7807,
    0x271dcc3c, 0x47b98e44, 0x33ff4a71, 0x02a063f8, 0xb051ebf2, 0x6f938d98, 0x2279abc3, 0xd55b01db,
    0xaa99e301, 0x95d0587c, 0xaee8684e, 0x24574971, 0x4b1e79a6, 0x4a646938, 0xa68d67f4, 0xb87839e6,
    0x8e3d388b, 0xed2af964, 0x541b83e3, 0xcb7fc8da, 0xe1140f8c, 0xe9724fd6, 0x616a78fa, 0x610cd51c,
    0x10f9173e, 0x8e180857, 0xa8f0b843, 0xd429a973, 0xceee91e5, 0x1d4c6b18, 0x2a80e6df, 0x396f4d23,
};

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

int cms_allocate(uint32_t height, uint32_t width, uint32_t key_size, time_ns_t periodic_cleanup_interval,
                 struct CMS **cms_out) {
  assert(height > 0);
  assert(width > 0);
  assert(height < CMS_MAX_SALTS_BANK_SIZE);

  struct CMS *cms_alloc = (struct CMS *)rte_malloc("struct CMS", sizeof(struct CMS), 64);
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
  if (vector_allocate(sizeof(struct cms_bucket), capacity, &((*cms_out)->buckets)) == 0) {
    rte_free(cms_alloc);
    return 0;
  }

  return 1;
}

void cms_increment(struct CMS *cms, void *key) {
  for (uint32_t h = 0; h < cms->height; h++) {
    unsigned hash   = __builtin_ia32_crc32si(CMS_SALTS[h], hash_obj(key, cms->key_size));
    uint32_t offset = h * cms->width + (hash % cms->width);

    struct cms_bucket *bucket = 0;
    vector_borrow(cms->buckets, offset, (void **)&bucket);
    bucket->value++;
    vector_return(cms->buckets, offset, bucket);
  }
}

uint64_t cms_count_min(struct CMS *cms, void *key) {
  uint64_t min_val = INT64_MAX;

  for (uint32_t h = 0; h < cms->height; h++) {
    unsigned hash   = __builtin_ia32_crc32si(CMS_SALTS[h], hash_obj(key, cms->key_size));
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
