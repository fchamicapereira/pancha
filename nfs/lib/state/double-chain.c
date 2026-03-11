#include "double-chain.h"

#include <stdlib.h>
#include <stddef.h>

#include "double-chain-impl.h"

#ifndef NULL
#define NULL 0
#endif // NULL

struct DoubleChain {
  struct dchain_cell *cells;
  time_ns_t *timestamps;
};

int dchain_allocate(int index_range, struct DoubleChain **chain_out) {
  struct DoubleChain *old_chain_out = *chain_out;
  struct DoubleChain *chain_alloc   = (struct DoubleChain *)malloc(sizeof(struct DoubleChain));
  if (chain_alloc == NULL)
    return 0;
  *chain_out = (struct DoubleChain *)chain_alloc;

  struct dchain_cell *cells_alloc = (struct dchain_cell *)malloc(sizeof(struct dchain_cell) * (index_range + DCHAIN_RESERVED));
  if (cells_alloc == NULL) {
    free(chain_alloc);
    *chain_out = old_chain_out;
    return 0;
  }
  (*chain_out)->cells = cells_alloc;

  time_ns_t *timestamps_alloc = (time_ns_t *)malloc(sizeof(time_ns_t) * (index_range));
  if (timestamps_alloc == NULL) {
    free((void *)cells_alloc);
    free(chain_alloc);
    *chain_out = old_chain_out;
    return 0;
  }
  (*chain_out)->timestamps = timestamps_alloc;

  dchain_impl_init((*chain_out)->cells, index_range);
  return 1;
}

int dchain_allocate_new_index(struct DoubleChain *chain, int *index_out, time_ns_t time) {
  int ret = dchain_impl_allocate_new_index(chain->cells, index_out);
  if (ret) {
    chain->timestamps[*index_out] = time;
  }
  return ret;
}

int dchain_rejuvenate_index(struct DoubleChain *chain, int index, time_ns_t time) {
  int ret = dchain_impl_rejuvenate_index(chain->cells, index);
  if (ret) {
    chain->timestamps[index] = time;
  }
  return ret;
}

int dchain_expire_one_index(struct DoubleChain *chain, int *index_out, time_ns_t time) {
  int has_ind = dchain_impl_get_oldest_index(chain->cells, index_out);
  if (has_ind) {
    if (chain->timestamps[*index_out] < time) {
      int rez = dchain_impl_free_index(chain->cells, *index_out);
      return rez;
    }
  }
  return 0;
}

int dchain_is_index_allocated(struct DoubleChain *chain, int index) { return dchain_impl_is_index_allocated(chain->cells, index); }

int dchain_free_index(struct DoubleChain *chain, int index) { return dchain_impl_free_index(chain->cells, index); }
