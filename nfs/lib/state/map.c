#include "map.h"
#include "map-impl-pow2.h"
#include "../util/compute.h"

#include <rte_malloc.h>

#include <stdlib.h>
#include <stddef.h>

struct Map {
  int *busybits;
  void **keyps;
  unsigned *khs;
  int *chns;
  int *vals;
  unsigned capacity;
  unsigned size;
  unsigned key_size;
};

int map_allocate(unsigned capacity, unsigned key_size, struct Map **map_out) {
  // Check that capacity is a power of 2
  if (capacity == 0 || is_power_of_two(capacity) == 0) {
    return 0;
  }

  struct Map *old_map_val = *map_out;
  struct Map *map_alloc   = (struct Map *)rte_malloc("struct Map", sizeof(struct Map), 64);
  if (map_alloc == NULL)
    return 0;
  *map_out       = (struct Map *)map_alloc;
  int *bbs_alloc = (int *)rte_malloc("int", sizeof(int) * (int)capacity, 64);
  if (bbs_alloc == NULL) {
    rte_free(map_alloc);
    *map_out = old_map_val;
    return 0;
  }
  (*map_out)->busybits = bbs_alloc;
  void **keyps_alloc   = (void **)rte_malloc("void **", sizeof(void *) * (int)capacity, 64);
  if (keyps_alloc == NULL) {
    rte_free(bbs_alloc);
    rte_free(map_alloc);
    *map_out = old_map_val;
    return 0;
  }
  (*map_out)->keyps   = keyps_alloc;
  unsigned *khs_alloc = (unsigned *)rte_malloc("unsigned", sizeof(unsigned) * (int)capacity, 64);
  if (khs_alloc == NULL) {
    rte_free(keyps_alloc);
    rte_free(bbs_alloc);
    rte_free(map_alloc);
    *map_out = old_map_val;
    return 0;
  }
  (*map_out)->khs = khs_alloc;
  int *chns_alloc = (int *)rte_malloc("int", sizeof(int) * (int)capacity, 64);
  if (chns_alloc == NULL) {
    rte_free(khs_alloc);
    rte_free(keyps_alloc);
    rte_free(bbs_alloc);
    rte_free(map_alloc);
    *map_out = old_map_val;
    return 0;
  }
  (*map_out)->chns = chns_alloc;
  int *vals_alloc  = (int *)rte_malloc("int", sizeof(int) * (int)capacity, 64);
  if (vals_alloc == NULL) {
    rte_free(chns_alloc);
    rte_free(khs_alloc);
    rte_free(keyps_alloc);
    rte_free(bbs_alloc);
    rte_free(map_alloc);
    *map_out = old_map_val;
    return 0;
  }
  (*map_out)->vals     = vals_alloc;
  (*map_out)->capacity = capacity;
  (*map_out)->size     = 0;
  (*map_out)->key_size = key_size;

  map_impl_init((*map_out)->busybits, (*map_out)->key_size, (*map_out)->keyps, (*map_out)->khs, (*map_out)->chns,
                (*map_out)->vals, capacity);

  return 1;
}

static unsigned khash(void *key, unsigned key_size) {
  unsigned hash = 0;
  while (key_size > 0) {
    if (key_size >= sizeof(unsigned int)) {
      hash = __builtin_ia32_crc32si(hash, *(unsigned int *)key);
      key  = (unsigned int *)key + 1;
      key_size -= sizeof(unsigned int);
    } else {
      unsigned int c = *(unsigned char *)key;
      hash           = __builtin_ia32_crc32si(hash, c);
      key            = (unsigned char *)key + 1;
      key_size -= 1;
    }
  }
  return hash;
}

int map_get(struct Map *map, void *key, int *value_out) {
  unsigned hash = khash(key, map->key_size);
  return map_impl_get(map->busybits, map->keyps, map->khs, map->chns, map->vals, key, map->key_size, hash, value_out,
                      map->capacity);
}

void map_put(struct Map *map, void *key, int value) {
  unsigned hash = khash(key, map->key_size);
  map_impl_put(map->busybits, map->keyps, map->khs, map->chns, map->vals, key, hash, value, map->capacity);
  ++map->size;
}

void map_erase(struct Map *map, void *key, void **trash) {
  unsigned hash = khash(key, map->key_size);
  map_impl_erase(map->busybits, map->keyps, map->khs, map->chns, key, map->key_size, hash, map->capacity, trash);
  --map->size;
}

unsigned map_size(struct Map *map) { return map->size; }
