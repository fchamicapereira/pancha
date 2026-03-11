#include "expirator.h"
#include <assert.h>

int expire_items_single_map(struct DoubleChain *chain, struct Vector *vector, struct Map *map, time_ns_t time) {
  int count = 0;
  int index = -1;

  while (dchain_expire_one_index(chain, &index, time)) {
    void *key;
    vector_borrow(vector, index, &key);
    map_erase(map, key, &key);
    vector_return(vector, index, key);
    ++count;
  }
  return count;
}

int expire_items_single_map_iteratively(struct Vector *vector, struct Map *map, int start, int n_elems) {
  assert(start >= 0);
  assert(n_elems >= 0);
  void *key;
  for (int i = start; i < n_elems; i++) {
    vector_borrow(vector, i, (void **)&key);
    map_erase(map, key, (void **)&key);
    vector_return(vector, i, key);
  }
  return n_elems;
}
