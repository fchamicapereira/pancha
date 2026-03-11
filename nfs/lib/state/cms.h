#ifndef _CMS_H_INCLUDED_
#define _CMS_H_INCLUDED_

#include "cms-util.h"

#include <stdbool.h>
#include <stdint.h>

#include "../util/time.h"

struct CMS;

int cms_allocate(uint32_t height, uint32_t width, uint32_t key_size,
                 time_ns_t periodic_cleanup_interval, struct CMS **cms_out);
void cms_increment(struct CMS *cms, void *key);
int cms_count_min(struct CMS *cms, void *key);
int cms_periodic_cleanup(struct CMS *cms, time_ns_t now);

#endif