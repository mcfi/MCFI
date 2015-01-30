/**
 * Adjacency list-based graph representation
 */

#ifndef GRAPH_H
#define GRAPH_H
#include "errors.h"
#include "uthash.h"
#include "utlist.h"

#include <stdio.h>

typedef struct vertex_t {
  UT_hash_handle hh;         /* hash table handle */
  void *val;                 /* vertex value */
  struct vertex_t *adj;      /* connecting vertices */
} vertex;

static int g_in(vertex **g, void *val) {
  vertex *v;
  assert(g);
  HASH_FIND_PTR(*g, &val, v);
  return !!v;
}

static void g_add_vertex(vertex **g, void *val) {
  vertex *v;
  assert(g);
  HASH_FIND_PTR(*g, &val, v);
  if (!v) {
    v = malloc(sizeof(*v));
    if (!v) exit(-OOM);
    v->val = val;
    v->adj = 0;
    HASH_ADD_PTR(*g, val, v);
  }
}

static void g_add_edge_helper(vertex **g, void *val1, void *val2) {
  vertex *v;
  assert(g);
  HASH_FIND_PTR(*g, &val1, v);
  if (!v) {
    v = malloc(sizeof(*v));
    if (!v) exit(-OOM);
    v->val = val1;
    v->adj = 0; /* initialize the adjacent list */
    HASH_ADD_PTR(*g, val, v);
  }
  g_add_vertex(&(v->adj), val2);
}

static void g_add_edge(vertex **g, void *val1, void *val2) {
  g_add_edge_helper(g, val1, val2);
  g_add_edge_helper(g, val2, val1);
}

static void g_del_vertex(vertex **g, void *val) {
  vertex *v;
  assert(g);
  HASH_FIND_PTR(*g, &val, v);
  if (v) {
    HASH_DEL(*g, v);
    free(v);
  }
}

static void g_del_edge_helper(vertex **g, void *val1, void *val2) {
  vertex *v;
  assert(g);
  HASH_FIND_PTR(*g, &val1, v);
  if (v) {
    g_del_vertex(&(v->adj), val2);
    if (!v->adj) { /* no node left */
      HASH_DEL(*g, v);
      free(v);
    }
  }
}

static void g_del_edge(vertex **g, void *val1, void *val2) {
  g_del_edge_helper(g, val1, val2);
  g_del_edge_helper(g, val2, val1);
}

/* destruct vertices */
static void g_dtor_vertex(vertex **g) {
  assert(g);
  vertex *v, *tmp;
  HASH_ITER(hh, *g, v, tmp) {
    HASH_DEL(*g, v);
    free(v);
  }
}

static void g_dtor(vertex **g) {
  assert(g);
  vertex *v, *tmp;
  HASH_ITER(hh, *g, v, tmp) {
    g_dtor_vertex(&(v->adj));
    HASH_DEL(*g, v);
    free(v);
  }
}


static void g_print_adj(vertex *adj) {
  vertex *v, *tmp;
  HASH_ITER(hh, adj, v, tmp) {
    printf("%ld ", (long)v->val);
  }
  printf("\n");
}

static void g_print(vertex *g) {
  vertex *v, *tmp;
  HASH_ITER(hh, g, v, tmp) {
    printf("%ld->", (long)v->val);
    g_print_adj(v->adj);
  }
}

typedef struct node_t {
  struct node_t *next, *prev;
  void *val;
} node;

/* do a breadth-first search from node val */
static vertex *g_bfs(vertex **g, void *val) {
  vertex *rg = 0;    /* result nodes */
  node *q = 0;   /* queue of vertices */
  vertex *v, *vi, *tmp;
  node *qv = 0;
  qv = malloc(sizeof(*qv));
  qv->val = val;
  DL_APPEND(q, qv); /* add the initial node into the q */
  while (q) {        /* q is not empty */
    node *front = q;
    DL_DELETE(q, front);
    g_add_vertex(&rg, front->val);
    HASH_FIND_PTR(*g, &(front->val), v);
    assert(v);
    HASH_ITER(hh, v->adj, vi, tmp) { /* for every adjacent vertex */
      vertex *vt;
      HASH_FIND_PTR(rg, &(vi->val), vt);
      if (!vt) {
        qv = malloc(sizeof(*qv));
        if (!qv) exit(-OOM);
        qv->val = vi->val;
        DL_APPEND(q, qv);
      }
    }
    free(front);
  }
  return rg;
}

/* get a list of connected components */
static node *g_get_lcc(vertex **g) {
  assert(g);
  node *lcc = 0;
  node *cc;
  vertex *explored = 0;
  vertex *v, *tmp;
  vertex *vi, *tmpi;
  HASH_ITER(hh, *g, v, tmp) {
    if (!g_in(&explored, v->val)) {
      cc = malloc(sizeof(*cc));
      if (!cc) exit(-OOM);
      cc->val = g_bfs(g, v->val);
      DL_APPEND(lcc, cc);
      /* add all nodes in cc to expored */
      HASH_ITER(hh, (vertex*)(cc->val), vi, tmpi) {
        g_add_vertex(&explored, vi->val);
      }
    }
  }
  HASH_CLEAR(hh, explored);
  return lcc;
}

static void l_print(node *l, void (*print)(void *)) {
  node *elt;
  DL_FOREACH(l, elt) {
    printf("Ele\n");
    print(elt->val);
  }
}

#endif
