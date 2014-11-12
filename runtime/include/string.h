#ifndef STRING_H
#define STRING_H

#include <def.h>

size_t strlen(const char *s);
int strcmp(const char* a, const char* b);
void* memcpy(void *dest, const void *src, size_t n);
void *memset(void *dest, int c, size_t n);

#endif
