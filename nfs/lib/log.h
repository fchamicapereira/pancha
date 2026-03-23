#pragma once

#include <stdio.h>
#include <inttypes.h>

#define NF_INFO(text, ...)                                                                                                                           \
  printf(text "\n", ##__VA_ARGS__);                                                                                                                  \
  fflush(stdout);

#ifndef NDEBUG
#include <stdio.h>
#include <inttypes.h>
#define NF_DEBUG(text, ...)                                                                                                                          \
  fprintf(stderr, "DEBUG: " text "\n", ##__VA_ARGS__);                                                                                               \
  fflush(stderr);
#else // NDEBUG
#define NF_DEBUG(...)
#endif // NDEBUG
