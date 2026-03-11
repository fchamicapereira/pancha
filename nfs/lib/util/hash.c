#include "hash.h"

#include <stdint.h>

unsigned hash_obj(void *obj, unsigned size_bytes) {
  unsigned hash = 0;
  while (size_bytes > 0) {
    if (size_bytes >= sizeof(unsigned int)) {
      hash = __builtin_ia32_crc32si(hash, *(unsigned int *)obj);
      obj  = (unsigned int *)obj + 1;
      size_bytes -= sizeof(unsigned int);
    } else {
      unsigned int c = *(unsigned char *)obj;
      hash           = __builtin_ia32_crc32si(hash, c);
      obj            = (unsigned char *)obj + 1;
      size_bytes -= 1;
    }
  }
  return hash;
}