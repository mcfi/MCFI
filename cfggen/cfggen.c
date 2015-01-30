#include <stdlib.h>
#include <string.h>
#ifdef STANDALONE
#include <stdio.h>
#endif

typedef struct code_module_t cpp_module;
typedef struct function_t function;
typedef struct icf_t ict;
typedef struct cpp_class_t cpp_class;

/**
 * A code module may be an executable or a .so file.
 */
struct code_module_t {
  char *path; // absolute path of the code module
  uintptr_t addr; // in-memory address
};

struct cpp_class_t {
  char *name;
  // set of methods
};

struct icf_t {
  code_module *mod;
  char *id;
  size_t offset; // offset of the bid mov instruction's patch point in the code module
};

struct function_t {
  code_module *mod;     // defined in which code module
  cpp_class *cls;       // if this function is defined as a cpp class method
  char *type;           // type of the function
  char *mangled_name;
  char *demangled_name; // if this function is a cpp method, then demangled name
                        // should point to the demangled name.
  size_t offset; // offset in the code module
  int cons:1; // Constant
  int volt:1; // vOlatile
  int stat:1; // Static
  int virt:1; // Virtual
  // list of returns
  // list of direct tail calls
  // list of indirect tail calls
};
