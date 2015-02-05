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
  ++counter;
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
  char *string = *cursor;
  *cursor += name_len;
  **cursor = '\0'; /* patch the symbol so that 'string' is a valid string */
  *cursor += 1;
  if (len)
    *len = name_len;
  
  return string;
}

static char *sp_intern_string(char *string) {
  return sp_add_cpy_or_nothing(&sp, string);
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
      attrs &= CONSTANT;
      break;
    case 'o':
      attrs &= VOLATILE;
      break;
    case 's':
      attrs &= STATIC;
      break;
    case 'v':
      attrs &= VIRTUAL;
      break;
    }
  }
  if (method_name[len] == '@')
    method_name[len] = '\0';
  return attrs;
}

static int isConstant(unsigned char attrs) {
  return attrs & CONSTANT;
}

static int isVolatile(unsigned char attrs) {
  return attrs & VOLATILE;
}

static int isStatic(unsigned char attrs) {
  return attrs & STATIC;
}

static int isVirtual(unsigned char attrs) {
  return attrs & VIRTUAL;
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
    if (!method_entry) {
      dict_add((dict**)&(class_entry->value), method_name, (void*)attrs);
    } else {
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

void parse_icfs(char *content, const char *end, icf **icfs) {
  assert(content && end && icfs);
  char *cursor = content;
  while (cursor < end) {
    int stop = 0;
    char *id = _get_string_before_symbol(&cursor, '#', &stop, 0);
    if (0 != sp_str_handle(&sp, id)) {
      fprintf(stderr, "Non-unique id, %s\n", id);
      exit(-1);
    }
    id = sp_intern_string(id);
    icf *ic = alloc_icf(CALL);
    ic->id = id;
    ic->offset = 0;
    HASH_ADD_PTR(*icfs, id, ic);
    cursor += 2;
    switch (*(cursor-2)) {
    case 'V':
      {
        char *class_name =
          sp_intern_string(_get_string_before_symbol(&cursor, '#', &stop, 0));
        char *method_name =
          sp_intern_string(_get_string_before_symbol(&cursor, '#', &stop, 0));
        assert(stop);
        ic->ity = VirtualMethodCall;
        ic->class_name = class_name;
        ic->method_name = method_name;
      }
      break;
    case 'D':
      {
        char *class_name =
          sp_intern_string(_get_string_before_symbol(&cursor, '#', &stop, 0));
        assert(stop);
        char *method_name =
          sp_intern_string("~");
        assert(stop);
        ic->ity = VirtualMethodCall;
        ic->class_name = class_name;
        ic->method_name = method_name;
      }
      break;
    case 'P':
      {
        char *class_name =
          sp_intern_string(_get_string_before_symbol(&cursor, '#', &stop, 0));
        size_t len;
        char *co = _get_string_before_symbol(&cursor, '#', &stop, &len);
        if (len == 2)
          ic->attrs = CONSTANT | VOLATILE;
        else if (len == 1) {
          if (*co == 'c')
            ic->attrs = CONSTANT;
          else
            ic->attrs = VOLATILE;
        }
        char *type =
          sp_intern_string(_get_string_before_symbol(&cursor, '#', &stop, 0));
        assert(stop);
        ic->ity = PointerToMethodCall;
        ic->class_name = class_name;
        ic->type = type;
      }
      break;
    case 'N':
      {
        char *type =
          sp_intern_string(_get_string_before_symbol(&cursor, '#', &stop, 0));
        assert(stop);
        ic->ity = NormalCall;
      }
      break;
    default:
      break;
    }
  }
}

/**
 * Functions whose addresses are taken.
 */
void parse_fats(char *content, const char *end, dict **fats) {
  assert(content && end && fats);
  char *cursor = content;
  int stop;
  while (cursor < end) {
    char *fat = sp_intern_string(_get_string_before_symbol(&cursor, '\0', &stop, 0));
    assert(stop);
    keyvalue *kv = dict_find(*fats, fat);
    if (!kv)
      dict_add(fats, fat, 0);
  }
}

/**
 * Function aliases.
 */
void parse_aliases(char *content, const char *end, graph **aliases) {
  assert(content && end && aliases);
  char *cursor = content;
  int stop;
  while (cursor < end) {
    char *name = sp_intern_string(_get_string_before_symbol(&cursor, ' ', &stop, 0));
    char *aliasee = sp_intern_string(_get_string_before_symbol(&cursor, ' ', &stop, 0));
    assert(stop);
    g_add_edge(aliases, name, aliasee);
  }
}

/**
 * Functions
 */
static void _add_node_list(node **node_list, char *cursor, char local_symbol) {
  int stop;
  do {
    char *name =
      sp_intern_string(_get_string_before_symbol(&cursor, local_symbol, &stop, 0));
    DL_APPEND(*node_list, new_node(name));
  } while (!stop);
}

void parse_functions(char *content, const char *end, function **functions) {
  assert(content && end && functions);
  char *cursor = content;
  char *local_cursor = 0;
  function *f = 0;
  int stop;
  while (cursor < end) {
    stop = FALSE;
    while (!stop) {
      switch (*cursor) {
      case '{':
        cursor += 2;
        f = alloc_function();
        f->name = sp_intern_string(_get_string_before_symbol(&cursor, '\n', &stop, 0));
        break;
      case 'N':
        cursor += 2;
        f->class_name =
          sp_intern_string(_get_string_before_symbol(&cursor, '#', &stop, 0));
        f->method_name =
          sp_intern_string(_get_string_before_symbol(&cursor, '\n', &stop, 0));
        break;
      case 'D':
        cursor += 2;
        f->class_name =
          sp_intern_string(_get_string_before_symbol(&cursor, '\n', &stop, 0));
        f->method_name = sp_intern_string("~");
        break;
      case 'Y':
        cursor += 2;
        f->type =
          sp_intern_string(_get_string_before_symbol(&cursor, '\n', &stop, 0));
        break;
      case 'T':
        cursor += 2;
        local_cursor = _get_string_before_symbol(&cursor, '\n', &stop, 0);
        _add_node_list(&(f->dtails), local_cursor, ' ');
        break;
      case 'I':
        cursor += 2;
        local_cursor = _get_string_before_symbol(&cursor, '\n', &stop, 0);
        _add_node_list(&(f->itails), local_cursor, ' ');
        break;
      case 'R':
        cursor += 2;
        local_cursor = _get_string_before_symbol(&cursor, '\n', &stop, 0);
        _add_node_list(&(f->returns), local_cursor, ' ');
        break;
      case '}':
        cursor += 2;
        DL_APPEND(*functions, f);
        f = 0;
        stop = TRUE;
        break;
      default:
        fprintf(stderr, "Unrecognized symbol: %s\n", cursor);
        exit(-1);
      }
    }
  }
}

char *acquire_file_content(const char *file_name, size_t *len) {
  struct stat statbuf;
  stat(file_name, &statbuf);
  *len = statbuf.st_size;
  int fd = open(file_name, O_RDONLY);
  assert(fd > -1);
  char *buf = mmap(0, (*len + 4095)/4096*4096,
                   PROT_READ | PROT_WRITE, MAP_PRIVATE,
                   fd, 0);
  assert(buf != (char*)-1);
  close(fd);
  return buf;
}

void release_file_content(char *buf, size_t len) {
  munmap(buf, (len + 4095)/4096*4096);
}

int main(int argc, char **argv) {
  size_t len;
  char *buf;
  buf = acquire_file_content("xalan.cha", &len);
  parse_cha(buf, buf + len, &classes, &cha);
  release_file_content(buf, len);

  buf = acquire_file_content("xalan.ic", &len);
  icf *icfs = 0;
  parse_icfs(buf, buf + len, &icfs);
  release_file_content(buf, len);

  buf = acquire_file_content("xalan.at", &len);
  dict *fats = 0;
  parse_fats(buf, buf + len, &fats);
  release_file_content(buf, len);

  buf = acquire_file_content("xalan.alias", &len);
  graph *aliases = 0;
  parse_aliases(buf, buf + len, &aliases);
  release_file_content(buf, len);

  buf = acquire_file_content("xalan.func", &len);
  function *functions = 0;
  parse_functions(buf, buf + len, &functions);
  release_file_content(buf, len);
}
