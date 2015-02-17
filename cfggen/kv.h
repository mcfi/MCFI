#ifndef SET_H
#define SET_H

#include <stdlib.h>
#include <stdio.h>
#include "errors.h"
#include "uthash.h"
#include "utlist.h"

typedef struct keyvalue_t {
  UT_hash_handle hh;
  void *key;
  void *value;
} keyvalue;

static keyvalue *new_kv(void* key, void *value) {
  keyvalue *kv = malloc(sizeof(*kv));
  if (!kv) exit(-OOM);
  kv->key = key;
  kv->value = value;
  return kv;
}

static void free_kv(keyvalue *kv) {
  if (kv) free(kv);
}

static keyvalue *dict_find(const keyvalue *dict, const void *key) {
  keyvalue *kv = 0;
  HASH_FIND_PTR(dict, &key, kv);
  return kv;
}

static int dict_in(const keyvalue *dict, const void *key) {
  return !!dict_find(dict, key);
}

static keyvalue *dict_add(keyvalue **dict, void *key, void *value) {
  assert(!dict_in(*dict, key));
  keyvalue *kv = new_kv(key, value);
  HASH_ADD_PTR(*dict, key, kv);
  return kv;
}

static void dict_del(keyvalue **dict, void *key) {
  keyvalue *kv = dict_find(*dict, key);
  if (kv) {
    HASH_DEL(*dict, kv);
    free(kv);
  }
}

static void dict_clear(keyvalue **dict) {
  keyvalue *kv, *tmp;
  HASH_ITER(hh, *dict, kv, tmp) {
    HASH_DEL(*dict, kv);
    free(kv);
  }
}

static void dict_dtor(keyvalue **dict,
                      void (*free_key)(void **key),
                      void (*free_value)(void **value)) {
  keyvalue *kv, *tmp;
  HASH_ITER(hh, *dict, kv, tmp) {
    HASH_DEL(*dict, kv);
    if (free_key)
      free_key(&(kv->key));
    if (free_value)
      free_value(&(kv->value));
    free(kv);
  }
}

static void dict_print(keyvalue *dict,
                       void (*print_key)(const void *key),
                       void (*print_value)(const void *value)) {
  keyvalue *kv, *tmp;
  printf("{");
  HASH_ITER(hh, dict, kv, tmp) {
    if (print_key)
      print_key(kv->key);
    if (print_value) {
      printf(": ");
      print_value(kv->value);
      printf(", ");
    }
  }
  printf("}\n");
}

typedef keyvalue dict;

#endif
