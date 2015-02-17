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

/**
 * In a char stream pointed to by "cursor", find the next symbol "sym".
 * If a NULL-byte ('\0') is encountered, set stop to be TRUE. Return
 * the distance from cursor to the symbol.
 */
static size_t next_symbol(char *cursor, char sym, /*out*/int *stop) {
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

static void print_string(const void *str) {
  printf("%s", (const char*)str);
}

static void cha_cc_print(graph *g) {
  dict_print(g, print_string, print_cc);
}

static char *_get_string_before_symbol(/*in/out*/char **cursor, char symbol,
                                       /*out*/int *stop, /*out*/size_t *len) {
  size_t name_len = next_symbol(*cursor, symbol, stop);

  char *string = *cursor;

  *cursor += name_len;

  **cursor = '\0'; /* patch the symbol so that 'string' is a valid string */

  *cursor += 1;

  if (len) *len = name_len;
  
  return string;
}

/**
 * Intern the string in the string pool, and return the interned string.
 */
static char *sp_intern_string(char *string) {
  return sp_add_cpy_or_nothing(&sp, string);
}

static char *parse_inheritance(char *cursor, const char* end,
                               /*out*/graph **cha) {
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

  int counter = 5; /* only cosv qualifiers are possible */

  for (--len; len > 0 && method_name[len] != '@' && counter > 0;
       --len, --counter) {
    switch (method_name[len]) {
    case 'c': attrs |= CONSTANT; break;
    case 'o': attrs |= VOLATILE; break;
    case 's': attrs |= STATIC;   break;
    case 'v': attrs |= VIRTUAL;  break;
    }
  }

  /* patch the byte so that we can later manipulate the name without any copy */
  if (method_name[len] == '@') {
    method_name[len] = '\0';
    /* only if there really are type qualifiers can we return the attrs*/
    return attrs;
  }
  return 0;
}

static int is_constant(unsigned char attrs) { return attrs & CONSTANT; }
static int is_volatile(unsigned char attrs) { return attrs & VOLATILE; }
static int is_static(unsigned char attrs)   { return attrs & STATIC;   }
static int is_virtual(unsigned char attrs)  { return attrs & VIRTUAL;  }

static char *parse_classes(char *cursor, const char *end,
                           /*out*/dict **classes) {
  int stop;

  char *class_name =
    sp_intern_string(_get_string_before_symbol(&cursor, '#', &stop, 0));

  keyvalue *class_entry = dict_find(*classes, class_name);

  if (!class_entry)
    class_entry = dict_add(classes, class_name, 0);

  while (!stop) {
    size_t len;

    char *method_name = _get_string_before_symbol(&cursor, '#', &stop, &len);

    unsigned char attrs = _get_cpp_method_attr(method_name, len);

    method_name = sp_intern_string(method_name);

    keyvalue *method_entry = dict_find((dict*)class_entry->value, method_name);

    if (!method_entry) {
      dict_add((dict**)&(class_entry->value), method_name,
               (void*)(unsigned long)attrs); /* suppress compiler warning */
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
               /*out*/dict **classes, /*out*/graph **cha) {
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
}

/**
 * parse indirect control-flow transfer metadata.
 */
void parse_icfs(char *content, const char *end, /*out*/icf **icfs) {
  assert(content && end && icfs);

  char *cursor = content;
  
  while (cursor < end) {
    int stop;

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
          if (*co == 'c') ic->attrs = CONSTANT;
          else            ic->attrs = VOLATILE;
        }

        ic->type =
          sp_intern_string(_get_string_before_symbol(&cursor, '#', &stop, 0));

        assert(stop);

        ic->ity = PointerToMethodCall;
        ic->class_name = class_name;
      }
      break;
    case 'N':
      {
        ic->type =
          sp_intern_string(_get_string_before_symbol(&cursor, '#', &stop, 0));

        assert(stop);

        ic->ity = NormalCall;
      }
      break;
    default:
      fprintf(stderr, "ICF parsing failed at %lu\n", cursor - content);
      exit(-1);
      break;
    }
  }
}

/**
 * parse functions whose addresses are taken.
 */
void parse_fats(char *content, const char *end, /*out*/dict **fats) {
  assert(content && end && fats);

  char *cursor = content;

  int stop;

  while (cursor < end) {
    char *fat =
      sp_intern_string(_get_string_before_symbol(&cursor, '\0', &stop, 0));

    assert(stop);

    keyvalue *kv = dict_find(*fats, fat);

    if (!kv)
      dict_add(fats, fat, 0);
  }
}

/**
 * Function aliases.
 */
void parse_aliases(char *content, const char *end, /*out*/graph **aliases) {
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
 * split a char stream delimited by "local_symbol" and add the splitted
 * content to node_list.
 */
static void _add_node_list(char *cursor, char local_symbol, /*out*/node **node_list) {
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
        _add_node_list(local_cursor, ' ', &(f->dtails));
        break;
      case 'I':
        cursor += 2;
        local_cursor = _get_string_before_symbol(&cursor, '\n', &stop, 0);
        _add_node_list(local_cursor, ' ', &(f->itails));
        break;
      case 'R':
        cursor += 2;
        local_cursor = _get_string_before_symbol(&cursor, '\n', &stop, 0);
        _add_node_list(local_cursor, ' ', &(f->returns));
        break;
      case '}':
        cursor += 2;
        DL_APPEND(*functions, f);
        f = 0;
        stop = TRUE;
        break;
      default:
        fprintf(stderr, "Functions parsing failed at %lu\n", cursor - content);
        exit(-1);
      }
    }
  }
}

/**
 * Test whether a function is a C++ instance method (non-static member method),
 * and set the attributes if it is.
 */
static int _is_instance_method(const function *f, dict *classes,
                              /*out*/unsigned char *attrs) {
  if (classes && f->class_name && f->method_name) {
    keyvalue *class_entry = dict_find(classes, f->class_name);

    if (!class_entry)
      return FALSE;

    keyvalue *method_entry = dict_find((dict*)(class_entry->value), f->method_name);

    if (!method_entry)
      return FALSE;

    if (attrs)
      *attrs = (unsigned char)method_entry->value;

    return !is_static((unsigned char)method_entry->value);
  }
  return FALSE;
}

/* mark a pointer to an indirect branch */
static void* _mark_ptr(void *ptr) {
  unsigned long p = (unsigned long)ptr;
  p |= 0x8000000000000000UL;
  return (void*)p;
}

/* unmark a pointer */
static void* _unmark_ptr(void *ptr) {
  unsigned long p = (unsigned long)ptr;
  p &= 0x7FFFFFFFFFFFFFFFUL;
  return (void*)p;
};

/* check if a pointer is marked */
static int _is_marked(const void *ptr) {
  return !!((unsigned long)ptr & 0x8000000000000000UL);
}

/**
 * Based on the class hierarchy, find the relations of all virtual
 * methods and output the relations into the call graph.
 */
static void _build_cha_relations(/*out*/graph **callgraph,
                                 dict *classes, graph *cha,
                                 dict *all_virtual_funcs_grouped_by_cls_mtd_name) {
  node *cha_lcc = g_get_lcc(&cha); /* class groups associated by inheritance */
  graph *explored = 0;
  node *n, *ntmp;
  graph *v, *tmp;

  /* for each class inheritance group */
  DL_FOREACH_SAFE(cha_lcc, n, ntmp) {
    /* for each class in the inheritance group */
    HASH_ITER(hh, (vertex*)(n->val), v, tmp) {
      /* for each method of this class, link it to other methods
       * of other classes */
      keyvalue *class_entry = dict_find(classes, v->key);

      if (class_entry) {
        keyvalue *method_entry, *mtmp;

        HASH_ITER(hh, (vertex*)(class_entry->value), method_entry, mtmp) {

          /* only virtual methods are considered */
          if (is_virtual((unsigned char)(method_entry->value)) &&
              !g_directed_edge_in(explored,
                                  class_entry->key,
                                  method_entry->key)) {
            dict *eqc = 0;
            graph *vc, *vctmp;

            HASH_ITER(hh, (vertex*)(n->val), vc, vctmp) {

              keyvalue *i_class_entry = dict_find(classes, vc->key);

              if (i_class_entry) {
                keyvalue *i_method_entry =
                  dict_find((vertex*)(i_class_entry->value), method_entry->key);

                if (i_method_entry && /* this class contains this method */
                    /* this method is virtual */
                    is_virtual((unsigned char)i_method_entry->value)&&
                    /* not explored yet */
                    !g_directed_edge_in(explored, i_class_entry->key, i_method_entry->key)) {
                  g_add_directed_edge(&explored, i_class_entry->key, i_method_entry->key);

                  keyvalue *virtual_method_entry =
                    g_find_l2_vertex(all_virtual_funcs_grouped_by_cls_mtd_name,
                                     i_class_entry->key, i_method_entry->key);

                  if (virtual_method_entry) {
                    keyvalue *method_name, *tmpkv;

                    HASH_ITER(hh, (dict*)(virtual_method_entry->value),
                              method_name, tmpkv) {
                      dict_add(&eqc, method_name->key, 0);
                    }
                  }
                }
              }
            }
            /* add the eqc into the callgraph, eqc will also be freed */
            g_add_cc_to_g(callgraph, eqc);
          }
        }
      }
    }
    /* clean the node in cha_lcc*/
    DL_DELETE(cha_lcc, n);
    dict_clear((dict**)(&(n->val)));
    free(n);
  }
}

/**
 * Based on the CFG-related meta information, build a call graph.
 */
graph *build_callgraph(icf *icfs, function *functions,
                       dict *classes, graph *cha,
                       dict *fats, graph *aliases) {
  graph *callgraph = 0; /* call graph */
  graph *chacc = 0;     /* cha cache */
  dict *all_funcs_grouped_by_name = 0;
  dict *all_virtual_funcs_grouped_by_cls_mtd_name = 0;
  dict *global_funcs_grouped_by_types = 0;   /* global or static member functions */
  dict *instance_funcs_grouped_by_types = 0; /* non-static and non-virtual methods */
  dict *instance_funcs_grouped_by_const_types = 0;
  dict *instance_funcs_grouped_by_volatile_types = 0;
  dict *instance_funcs_grouped_by_const_volatile_types = 0;
  function *f;
  int virtual;
  int instance;
  unsigned char attrs;
  
  /* Compute aliases transitive closure */
  graph *aliases_tc = g_transitive_closure(&aliases);

  DL_FOREACH(functions, f) {
    g_add_directed_edge(&all_funcs_grouped_by_name, f->name, f);

    instance = _is_instance_method(f, classes, &attrs);

    if (is_virtual(attrs)) {
      g_add_directed_l2_edge(&all_virtual_funcs_grouped_by_cls_mtd_name,
                             f->class_name, f->method_name, f->name);
      keyvalue *alias_entry = dict_find(aliases_tc, f->name);
      if (alias_entry) {
        keyvalue *v, *tmp;

        HASH_ITER(hh, (vertex*)(alias_entry->value), v, tmp) {
          g_add_directed_l2_edge(&all_virtual_funcs_grouped_by_cls_mtd_name,
                                 f->class_name, f->method_name, v->key);
        }
      }
    }

    if (dict_in(fats, f->name) && f->type) {
      if (!instance) {/* global or static member functions */
        g_add_directed_edge(&global_funcs_grouped_by_types,
                            f->type, f->name);
      } else {
        if (!is_constant(attrs) && !is_volatile(attrs)) {
          g_add_directed_edge(&instance_funcs_grouped_by_types,
                              f->type, f->name);
          /*
           * Some destructors are aliased to others, and we should take care
           * of all alises
           */
          keyvalue *alias_entry = dict_find(aliases_tc, f->name);
          if (alias_entry) {
            keyvalue *v, *tmp;

            HASH_ITER(hh, (vertex*)(alias_entry->value), v, tmp) {
              g_add_directed_edge(&instance_funcs_grouped_by_types,
                                  f->type, v->key);
            }
          }
        } else if (is_constant(attrs) && !is_volatile(attrs))
          g_add_directed_edge(&instance_funcs_grouped_by_const_types,
                              f->type, f->name);
        else if (!is_constant(attrs) && is_volatile(attrs))
          g_add_directed_edge(&instance_funcs_grouped_by_volatile_types,
                              f->type, f->name);
        else
          g_add_directed_edge(&instance_funcs_grouped_by_const_volatile_types,
                              f->type, f->name);
      }
    }
  }

  /* get the list of inheritance relationship */
  _build_cha_relations(&callgraph, classes, cha,
                       all_virtual_funcs_grouped_by_cls_mtd_name);
  
  /* for each indirect branch, find its targets */
  icf *ic, *tmpic;

  HASH_ITER(hh, icfs, ic, tmpic) {
    if (ic->ity == VirtualMethodCall) {

      keyvalue *virtual_method_entry =
        g_find_l2_vertex(all_virtual_funcs_grouped_by_cls_mtd_name,
                         ic->class_name, ic->method_name);

      if (virtual_method_entry) {
        keyvalue *method_name, *tmpkv;

        HASH_ITER(hh, (dict*)(virtual_method_entry->value),
                  method_name, tmpkv) {
          g_add_edge(&callgraph, _mark_ptr(ic->id), method_name->key);
        }
      }
    } else if (ic->ity == PointerToMethodCall) {
      dict *g = 0;

      if (!is_constant(ic->attrs) && !is_volatile(ic->attrs))
        g = instance_funcs_grouped_by_types;
      else if (is_constant(ic->attrs) && !is_volatile(ic->attrs))
        g = instance_funcs_grouped_by_const_types;
      else if (!is_constant(ic->attrs) && is_volatile(ic->attrs))
        g = instance_funcs_grouped_by_volatile_types;
      else
        g = instance_funcs_grouped_by_const_volatile_types;

      keyvalue *typed_methods = dict_find(g, ic->type);

      if (typed_methods) {
        keyvalue *v, *tmp;
        HASH_ITER(hh, (dict*)(typed_methods->value), v, tmp) {
          g_add_edge(&callgraph, _mark_ptr(ic->id), v->key);
        }
      }
    } else {
      keyvalue *funcs_with_same_type = dict_find(global_funcs_grouped_by_types, ic->type);
      keyvalue *v, *tmp;

      if (funcs_with_same_type) {
        HASH_ITER(hh, (dict*)(funcs_with_same_type->value), v, tmp) {
          g_add_edge(&callgraph, _mark_ptr(ic->id), v->key);
        }
      }
    }
  }

  /* free the transitive closure of aliases */
  g_free_transitive_closure(&aliases_tc);
  return callgraph;
}

static char *acquire_file_content(const char *file_name, size_t *len) {
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

static void release_file_content(char *buf, size_t len) {
  munmap(buf, (len + 4095)/4096*4096);
}

static void print_node_val(void *nval) {
  vertex *g = (vertex*)nval;
  vertex *v, *tmp;
  HASH_ITER(hh, g, v, tmp) {
    if (_is_marked(v->key))
      printf("%s ", (char*)_unmark_ptr(v->key));
    else
      printf("%s ", (char*)(v->key));
  }
  printf("\n");
}

int main(int argc, char **argv) {
  size_t len;
  char *buf;
  buf = acquire_file_content("xalan.cha", &len);
  dict *classes = 0;      /* cpp classes */
  graph *cha = 0;
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

  graph *callgraph =
    build_callgraph(icfs, functions, classes, cha, fats, aliases);
  node *lcg = g_get_lcc(&callgraph);
  int count;
  node *n;
  DL_COUNT(lcg, n, count);
  fprintf(stderr, "%d\n", count);
  //l_print(lcg, print_node_val);
}
