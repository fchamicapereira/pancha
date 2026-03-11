#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "vector.h"

struct Vector {
  char *data;
  int elem_size;
  unsigned capacity;
};

int vector_allocate(int elem_size, unsigned capacity, struct Vector **vector_out) {
  struct Vector *old_vector_val = *vector_out;
  struct Vector *vector_alloc   = (struct Vector *)malloc(sizeof(struct Vector));
  if (vector_alloc == 0)
    return 0;
  *vector_out = (struct Vector *)vector_alloc;

  char *data_alloc = (char *)malloc((uint32_t)elem_size * capacity);
  if (data_alloc == 0) {
    free(vector_alloc);
    *vector_out = old_vector_val;
    return 0;
  }
  (*vector_out)->data      = data_alloc;
  (*vector_out)->elem_size = elem_size;
  (*vector_out)->capacity  = capacity;

  for (unsigned i = 0; i < capacity; ++i) {
    memset((*vector_out)->data + elem_size * (int)i, 0, elem_size);
  }

  return 1;
}

void vector_borrow(struct Vector *vector, int index, void **val_out) { *val_out = vector->data + index * vector->elem_size; }

void vector_return(struct Vector *vector, int index, void *value) {}

void vector_clear(struct Vector *vector) { memset(vector->data, 0, vector->elem_size * vector->capacity); }

int vector_sample_lt(struct Vector *vector, int samples, void *threshold, int *index_out) {
  for (int i = 0; i < samples; i++) {
    int index  = rand() % vector->capacity;
    void *elem = vector->data + index * vector->elem_size;

    int threshold_lt_elem = 0;
    for (int j = vector->elem_size - 1; j >= 0; j--) {
      if (((uint8_t *)threshold)[j] > ((uint8_t *)elem)[j]) {
        break;
      }

      if (((uint8_t *)threshold)[j] < ((uint8_t *)elem)[j]) {
        threshold_lt_elem = 1;
        break;
      }
    }

    if (threshold_lt_elem) {
      *index_out = index;
      return 1;
    }
  }

  return 0;
}