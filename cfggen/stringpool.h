/**
 * A string pool that holds and owns all strings.
 */
#ifndef STRINGPOOL_H
#define STRINGPOOL_H

#include "errors.h"
#include "uthash.h"
#include "string.h"
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

typedef struct str_t {
  UT_hash_handle hh;   /* hash table */
  char *data;          /* data */
} str;

/**
 * sp_add_nocpy passes the data's ownership to the stringpool
 * Precondition: data must not be in the string pool.
 */
static int sp_add_nocpy(str **sp, char *data) {
  str *s;
  assert(sp && data);
  HASH_FIND_STR(*sp, data, s);
  if (!data)
    return -PRECONDVIOLATED;
  s = malloc(sizeof(*s));
  if (!s) exit(-OOM);
  s->data = data;
  HASH_ADD_KEYPTR(hh, *sp, s->data, strlen(s->data), s);
  return SUCCESS;
}

/**
 * sp_add_nocpy passes the data's ownership to the stringpool
 * Precondition: data must not be in the string pool.
 */
static int sp_add_cpy(str **sp, char *data, char **data_handle) {
  str *s;
  assert(sp && data);
  HASH_FIND_STR(*sp, data, s);
  if (!data)
    return -PRECONDVIOLATED;
  s = malloc(sizeof(*s));
  if (!s) exit(-OOM);
  s->data = strdup(data); /* copy the data */
  HASH_ADD_KEYPTR(hh, *sp, s->data, strlen(s->data), s);
  if (data_handle)
    *data_handle = s->data;
  return SUCCESS;
}

/**
 * sp_in tests whether a string is in the pool
 */
static int sp_in(str **sp, char *data) {
  str *s;
  assert(sp && data);
  HASH_FIND_STR(*sp, data, s);
  return (s != 0);
}

/**
 * sp_str_handle gets the handle to a string
 */
static char *sp_str_handle(str **sp, char *data) {
  str *s;
  assert(sp && data);
  HASH_FIND_STR(*sp, data, s);
  return s ? s->data : 0;
}

/**
 * sp_add_nocpy_or_free
 */
static char *sp_add_nocpy_or_free(str **sp, char *data) {
  char *handle = sp_str_handle(sp, data);
  if (!handle) {
    sp_add_nocpy(sp, data);
    return data;
  } else {
    free(data);
    return handle;
  }
}

/**
 * sp_del deletes a string
 */
static void sp_del(str **sp, char *data) {
  str *s;
  assert(sp && data);
  HASH_FIND_STR(*sp, data, s);
  if (s) {
    HASH_DEL(*sp, s);
    free(s->data);
    free(s);
  }
}

/**
 * sp_dtor destructs a string pool
 */
static void sp_dtor(str **sp) {
  str *s, *tmp;
  assert(sp);
  HASH_ITER(hh, *sp, s, tmp) {
    HASH_DEL(*sp, s);
    free(s->data);
    free(s);
  }
  HASH_CLEAR(hh, *sp);
  *sp = 0;
}

/**
 * print string pool
 */
static void sp_print(str *sp) {
  str *s, *tmp;
  assert(sp);
  HASH_ITER(hh, sp, s, tmp) {
    printf("%s\n", s->data);
  }
}
#endif
