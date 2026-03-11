#ifndef _CHT_H_INCLUDED_
#define _CHT_H_INCLUDED_

#include "double-chain.h"
#include "vector.h"

// MAX_CHT_HEIGHT*MAX_CHT_HEIGHT < MAX_INT
#define MAX_CHT_HEIGHT 40000

int cht_fill_cht(struct Vector *cht, uint32_t cht_height,
                 uint32_t backend_capacity);

int cht_find_preferred_available_backend(uint64_t hash, struct Vector *cht,
                                         struct DoubleChain *active_backends,
                                         uint32_t cht_height,
                                         uint32_t backend_capacity,
                                         int *chosen_backend);

#endif //_CHT_H_INCLUDED_
