#ifndef CFGGEN_H
#define CFGGEN_H

#include "uthash.h"
#include "utlist.h"
#include "graph.h"
#include "stringpool.h"

enum Vertex_Type {
  RETURN,          /* return instruction */
  CALL,            /* indirect call */
  FUNCTION,        /* function */
  RETURNADDR       /* return address */
};

enum ICF_Type {
  VirtualMethodCall,
  PointerToMethodCall,
  NormalCall
};

enum Qualifiers {
  CONSTANT = 1,
  VOLATILE = 2,
  STATIC = 4,
  VIRTUAL = 8
};

typedef struct code_module_t code_module;
typedef struct function_t function;
typedef struct icf_t icf;
typedef struct ra_t ra;

struct icf_t {
  char *id;
  enum ICF_Type ity;
  char *type;
  unsigned char attrs; /* constant / volatile */
  char *class_name;
  char *method_name;
  size_t offset;
  UT_hash_handle hh;
};

static icf *alloc_icf(enum Vertex_Type ty) {
  icf *i = malloc(sizeof(*i));
  if (!i) exit(-OOM);
  return i;
}

static void free_icf(icf *i) {
  free(i);
}

struct function_t {
  char *name;
  char *class_name;
  char *method_name;
  char *type;           /* type of the function */
  size_t offset;        /* offset relative to the code start */
  node *returns;        /* return instructions */
  node *dtails;         /* direct tail calls */
  node *itails;         /* indirect tail calls */
  struct function_t *prev, *next;
};

static function *alloc_function(void) {
  function *f = malloc(sizeof(*f));
  if (!f) exit(-OOM);
  memset(f, 0, sizeof(*f));
  return f;
}

static void free_function(function *f) {
  node *elt, *tmp;
  DL_FOREACH_SAFE(f->returns, elt, tmp) {
    DL_DELETE(f->returns, elt);
    free(elt);
  }
  DL_FOREACH_SAFE(f->dtails, elt, tmp) {
    DL_DELETE(f->dtails, elt);
    free(elt);
  }
  DL_FOREACH_SAFE(f->itails, elt, tmp) {
    DL_DELETE(f->itails, elt);
    free(elt);
  }
  free(f);
}

struct ra_t {
  enum Vertex_Type ty;
  char *name;           /* return address of a direct call */
  icf  *ic;             /* return address of an indirect call */
  size_t offset;
  struct ra_t *next, *prev;
};

static ra *alloc_ra(void) {
  ra *r = malloc(sizeof(*r));
  if (!r) exit(-OOM);
  memset(r, 0, sizeof(*r));
  r->ty = RETURNADDR;
  return r;
};

static void free_ra(ra *r) {
  free(r);
}

/**
 * A code module may be an executable or a *.so library.
 */
struct code_module_t {
  char *path;          /* absolute path of the code module */
  uintptr_t start;     /* code start address */
  icf      *icalls;    /* indirect call instructions */
  function *functions; /* functions */
  ra       *ras;       /* return addresses */
  vertex   *addr_taken_functions; /* functions whose addresses are taken */
  struct code_module_t *next, *prev;
};

static code_module *alloc_code_module(void) {
  code_module *cm = malloc(sizeof(*cm));
  if (!cm) exit(-OOM);
  memset(cm, 0, sizeof(*cm));
  return cm;
}

/*
static void free_code_module(code_module *cm) {
  icf *i, *tmpi;
  DL_FOREACH_SAFE(cm->icalls, i, tmpi) {
    DL_DELETE(cm->icalls, i);
    free_icf(i);
  }
  function *f, *tmpf;
  DL_FOREACH_SAFE(cm->functions, f, tmpf) {
    DL_DELETE(cm->functions, f);
    free_function(f);
  }
  ra *r, *tmpr;
  DL_FOREACH_SAFE(cm->ras, r, tmpr) {
    DL_DELETE(cm->ras, r);
    free_ra(r);
  }
  g_dtor(&(cm->addr_taken_functions));
}
*/
#endif
