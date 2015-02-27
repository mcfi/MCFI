#ifndef STRING_H
#define STRING_H

#include <def.h>

size_t strlen(const char *s);
size_t strnlen(const char* s, size_t maxlen);
int strcmp(const char* a, const char* b);
int strncmp(const char *_l, const char *_r, size_t n);
void* memcpy(void *dest, const void *src, size_t n);
void *memset(void *dest, int c, size_t n);

static int memcmp(const void *vl, const void *vr, size_t n)
{
  const unsigned char *l=vl, *r=vr;
  for (; n && *l == *r; n--, l++, r++);
  return n ? *l-*r : 0;
}

static int isdigit(int ch) {
  return (ch >= '0') && (ch <= '9');
}

static int isalpha(int ch) {
  return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <='Z');
}

static int isalnum(int ch) {
  return isdigit(ch) || isalpha(ch);
}

static int skip_atoi(const char **s) {
  int i = 0;
  while(isdigit(**s))
    i = i*10 + *((*s)++) - '0';

  return i;
}

static inline int isupper(int c) {
  return c >= 'A' && c <= 'Z';
}

static inline int islower(int c) {
  return c >= 'a' && c <= 'z';
}

static inline int tolower(int c)
{
  if (isupper(c))
    c -= 'A'-'a';
  return c;
}

static inline int toupper(int c)
{
  if (islower(c))
    c -= 'a'-'A';
  return c;
}

#endif
