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

static void print_string(void *str) {
  printf("%s\n", (char*)str);
}
static void cha_cc_print(graph *g) {
  dict_print(g, print_string, -1);
}

static char *_get_string_before_symbol(/*in/out*/char **cursor, char symbol,
                                       /*out*/int *stop, /*len*/size_t *len) {
  size_t name_len = next_symbol(*cursor, symbol, stop);
  char *string = xmalloc(name_len + 1);
  memcpy(string, *cursor, name_len);
  *cursor += (name_len + 1);
  if (len)
    *len = name_len;
  return string;
}

static char *sp_intern_string(char *string) {
  return sp_add_nocpy_or_free(&sp, string);
}

static char *parse_inheritance(char *cursor, const char* end, graph **cha) {
  int stop;
  char *sub_class_name =
    sp_intern_string(_get_string_before_symbol(&cursor, '#', &stop, 0));
  while (!stop) {
    char *super_class_name =
      sp_intern_string(_get_string_before_symbol(&cursor, '#', &stop, 0));
    g_add_edge(cha, sub_class_name, super_class_name);
  }
  return cursor;
}

static unsigned char _get_cpp_method_attr(/*in/out*/char *method_name,
                                          size_t len) {
  unsigned char attrs = 0;
  int counter = 5;
  for (--len; len > 0 && method_name[len] != '@' && counter > 0; --len, counter--) {
    switch (method_name[len]) {
    case 'c':
      attrs &= 1;
      break;
    case 'o':
      attrs &= 2;
      break;
    case 's':
      attrs &= 4;
      break;
    case 'v':
      attrs &= 8;
      break;
    }
  }
  if (method_name[len] == '@')
    method_name[len] = '\0';
  return attrs;
}

static int isConstant(unsigned char attrs) {
  return attrs & 1;
}

static int isVolatile(unsigned char attrs) {
  return attrs & 2;
}

static int isStatic(unsigned char attrs) {
  return attrs & 4;
}

static int isVirtual(unsigned char attrs) {
  return attrs & 8;
}

static char *parse_classes(char *cursor, const char *end, dict **classes) {
  int stop;
  char *class_name =
    sp_intern_string(_get_string_before_symbol(&cursor, '#', &stop, 0));
  keyvalue *class_entry = dict_find(*classes, class_name);
  if (!class_entry) {
    class_entry = dict_add(classes, class_name, 0);
  }
  while (!stop) {
    size_t len;
    char *method_name = _get_string_before_symbol(&cursor, '#', &stop, &len);
    unsigned char attrs = _get_cpp_method_attr(method_name, len);
    method_name = sp_intern_string(method_name);
    keyvalue *method_entry = dict_find((dict*)class_entry->value, method_name);
    if (!method_entry)
      dict_add((dict**)&(class_entry->value), method_name, (void*)attrs);
    else {
      if ((unsigned char)method_entry->value != attrs) {
        fprintf(stderr, "Duplicated method name %s in class %s\n",
                method_name, class_name);
        exit(-1);
      }
    }
  }
  return cursor;
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
      cursor = parse_classes(cursor+2, end, classes);
      break;
    default:
      fprintf(stderr, "Invalid cha info at offset %lu\n", cursor - content);
      exit(-1);
    }
  }
  node *lcc = g_get_lcc(cha);
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
