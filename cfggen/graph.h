/**
 * Adjacency list-based graph representation
 */

#ifndef GRAPH_H
#define GRAPH_H
#include "kv.h"

typedef keyvalue vertex;
typedef vertex graph;

static vertex *new_vertex(void* val) {
  return new_kv(val, 0);
}

static int g_in(const vertex *g, const void *val) {
  return dict_in(g, val);
}

static void g_add_vertex(vertex **g, void *val) {
  if (!g_in(*g, val))
    dict_add(g, val, 0);
}

static void g_add_edge_helper(vertex **g, void *val1, void *val2) {
  vertex *v = dict_find(*g, val1);
  if (!v) {
    g_add_vertex(g, val1);
    v = dict_find(*g, val1);
  }
  g_add_vertex((vertex**)(&(v->value)), val2);
}

static void g_add_edge(vertex **g, void *val1, void *val2) {
  g_add_edge_helper(g, val1, val2);
  g_add_edge_helper(g, val2, val1);
}

static void g_add_directed_edge(vertex **g, void *val1, void *val2) {
  g_add_edge_helper(g, val1, val2);
}

static void g_del_vertex(vertex **g, void *val) {
  vertex *v = dict_find(*g, val);
  if (v) {
    dict_clear((vertex**)(&(v->value)));
    dict_del(g, val);
  }
}

static void g_del_edge_helper(vertex **g, void *val1, void *val2) {
  vertex *v = dict_find(*g, val1);
  if (v) {
    dict_del((vertex**)&(v->value), val2);
    if (!v->value) { /* no node left */
      dict_del(g, val1);
    }
  }
}

static void g_del_edge(vertex **g, void *val1, void *val2) {
  g_del_edge_helper(g, val1, val2);
  g_del_edge_helper(g, val2, val1);
}

static void g_del_directed_edge(vertex **g, void *val1, void *val2) {
  g_del_edge_helper(g, val1, val2);
}

static void g_free_adj(vertex **g) {
  dict_clear(g);
}

static void g_dtor(vertex **g) {
  dict_dtor(g, 0, g_free_adj);
}

static void g_print_vertex(void *v) {
  printf("%lu", (unsigned long)v);
}

static void g_print_adj(vertex *g) {
  dict_print(g, g_print_vertex, -1);
}

static void g_print(vertex *g) {
  dict_print(g, 0, g_print_adj);
}

typedef struct node_t {
  struct node_t *next, *prev;
  void *val;
} node;

static node *new_node(void *val) {
  node *n = malloc(sizeof(*n));
  if (!n) exit(-OOM);
  n->val = val;
  return n;
}

/* do a breadth-first search from node val */
static vertex *g_bfs(vertex **g, void *val) {
  vertex *rg = 0;    /* result nodes */
  node *q = 0;       /* queue of vertices */
  vertex *v, *vi, *tmp;
  node *qv = 0;
  qv = new_node(val);
  DL_APPEND(q, qv); /* add the initial node into the q */
  while (q) {       /* q is not empty */
    node *front = q;
    DL_DELETE(q, front);
    g_add_vertex(&rg, front->val);
    v = dict_find(*g, front->val);
    assert(v);
    HASH_ITER(hh, (vertex*)(v->value), vi, tmp) { /* for every adjacent vertex */
      vertex *vt = dict_find(rg, vi->key);
      if (!vt) {
        qv = new_node(vi->key);
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
    if (!g_in(explored, v->key)) {
      cc = new_node(g_bfs(g, v->key));
      DL_APPEND(lcc, cc);
      /* add all nodes in cc to expored */
      HASH_ITER(hh, (vertex*)(cc->val), vi, tmpi) {
        g_add_vertex(&explored, vi->key);
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
