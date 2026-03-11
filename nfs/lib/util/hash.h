#ifndef HASH_H_INCLUDED
#define HASH_H_INCLUDED

unsigned hash_obj(void *obj, unsigned size_bytes);

// verifast doesn't know about these
unsigned __builtin_ia32_crc32si(unsigned acc, unsigned int x);
unsigned long long __builtin_ia32_crc32di(unsigned long long acc, unsigned long long x);

#endif