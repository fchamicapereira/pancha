#include "time.h"

#include <time.h>
#include <assert.h>

time_ns_t last_time = 0;

time_ns_t current_time(void) {
  struct timespec tp;
  clock_gettime(CLOCK_MONOTONIC, &tp);
  last_time = tp.tv_sec * 1000000000ul + tp.tv_nsec;
  return last_time;
}

time_ns_t recent_time(void) { return last_time; }
