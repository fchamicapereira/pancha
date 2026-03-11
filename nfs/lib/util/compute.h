#ifndef _COMPUTE_H_INCLUDED_
#define _COMPUTE_H_INCLUDED_

#include <stdint.h>
#include <stdbool.h>

static inline uint64_t ensure_power_of_two(uint64_t n) {
  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
  n |= n >> 32;
  n++;
  return n;
}

static inline bool is_power_of_two(uint64_t n) { return (n & (n - 1)) == 0; }

static inline bool is_prime(uint64_t n) {
  if (n <= 1)
    return false;
  if (n <= 3)
    return true;

  // Slow but simple
  for (uint64_t i = 2; i < n; i++) {
    if (n % i == 0) {
      return false;
    }
  }

  return true;
}

#endif