/**
 * A string pool that holds and owns all strings.
 */
#ifndef STRINGPOOL_H
#define STRINGPOOL_H

#include "uthash.h"

typedef struct str_t {
  UT_hash_handle hh;   /* hash table */
  char *data;          /* data */
} str;

/**
 * sp_add_nocpy passes the data's ownership to the stringpool
 * Precondition: data must not be in the string pool.
 */
static int sp_add_nocpy(str **sp, char *data) {
  str *s = malloc(sizeof(*s));
  if (!s) oom();
  s->data = data;
  HASH_ADD_KEYPTR(hh, *sp, s->data, strlen(s->data), s);
  return SUCCESS;
}

/**
 * sp_add_nocpy passes the data's ownership to the stringpool
 * Precondition: data must not be in the string pool.
 */
static int sp_add_cpy(str **sp, char *data, char **data_handle) {
  str *s = malloc(sizeof(*s));
  if (!s) oom();
  size_t len = strlen(data);
  s->data = malloc(len + 1); /* copy the data */
  if (!s->data) oom();
  memcpy(s->data, data, len + 1);
  HASH_ADD_KEYPTR(hh, *sp, s->data, strlen(s->data), s);
  if (data_handle)
    *data_handle = s->data;
  return SUCCESS;
}

/**
 * sp_str_handle gets the handle to a string
 */
static char *sp_str_handle(str **sp, char *data) {
  str *s = 0;
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
 * sp_add_cpy_or_nothing
 */
static char *sp_add_cpy_or_nothing(str **sp, char *data) {
  char *handle = sp_str_handle(sp, data);
  if (!handle) {
    sp_add_cpy(sp, data, &handle);
    return handle;
  }
  return handle;
}

/**
 * sp_del deletes a string
 */
static void sp_del(str **sp, char *data) {
  str *s;
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
  HASH_ITER(hh, sp, s, tmp) {
    dprintf(STDERR_FILENO, "%s\n", s->data);
  }
}
#endif
