#include <elf.h>
#include <tcb.h>
#include <mm.h>
#include <string.h>
#include <syscall.h>
#include <errno.h>
#include <stdarg.h>

/* the table region holding Bary and Tary */
void* table = 0;

/* auxiliary information, 15 in total as defined in the ABI */
#define AUX_CNT 30

typedef struct {
  int a_type;
  union {
    long a_val;
    void *a_ptr;
    void (*a_fnc)();
  } a_un;
} auxv_t;

static size_t aux[AUX_CNT] = {0};

static void init_aux(char **envp) {
  unsigned i;
  size_t *auxv;
  for (i = 0; envp[i]; i++); // advance envp

  auxv = (void*)(envp + i + 1);
                 
  for (i = 0; auxv[i]; i += 2) {
    if (auxv[i] < AUX_CNT)
      aux[auxv[i]] = auxv[i+1];
  }
}

static void alloc_sandbox(void) {
#if 0
  /* reserve memory from 64KB to 8GB, [4GB, 8GB) serves as a guard region. */
  
  void *ptr = mmap((void*)SixtyFourKB, FourGB * 2 - SixtyFourKB,
                   PROT_NONE, MAP_ANONYMOUS | MAP_FIXED | MAP_PRIVATE,
                   -1, 0);
  if (ptr != (void*)SixtyFourKB) {
    dprintf(STDERR_FILENO, "[alloc_sandbox] mmap failed\n", errn);
    quit(-1);
  }

  /* not each page is required, so we just give the kernel some hints
     to allocate physical pages as late as possible */
  if (0 != madvise(ptr, FourGB - SixtyFourKB, MADV_DONTNEED)) {
    dprintf(STDERR_FILENO, "[alloc_sandbox] madvise failed\n", errn);
    quit(-1);
  }
#endif
}

static void reserve_table_region(void) {
  /* reserve another 4GB memory region */
  table = mmap((void*)0, FourGB,
               PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE,
               -1, 0);
  if ((long)table < 0) {
    dprintf(STDERR_FILENO, "[reserve_table_region] mmap failed\n", errn);
    quit(-1);
  }

  /* not needed now, advise the kernel to map physical pages as late
     as possible */
  if (0 != madvise(table, FourGB, MADV_DONTNEED)) {
    dprintf(STDERR_FILENO, "[reserve_table_region] madvise failed\n", errn);
    quit(-1);
  }

  /* set %gs */
  if (0 != arch_prctl(ARCH_SET_GS, (unsigned long) table)) {
    dprintf(STDERR_FILENO, "[reserve_table_region] arch_prctl failed\n", errn);
    quit(-1);
  }
}

/**
 * The first page in the table region is populated with entries
 * to runtime functions.
 */
void install_trampolines(void) {
  struct trampolines {
    unsigned long mmap;
    unsigned long mprotect;
    unsigned long munmap;
    unsigned long brk;
    unsigned long sigaction;
    unsigned long readv;
    unsigned long mremap;
    unsigned long shmat;
    unsigned long clone;
    unsigned long execve;
    unsigned long shmdt;
    unsigned long remap_file_pages;
    unsigned long preadv;
    unsigned long process_vm_readv;
    unsigned long set_tcb;
    unsigned long load_native_code;
    unsigned long unload_native_code;
    unsigned long create_code_heap;
    unsigned long dyncode_install;
    unsigned long dyncode_modify;
    unsigned long dyncode_delete;
    unsigned long report_cfi_violation;
    unsigned long online_patch;
  } *tp = (struct trampolines*)table;
  extern unsigned long runtime_rock_mmap;
  extern unsigned long runtime_rock_mprotect;
  extern unsigned long runtime_rock_munmap;
  extern unsigned long runtime_rock_mremap;
  extern unsigned long runtime_rock_brk;
  extern unsigned long runtime_set_tcb;
  extern unsigned long runtime_load_native_code;
  extern unsigned long runtime_unload_native_code;
  extern unsigned long runtime_create_code_heap;
  extern unsigned long runtime_dyncode_install;
  extern unsigned long runtime_dyncode_modify;
  extern unsigned long runtime_dyncode_delete;
  extern unsigned long runtime_report_cfi_violation;
  extern unsigned long runtime_online_patch;
  tp->mmap = runtime_rock_mmap;
  tp->mprotect = runtime_rock_mprotect;
  tp->munmap = runtime_rock_munmap;
  tp->mremap = runtime_rock_mremap;
  tp->brk = runtime_rock_brk;
  tp->set_tcb = runtime_set_tcb;
  tp->load_native_code = runtime_load_native_code;
  tp->unload_native_code = runtime_unload_native_code;
  tp->create_code_heap = runtime_create_code_heap;
  tp->dyncode_install = runtime_dyncode_install;
  tp->dyncode_modify = runtime_dyncode_modify;
  tp->dyncode_delete = runtime_dyncode_delete;
  tp->report_cfi_violation = runtime_report_cfi_violation;
  tp->online_patch = runtime_online_patch;

  if (0 != mprotect(table,  PAGE_SIZE, PROT_READ)) {
    dprintf(STDERR_FILENO, "[install_trampolines] mprotect failed\n");
  }
}

void* load_libc(void) {
  return 0;
}

static void dump_stack(int argc, char **argv) {
  dprintf(STDERR_FILENO, "argc = %d\n", argc);
  int i;
  for (i = 0; i < argc; ++i) {
    dprintf(STDERR_FILENO, "argv[%d] = %s\n", i, argv[i]);
  }

  char **envp = argv + argc + 1;
  auxv_t *auxv;

  for (i = 0; envp[i]; i++)
    dprintf(STDERR_FILENO, "envp[%d] = %s\n", i, envp[i]);
  /* dprintf(STDERR_FILENO, "sizeof(auxv_t) = %d\n", sizeof(auxv_t)); */
  auxv = (auxv_t*)(envp + i + 1);

  for (i = 0; auxv[i].a_type; ++i) {
    dprintf(STDERR_FILENO, "aux[%d] = 0x%lx\n", auxv[i].a_type, (long)auxv[i].a_un.a_val);
    switch(auxv[i].a_type) {
    case AT_PLATFORM:
      dprintf(STDERR_FILENO, "aux[AT_PLATFORM] = %s\n", (char*)auxv[i].a_un.a_ptr);
      break;
    case AT_RANDOM:
      dprintf(STDERR_FILENO, "aux[AT_RANDOM] = 0x%lx%lx\n",
              ((unsigned long*)auxv[i].a_un.a_ptr)[0],
              ((unsigned long*)auxv[i].a_un.a_ptr)[1]);
      break;
    case AT_EXECFN:
      dprintf(STDERR_FILENO, "aux[AT_EXECFN] = %s\n", (char*)auxv[i].a_un.a_ptr);
      break;
    default:
      break;
    }
  }
}

static char* build_stack(int argc, char **argv, char* stack_top, const char* libc_base) {
  int envc = 0;
  char **envp = argv + argc + 1;
  int auxvc = 0, orig_auxvc = 0;
  auxv_t *auxv = 0;
  size_t stack_size = 0;

  char *top = stack_top;

  size_t array_area_size = 0;

  int i;

  stack_size += 8; /* for storing argc */
  array_area_size += 8;

  for (i = 0; i < argc; i++)
    stack_size += (strlen(argv[i]) + 1); /* each argv[i] length */

  stack_size += (argc + 1) * 8; /* for storing argv[argc] */

  array_area_size += (argc + 1) * 8;

  for (i = 0; envp[i]; i++)
    stack_size += (strlen(envp[i]) + 1); /* each envp[i] length */

  envc = i;

  stack_size += (envc + 1)* 8; /* for storing envp[] */

  array_area_size += (envc + 1) * 8;

  auxv = (auxv_t*)(envp + envc + 1);

  for (i = 0; auxv[i].a_type; ++i) {
    switch (auxv[i].a_type) {
    default: auxvc++; break;
      /* The rest aux vecs are not effective inside sandbox */
    case AT_PLATFORM:
      auxvc++;
      stack_size += (strlen((char*)auxv[i].a_un.a_ptr)+1);
      break;
    case AT_RANDOM:
      auxvc++;
      stack_size += 16; /* the kernel provides a 16-byte random value */
      break;
    case AT_EXECFN:
      auxvc++;
      stack_size += (strlen((char*)auxv[i].a_un.a_ptr)+1);
      break;
    case AT_SYSINFO_EHDR:
      break;
    }
  }

  orig_auxvc = i;

  stack_size += (auxvc + 1) * sizeof(auxv_t); /* auxiliary vector */

  array_area_size += (auxvc + 1) * sizeof(auxv_t);

  stack_size = (stack_size + 15) / 16 * 16;/* ABI requires stack pointer to 16-byte aligned */

  top -= stack_size;

  /* zero fill the stack */
  memset(top, 0, stack_size);

  /* all strings are written to pool */
  char* pool = top + array_area_size;

#define writeval(val) do {                      \
    *(unsigned long*)top = (unsigned long)val;  \
    top += 8;                                   \
  } while (0);

#define copystr(str) do {                       \
    size_t len = strlen(str);                   \
    memcpy(pool, str, len);                     \
    pool += (len + 1);                          \
  } while (0);

  /* write argc */
  writeval(argc);
  /* write argv */
  for (i = 0; i < argc; i++) {
    writeval(pool);
    copystr(argv[i]);
  }
  /* write a null value */
  writeval(0);
  /* write envp */
  for (i = 0; i < envc; i++) {
    writeval(pool);
    copystr(envp[i]);
  }
  /* write a null value */
  writeval(0);
  /* write the aux vector */
  for (i = 0; i < orig_auxvc; i++) {
    switch (auxv[i].a_type) {
    default:
      writeval(auxv[i].a_type);
      writeval(auxv[i].a_un.a_val);
      break;
    case AT_SYSINFO_EHDR: /* linux vdso */
    case AT_HWCAP: /* cpu capability mask */
      break;
    case AT_BASE:
      writeval(auxv[i].a_type);
      writeval(libc_base);
      break;
    case AT_RANDOM:
      writeval(auxv[i].a_type);
      writeval(pool);
      ((unsigned long*)pool)[0] = ((unsigned long*)auxv[i].a_un.a_ptr)[0];
      ((unsigned long*)pool)[1] = ((unsigned long*)auxv[i].a_un.a_ptr)[1];
      pool += 16;
      break;
    case AT_PLATFORM:
    case AT_EXECFN:
      writeval(auxv[i].a_type);
      writeval(pool);
      copystr((char*)auxv[i].a_un.a_ptr);
      break;
    }
  }
#undef writeval
#undef copystr

  return stack_top - stack_size;
}

void* stack_init(int argc, char **argv, const char* libc_base) {
  size_t sz = 4906 * 2048;
  void* stack = mmap(0, sz, PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_32BIT, -1, 0);
  if (MAP_FAILED == stack) {
    dprintf(STDERR_FILENO, "[stack_init] failed\n");
    quit(-1);
  }
  stack += sz-8;
  dprintf(STDERR_FILENO, "Stack_init\n");
  stack = build_stack(argc, argv, stack, libc_base);
  argc = ((unsigned long*)stack)[0];
  argv = (char**)&((unsigned long*)stack)[1];
  dump_stack(argc, argv);
  return stack;
}

/* main function of the runtime */
void* runtime_init(int argc, char **argv) {
  dump_stack(argc, argv);
  /* initialize the aux vector */
  char **envp = argv + argc + 1;
  init_aux(envp);

  /* initialize the spin lock if needed */

  /* reserve 4GB memory for running the user program */
  alloc_sandbox();

  /* reserve another 4GB memory for Bary and Tary,
     and set %gs
  */
  reserve_table_region();

  /* install the trampolines through which the sandboxed
     application can escape the sandbox securely */
  install_trampolines();

  /* set up the trusted thread-control block */
  alloc_tcb();
  
  /* load the libc */
  return load_libc();
}
