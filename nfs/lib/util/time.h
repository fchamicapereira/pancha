#ifndef NF_TIME_H_INCLUDED
#define NF_TIME_H_INCLUDED
#include <stdint.h>

#define time_us_t int64_t
#define time_ns_t int64_t
#define PRVIGT PRId64

#define NS_TO_S_MULTIPLIER (1000000000ul)

time_ns_t current_time(void);

// Returns the last result of current_time. must only be called after
// current_time was invoked at least once.
time_ns_t recent_time(void);

#endif // NF_TIME_H_INCLUDED
