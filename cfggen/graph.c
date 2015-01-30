#include "graph.h"

vertex *g = 0;

int main() {
  g_add_edge(&g, (void*)1, (void*)2);
  g_add_edge(&g, (void*)5, (void*)6);
  g_add_edge(&g, (void*)1, (void*)6);
  g_add_edge(&g, (void*)3, (void*)4);
  g_print(g);
  printf("cc\n");
  node *lcc = g_get_lcc(&g);
  l_print(lcc, g_print);
  printf("good\n");
  g_del_edge(&g, (void*)1, (void*)2);
  g_print(g);
  printf("good\n");
  lcc = g_get_lcc(&g);
  l_print(lcc, g_print);
  g_dtor(&g);
}
