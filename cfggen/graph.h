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

static int g_directed_edge_in(const vertex *g, const void *val1, const void *val2) {
  vertex *v = dict_find(g, val1);
  if (v) {
    v = dict_find((vertex*)(v->value), val2);
    if (v)
      return TRUE;
  }
  return FALSE;
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

static void g_add_directed_l2_edge(vertex **g, void *val1, void *val2, void *val3) {
  g_add_edge_helper(g, val1, val2);
  vertex *v = dict_find(*g, val1);
  g_add_edge_helper((vertex**)(&(v->value)), val2, val3);
}

static void* g_find_l2_vertex(vertex *g, void *val1, void *val2) {
  vertex *v1 = dict_find(g, val1);
  if (v1) {
    vertex *v2 = dict_find((vertex*)(v1->value), val2);
    if (v2)
      return v2;
  }
  return 0;
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

static void g_free_adj(void **g) {
  dict_clear((vertex**)g);
}

static void g_dtor(vertex **g) {
  dict_dtor(g, 0, g_free_adj);
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

/* get the directory where each node is connected with
   all its connected nodes */

static graph *g_transitive_closure(vertex **g) {
  assert(g);
  graph *rs = 0;
  node *lcc = 0;
  lcc = g_get_lcc(g);
  if (lcc) {
    node *n, *ntmp;
    DL_FOREACH_SAFE(lcc, n, ntmp) {
      vertex *v, *tmp;
      HASH_ITER(hh, (vertex*)n->val, v, tmp) {
        g_add_directed_edge(&rs, v->key, n->val);
        //printf("%s+", v->key);
      }
      //printf("\n");
      DL_DELETE(lcc, n);
      free(n);
    }
  }
  return rs;
}

static void g_free_transitive_closure(vertex **g) {
  /* IMPORTANT: free the nodes just once */
}

static void g_add_cc_to_g(vertex **g, vertex *cc) {
  if (g && cc) {
    vertex *first = cc;
    HASH_DEL(cc, first);
    vertex *v, *tmp;
    HASH_ITER(hh, cc, v, tmp) {
      HASH_DEL(cc, v);
      g_add_edge(g, first->key, v->key);
      free(v);
    }
    free(first);
  }
}

static void l_print(node *l, void (*print)(void *)) {
  node *elt;
  DL_FOREACH(l, elt) {
    printf("\n***********************************\n");
    print(elt->val);
  }
}

#endif
