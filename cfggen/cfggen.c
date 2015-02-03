#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

#include "cfggen.h"

code_module *modules = 0;
str *sp = 0;            /* string pool */
graph *callgraph = 0;   /* call graph */
graph *rtgraph = 0;     /* return graph */
graph *cha = 0;         /* class hierarchy */
dict *chacc = 0;        /* class hierarchy cache */
dict *classes = 0;      /* cpp classes */

static void *xmalloc(size_t size) {
  char *ptr = malloc(size);
  if (!ptr) exit(-OOM);
  ptr[size-1] = '\0';
  return ptr;
}

static size_t next_symbol(char *cursor, char sym,
                          int *stop) {
  size_t advanced = 0;
  if (*cursor) {
    while (*cursor != sym && *cursor != '\0') {
      cursor++;
      advanced++;
    }
  }
  *stop = (*cursor == '\0');
  return advanced;
}

static char *parse_inheritance(char *cursor, const char* end, graph **cha) {
  /****************************************************************************/
#define _get_string(cursor, name, name_len, symbol, ptrstop) do {   \
    name_len = next_symbol(cursor, symbol, ptrstop);                \
    name = xmalloc(name_len + 1);                                   \
    memcpy(name, cursor, name_len);                                 \
    name = sp_add_nocpy_or_free(&sp, name);                         \
    cursor += (name_len + 1);                                       \
  } while (0);
  /****************************************************************************/
  assert(cursor < end);
  int stop;
  /* subclass */
  char *sub_class_name;
  size_t name_len;
  _get_string(cursor, sub_class_name, name_len, '#', &stop);
  do {
    char *super_class_name;
    _get_string(cursor, super_class_name, name_len, '#', &stop);
    g_add_edge(cha, sub_class_name, super_class_name);
  } while (!stop);
#undef _get_string
  return cursor;
}

static char *parse_classes(char *cursor, const char *end, dict **classes) {
  assert(cursor < end);
  int stop;
  size_t name_len = next_symbol(cursor, '#', &stop);
  assert(!stop);
  char *class_name = xmalloc(name_len + 1);
  memcpy(class_name, cursor, name_len);
  class_name = sp_add_nocpy_or_free(&sp, class_name);
}

void parse_cha(char* content, const char *end,
               dict **classes, graph **cha) {
  assert(content && classes && cha);
  char *cursor = content;
  while (cursor < end) {
    switch (*cursor) {
    case 'I':
      cursor = parse_inheritance(cursor+2, end, cha);
      break;
    case 'M':
      cursor += strlen(cursor);
      cursor += 1;
      break;
    default:
      fprintf(stderr, "Invalid cha info at offset %lu\n", cursor - content);
      exit(-1);
    }
  }
  node *lcc = g_get_lcc(cha);
  //sp_print(sp);
  l_print(lcc, g_print);
}

int main(int argc, char **argv) {
  struct stat statbuf;
  stat("xalan.cha", &statbuf);
  size_t len = statbuf.st_size;
  int fd = open("xalan.cha", O_RDONLY);
  assert(fd > -1);
  char *buf = mmap(0, (len + 4095)/4096*4096,
                   PROT_READ, MAP_PRIVATE,
                   fd, 0);
  assert(buf != (char*)-1);
  parse_cha(buf, buf + len, &classes, &cha);
}
