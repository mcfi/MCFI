#include <string.h>

size_t strlen(const char *s)
{
  const char* w = s;
  while (*w++);
  return w-s;
}

int strcmp(const char *l, const char *r)
{
  for (; *l==*r && *l; l++, r++);
  return *(unsigned char *)l - *(unsigned char *)r;
}

int strncmp(const char *_l, const char *_r, size_t n)
{
  const unsigned char *l=(void *)_l, *r=(void *)_r;
  if (!n--) return 0;
  for (; *l && *r && n && *l == *r ; l++, r++, n--);
  return *l - *r;
}

void *memcpy(void * restrict dest, const void * restrict src, size_t n)
{
  char* d = dest;
  const char* s = src;
  for (; n; n--) *d++ = *s++;
  return dest;
}

void *memset(void *dest, int c, size_t n) {
  unsigned char *s = dest;
  for (; n; n--, s++) *s = c;
  return dest;
}

size_t strnlen(const char* s, size_t maxlen) {
  const char *es = s;
  while (*es && maxlen) {
    es++;
    maxlen--;
  }

  return (es - s);
}
