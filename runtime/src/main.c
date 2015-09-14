#include <elf.h>
#include <tcb.h>
#include <mm.h>
#include <io.h>
#include <string.h>
#include <syscall.h>
#include <errno.h>
#include <stdarg.h>
#include <cfggen/cfggen.h>
#include "pager.h"
#include <time.h>

#define MAX_PATH 256

/* x64 Linux loads images from 0x400000 */
#define X64ABIBASE 0x400000UL

/* size of the plt entry */
#define PLT_ENT_SIZE 32

/* the table region holding Bary and Tary */
void* table = 0;

/* after we load libc, we set the following data */
char* libc_base = 0;
char* libc_entry = 0;
char* stack = 0;

/* load-time data */
char **lt_argv = 0;
int lt_argc = 0;

char **lt_envp = 0;
int lt_envc = 0;

/* cfg generation data */
code_module *modules = 0; /* code modules */
str *stringpool = 0;
dict *vtabletaken = 0;

/* default SDK path */
const char *MCFI_SDK = 0;
const char *HOME = 0;
const char MCFI_SDK_NAME[] = "MCFI_SDK";


#ifdef NOCFI
static char two_byte_nop[]   = {0x66, 0x90};
static char three_byte_nop[] = {0x0f, 0x1f, 0x00};
static char four_byte_nop[]  = {0x0f, 0x1f, 0x40, 0x00};
static char five_byte_nop[]  = {0x0f, 0x1f, 0x44, 0x00, 0x00};
static char six_byte_nop[]   = {0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00};
static char nine_byte_nop[]  = {0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00};
#endif

int snprintf(char *str, size_t size, const char *format, ...);

auxv_t *lt_auxv = 0;
int lt_auxc = 0;

#define AUX_CNT 38

size_t aux[AUX_CNT];
            
size_t lt_stack_size = 0;
size_t lt_array_area_size = 0;

struct Vmmap VM;

typedef Elf64_Ehdr Ehdr;
typedef Elf64_Phdr Phdr;
typedef Elf64_Sym Sym;

static int _prot(int x) {
  int prot = PROT_NONE;
  if (x & PF_X)
    prot |= PROT_EXEC;
  if (x & PF_W)
    prot |= PROT_WRITE;
  if (x & PF_R)
    prot |= PROT_READ;
  return prot;
}

static void alloc_sandbox(void) {
  /* [4GB, 8GB) serves as a guard region. */
  uintptr_t start;
  uintptr_t end;
  
  void *ptr = mmap((void*)FourGB, FourGB,
                   PROT_NONE, MAP_ANONYMOUS | MAP_FIXED | MAP_PRIVATE,
                   -1, 0);
  if (ptr != (void*)FourGB) {
    dprintf(STDERR_FILENO, "[alloc_sandbox] guard region alloc failed with %d\n", errn);
    quit(-1);
  }

  /*
   * [0, 0x400000) is unmapped according to the ABI, but he first 64KB is
   * guaranteed not to be mapped by the kernel.
   */
  start = 0;
  end = X64ABIBASE;
  VmmapAdd(&VM, start >> PAGESHIFT, (end-start) >> PAGESHIFT,
           PROT_NONE, PROT_NONE, VMMAP_ENTRY_ANONYMOUS);

  /* allocate the stack, default to 8MB */
  start = FourGB - SixtyFourKB * 128;
  end = FourGB;
  stack = mmap((void*)start, end - start,
               PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_FIXED | MAP_PRIVATE,
               -1, 0);
  if (stack != (void*)start) {
    dprintf(STDERR_FILENO, "[alloc_sandbox] guard region alloc failed with %d\n", errn);
    quit(-1);
  }
  stack = (void*)(end-8);
  VmmapAdd(&VM, start >> PAGESHIFT, (end-start) >> PAGESHIFT,
           PROT_READ | PROT_WRITE, PROT_READ | PROT_WRITE,
           VMMAP_ENTRY_ANONYMOUS);
  /* VmmapDebug(&VM, "VM dump\n"); */
}

static void reserve_table_region(void) {
  /* reserve another 4GB memory region */
  table = mmap((void*)0, FourGB,
               PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE,
               -1, 0);
  if ((long)table < 0) {
    dprintf(STDERR_FILENO, "[reserve_table_region] mmap failed with %d\n", errn);
    quit(-1);
  }

  /* not needed now, advise the kernel to map physical pages as late
     as possible */
  if (0 != madvise(table, FourGB, MADV_DONTNEED)) {
    dprintf(STDERR_FILENO, "[reserve_table_region] madvise failed with %d\n", errn);
    quit(-1);
  }

  /* set %gs */
  if (0 != arch_prctl(ARCH_SET_GS, (unsigned long) table)) {
    dprintf(STDERR_FILENO, "[reserve_table_region] arch_prctl failed with %d\n", errn);
    quit(-1);
  }
}

/**
 * The first page in the table region is populated with entries
 * to runtime functions.
 */
void install_trampolines(void) {
  /* the first 64KB are used for catching NULL-dereference */
  char *tramp_page = table + 0x10000;
  struct trampolines {
    void *mmap;
    void *mprotect;
    void *munmap;
    void *brk;
    void *sigaction;
    void *readv;
    void *mremap;
    void *shmat;
    void *clone;
    void *execve;
    void *shmdt;
    void *remap_file_pages;
    void *preadv;
    void *process_vm_readv;
    void *set_tcb;
    void *allocset_tcb;
    void *free_tcb;
    void *load_native_code;
    void *gen_cfg;
    void *unload_native_code;
    void *create_code_heap;
    void *code_heap_fill;
    void *dyncode_modify;
    void *dyncode_delete;
    void *report_cfi_violation;
    void *patch_call;
    void *take_addr_and_gen_cfg;
    void *set_gotplt;
    void *fork;
    void *collect_stat;
    void *reg_cfg_metadata;
    void *delete_code;
    void *move_code;
    void *patch_at;
    void *patch_entry;
  } *tp = (struct trampolines*)(tramp_page);
  extern unsigned long runtime_rock_mmap;
  extern unsigned long runtime_rock_mprotect;
  extern unsigned long runtime_rock_munmap;
  extern unsigned long runtime_rock_mremap;
  extern unsigned long runtime_rock_brk;
  extern unsigned long runtime_set_tcb;
  extern unsigned long runtime_allocset_tcb;
  extern unsigned long runtime_free_tcb;
  extern unsigned long runtime_load_native_code;
  extern unsigned long runtime_gen_cfg;
  extern unsigned long runtime_unload_native_code;
  extern unsigned long runtime_create_code_heap;
  extern unsigned long runtime_code_heap_fill;
  extern unsigned long runtime_dyncode_modify;
  extern unsigned long runtime_dyncode_delete;
  extern unsigned long runtime_report_cfi_violation;
  extern unsigned long runtime_patch_call;
  extern unsigned long runtime_patch_at;
  extern unsigned long runtime_patch_entry;
  extern unsigned long runtime_take_addr_and_gen_cfg;
  extern unsigned long runtime_set_gotplt;
  extern unsigned long runtime_rock_fork;
  extern unsigned long runtime_collect_stat;
  extern unsigned long runtime_create_code_heap;
  extern unsigned long runtime_reg_cfg_metadata;
  extern unsigned long runtime_delete_code;
  extern unsigned long runtime_move_code;

  tp->mmap = &runtime_rock_mmap;
  tp->mprotect = &runtime_rock_mprotect;
  tp->munmap = &runtime_rock_munmap;
  tp->mremap = &runtime_rock_mremap;
  tp->brk = &runtime_rock_brk;
  tp->set_tcb = &runtime_set_tcb;
  tp->allocset_tcb = &runtime_allocset_tcb;
  tp->free_tcb = &runtime_free_tcb;
  tp->load_native_code = &runtime_load_native_code;
  tp->gen_cfg = &runtime_gen_cfg;
  tp->unload_native_code = &runtime_unload_native_code;
  tp->create_code_heap = &runtime_create_code_heap;
  tp->code_heap_fill = &runtime_code_heap_fill;
  tp->dyncode_modify = &runtime_dyncode_modify;
  tp->dyncode_delete = &runtime_dyncode_delete;
  tp->report_cfi_violation = &runtime_report_cfi_violation;
  tp->patch_call = &runtime_patch_call;
  tp->take_addr_and_gen_cfg = &runtime_take_addr_and_gen_cfg;
  tp->set_gotplt = &runtime_set_gotplt;
  tp->fork = &runtime_rock_fork;
  tp->collect_stat = &runtime_collect_stat;
  tp->create_code_heap = &runtime_create_code_heap;
  tp->reg_cfg_metadata = &runtime_reg_cfg_metadata;
  tp->delete_code = &runtime_delete_code;
  tp->move_code = &runtime_move_code;
  tp->patch_at = &runtime_patch_at;
  tp->patch_entry = &runtime_patch_entry;

  /* set the first 68KB read-only */
  if (0 != mprotect(table,  BID_SLOT_START, PROT_READ)) {
    dprintf(STDERR_FILENO, "[install_trampolines] mprotect failed %d\n", errn);
  }
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
  auxv = (auxv_t*)(envp + i + 1);

  for (i = 0; auxv[i].a_type; ++i) {    
    switch(auxv[i].a_type) {
    case AT_BASE:
      dprintf(STDERR_FILENO, "aux[AT_BASE] = 0x%lx\n", auxv[i].a_un.a_val);
      break;
    case AT_CLKTCK:
      dprintf(STDERR_FILENO, "aux[AT_CLKTCK] = %ld\n", auxv[i].a_un.a_val);
      break;
    case AT_DCACHEBSIZE:
      dprintf(STDERR_FILENO, "aux[AT_DCACHESIZE] = %d\n", (int)auxv[i].a_un.a_val);
      break;
    case AT_EGID:
      dprintf(STDERR_FILENO, "aux[AT_EGID] = %d\n", (int)auxv[i].a_un.a_val);
      break;
    case AT_ENTRY:
      dprintf(STDERR_FILENO, "aux[AT_ENTRY] = 0x%lx\n", auxv[i].a_un.a_val);
      break;
    case AT_EUID:
      dprintf(STDERR_FILENO, "aux[AT_EUID] = %d\n", (int)auxv[i].a_un.a_val);
      break;
    case AT_EXECFD:
      dprintf(STDERR_FILENO, "aux[AT_EXECFD] = %d\n", (int)auxv[i].a_un.a_val);
      break;
    case AT_FPUCW:
      dprintf(STDERR_FILENO, "aux[AT_FPUCW] = %d\n", (int)auxv[i].a_un.a_val);
      break;
    case AT_GID:
      dprintf(STDERR_FILENO, "aux[AT_GID] = %d\n", (int)auxv[i].a_un.a_val);
      break;
    case AT_ICACHEBSIZE:
      dprintf(STDERR_FILENO, "aux[AT_ICACHEBSIZE] = %d\n", (int)auxv[i].a_un.a_val);
      break;
    case AT_PAGESZ:
      dprintf(STDERR_FILENO, "aux[AT_PAGESZ] = %d\n", (int)auxv[i].a_un.a_val);
      break;
    case AT_FLAGS:
      dprintf(STDERR_FILENO, "aux[AT_FLAGS] = %d\n", (int)auxv[i].a_un.a_val);
      break;
    case AT_PHDR:
      dprintf(STDERR_FILENO, "aux[AT_PHDR] = 0x%lx\n", auxv[i].a_un.a_val);
      break;
    case AT_PHENT:
      dprintf(STDERR_FILENO, "aux[AT_PHENT] = %d\n", (int)auxv[i].a_un.a_val);
      break;
    case AT_PHNUM:
      dprintf(STDERR_FILENO, "aux[AT_PHNUM] = %d\n", auxv[i].a_un.a_val);
      break;
    case AT_SECURE:
      dprintf(STDERR_FILENO, "aux[AT_SECURE] = %d\n", auxv[i].a_un.a_val);
      break;
    case AT_UCACHEBSIZE:
      dprintf(STDERR_FILENO, "aux[AT_UCACHEBSIZE] = %d\n", auxv[i].a_un.a_val);
      break;
    case AT_UID:
      dprintf(STDERR_FILENO, "aux[AT_UID] = %d\n", auxv[i].a_un.a_val);
      break;      
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
      dprintf(STDERR_FILENO, "aux[%d] = 0x%lx\n", auxv[i].a_type, (long)auxv[i].a_un.a_val);
      break;
    }
  }
  dprintf(STDERR_FILENO, "Stack dump over\n");
}

static char* build_stack(char* stack_top) {
  char *top = stack_top;
  char *rv = 0;
  int i;

  top -= lt_stack_size;

  /* ABI requires stack pointer should be 16-byte aligned */
  rv = top = (char*)((unsigned long)top & ~15);

  /* zero fill the stack */
  memset(top, 0, lt_stack_size);

  /* all strings are written to pool */
  char* pool = top + lt_array_area_size;

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
  writeval(lt_argc);
  /* write argv */
  for (i = 0; i < lt_argc; i++) {
    writeval(pool);
    copystr(lt_argv[i]);
  }
  /* write a null value */
  writeval(0);
  /* write envp */
  for (i = 0; i < lt_envc; i++) {
    writeval(pool);
    copystr(lt_envp[i]);
  }
  /* write a null value */
  writeval(0);
  /* write the aux vector */
  for (i = 0; i < lt_auxc; i++) {
    switch (lt_auxv[i].a_type) {
    default:
      writeval(lt_auxv[i].a_type);
      writeval(lt_auxv[i].a_un.a_val);
      break;
    case AT_SYSINFO_EHDR: /* linux vdso */
    case AT_HWCAP: /* cpu capability mask */
      break;
    case AT_BASE:
      writeval(lt_auxv[i].a_type);
      writeval(libc_base);
      break;
    case AT_RANDOM:
      writeval(lt_auxv[i].a_type);
      writeval(pool);
      ((unsigned long*)pool)[0] = ((unsigned long*)lt_auxv[i].a_un.a_ptr)[0];
      ((unsigned long*)pool)[1] = ((unsigned long*)lt_auxv[i].a_un.a_ptr)[1];
      pool += 16;
      break;
    case AT_PLATFORM:
    case AT_EXECFN:
      writeval(lt_auxv[i].a_type);
      writeval(pool);
      copystr((char*)lt_auxv[i].a_un.a_ptr);
      break;
    }
  }
#undef writeval
#undef copystr

  return rv;
}

static void stack_init(void) {
  int argc;
  char **argv;
  stack = build_stack(stack);
  /*
  argc = ((unsigned long*)stack)[0];
  argv = (char**)&((unsigned long*)stack)[1];
  dump_stack(argc, argv);
  */
}

static void extract_elf_load_data(int argc, char **argv) {
  int i;
  lt_argc = argc;
  lt_argv = argv;

  /* for storing argc */
  lt_stack_size += 8;
  lt_array_area_size += 8;

  for (i = 0; i < lt_argc; i++)
    lt_stack_size += (strlen(lt_argv[i]) + 1); /* each argv[i] length */

  lt_stack_size += (lt_argc + 1) * 8; /* for storing argv[argc] */
  lt_array_area_size += (lt_argc + 1) * 8;
  
  lt_envp = lt_argv + lt_argc + 1;
  
  for (i = 0; lt_envp[i]; i++) {
    if (!strncmp(lt_envp[i], MCFI_SDK_NAME, strlen(MCFI_SDK_NAME))) {
      MCFI_SDK = lt_envp[i] + strlen(MCFI_SDK_NAME) + 1;
    } else if (!strncmp(lt_envp[i], "HOME", 4)) {
      HOME = lt_envp[i] + 5;
    }
    lt_stack_size += (strlen(lt_envp[i]) + 1); /* each envp[i] length */
  }
  lt_envc = i;
  
  lt_stack_size += (lt_envc + 1)* 8; /* for storing envp[] */
  lt_array_area_size += (lt_envc + 1) * 8;
  
  lt_auxv = (auxv_t*)(lt_envp + i + 1);

  for (i = 0; lt_auxv[i].a_type; ++i) {
    aux[lt_auxv[i].a_type] = lt_auxv[i].a_un.a_val;
    switch (lt_auxv[i].a_type) {
    default:
      lt_auxc++; break;
    case AT_PLATFORM:
      lt_auxc++;
      lt_stack_size += (strlen((char*)lt_auxv[i].a_un.a_ptr)+1);
      break;
    case AT_RANDOM:
      lt_auxc++;
      lt_stack_size += 16; /* the kernel provides a 16-byte random value */
      break;
    case AT_EXECFN:
      lt_auxc++;
      lt_stack_size += (strlen((char*)lt_auxv[i].a_un.a_ptr)+1);
      break;
    case AT_SYSINFO_EHDR:
      break;
    }
  }

  lt_auxc = i;

  lt_stack_size += (lt_auxc + 1) * sizeof(auxv_t); /* auxiliary vector */
  lt_array_area_size += (lt_auxc + 1) * sizeof(auxv_t);
}

/* eat a hex number and an underscore */
static char *eat_hex_and_udscore(char *c) {
  while (isalnum(*c))
    c++;
  if (*c == '_')
    c++;
  return c;
}

unsigned int alloc_bid_slot(void) {
  /* the first page after the first 64KB pointed to by %gs is used for trampolines,
   * so the bid slots start from the second page.
   * Later we should extend this function to be an ID allocation routine.
   */
  static unsigned int bid_slot = BID_SLOT_START;
  unsigned int rbid_slot = bid_slot;
  bid_slot += 8; /* 8 bytes */
  //dprintf(STDERR_FILENO, "%x\n", rbid_slot);
  return rbid_slot;
}

code_module *load_mcfi_metadata(char *elf, size_t sz) {
  Elf64_Ehdr *ehdr = (Ehdr *)elf;
  Elf64_Shdr *shdr = (Elf64_Shdr *)(elf + ehdr->e_shoff);
  Elf64_Shdr *shstrtbl = &shdr[ehdr->e_shstrndx];
  char *shstrpool = (char *)(elf + shstrtbl->sh_offset);
  Elf64_Sym *sym = 0;
  Elf64_Sym *dynsym = 0;
  Elf64_Rela *relaplt = 0;
  size_t plt_offset = 0;
  size_t numrelaplt = 0;
  char *strtab = 0;
  char *dynstr = 0;

  size_t cnt;
  size_t numsym = 0;
  size_t numdynsym = 0;

  icf *icfs = 0;
  function *functions = 0;
  dict *classes = 0;
  dict *cha = 0;
  dict *aliases = 0;
  dict *fats_in_code = 0;
  dict *fats_in_data = 0;
  dict *ctor = 0;
  dict *vtable = 0;
  dict *flp = 0;
  code_module *cm = alloc_code_module();

  for (cnt = 0; cnt < ehdr->e_shnum; cnt++) {
    if (cnt == ehdr->e_shstrndx)
      continue;
    char *shname = shstrpool + shdr[cnt].sh_name;
    //dprintf(STDERR_FILENO, "%d, %x, %s\n", cnt, shdr[cnt].sh_name, shname);
    if (0 == strcmp(shname, ".symtab")) {
      sym = (Elf64_Sym *)(elf + shdr[cnt].sh_offset);
      numsym = shdr[cnt].sh_size / sizeof(*sym);
    } else if (0 == strcmp(shname, ".strtab")) {
      strtab = elf + shdr[cnt].sh_offset;
    } else if (0 == strcmp(shname, ".MCFIIndirectCalls")) {
      parse_icfs(elf + shdr[cnt].sh_offset, /* content */
                 elf + shdr[cnt].sh_offset + shdr[cnt].sh_size, /* size */
                 &icfs, &stringpool);
    } else if (0 == strcmp(shname, ".MCFIFuncInfo")) {
      parse_functions(elf + shdr[cnt].sh_offset, /* content */
                      elf + shdr[cnt].sh_offset + shdr[cnt].sh_size, /* size */
                      &functions, &ctor, &stringpool);
    } else if (0 == strcmp(shname, ".MCFICHA")) {
      parse_cha(elf + shdr[cnt].sh_offset, /* content */
                elf + shdr[cnt].sh_offset + shdr[cnt].sh_size, /* size */
                &classes, &cha, &stringpool);
    } else if (0 == strcmp(shname, ".MCFIAliases")) {
      parse_aliases(elf + shdr[cnt].sh_offset, /* content */
                    elf + shdr[cnt].sh_offset + shdr[cnt].sh_size, /* size */
                    &aliases, &stringpool);
    } else if (0 == strcmp(shname, ".MCFIAddrTaken")) {
      parse_fats(elf + shdr[cnt].sh_offset, /* content */
                 elf + shdr[cnt].sh_offset + shdr[cnt].sh_size, /* size */
                 &fats_in_data, &stringpool);
    } else if (0 == strcmp(shname, ".MCFIAddrTakenInCode")) {
      parse_fats(elf + shdr[cnt].sh_offset, /* content */
                 elf + shdr[cnt].sh_offset + shdr[cnt].sh_size, /* size */
                 &fats_in_code, &stringpool);
    } else if (0 == strcmp(shname, ".MCFIVtable")) {
      parse_vtable(elf + shdr[cnt].sh_offset, /* content */
                   elf + shdr[cnt].sh_offset + shdr[cnt].sh_size, /* size */
                   &vtable, &stringpool);
    } else if (0 == strcmp(shname, ".MCFIDtorCxaAtExit")) {
      // TODO: Add handling code here
    } else if (0 == strcmp(shname, ".MCFIDtorCxaThrow")) {
      // TODO: Add handling code here
    } else if (0 == strcmp(shname, ".rela.plt")) {
      relaplt = (Elf64_Rela*)(elf + shdr[cnt].sh_offset);
      numrelaplt = shdr[cnt].sh_size / sizeof(*relaplt);
      //dprintf(STDERR_FILENO, "%d\n", numrelaplt);
    } else if (0 == strcmp(shname, ".plt")) {
      plt_offset = shdr[cnt].sh_offset;
    } else if (0 == strcmp(shname, ".dynsym")) {
      dynsym = (Elf64_Sym*)(elf + shdr[cnt].sh_offset);
      numdynsym = shdr[cnt].sh_size / sizeof(*dynsym);
    } else if (0 == strcmp(shname, ".dynstr")) {
      dynstr = elf + shdr[cnt].sh_offset;
    } else if (0 == strcmp(shname, ".got.plt")) {
      //dprintf(STDERR_FILENO, ".got.plt = %x\n", shdr[cnt].sh_addr);
      cm->gotplt = shdr[cnt].sh_addr;
      cm->gotpltsz = RoundToPage(shdr[cnt].sh_size);
    }
  }

  if (ehdr->e_type == ET_EXEC) {
    cm->base_addr = X64ABIBASE;
    /* add the entry point of the exe as a fake function */
    function *f = alloc_function();
    f->name = sp_intern_string(&stringpool, "__exe_elf_entry");
    f->type = sp_intern_string(&stringpool, "ExeElfEntry");
    DL_APPEND(functions, f);

    /* the fake function's address is taken */
    dict_add(&fats_in_data, f->name, 0);

    /* add the fake function's address */
    symbol *funcsym = alloc_sym();
    funcsym->name = f->name;
    funcsym->offset = ehdr->e_entry - cm->base_addr;
    DL_APPEND(cm->funcsyms, funcsym);
  }
  cm->icfs = icfs;
  cm->functions = functions;
  cm->classes = classes;
  cm->cha = cha;
  cm->aliases = aliases;
  cm->fats_in_code = fats_in_code;
  cm->fats_in_data = fats_in_data;
  merge_dicts(&(cm->fats), cm->fats_in_code);
  merge_dicts(&(cm->fats), cm->fats_in_data);
  cm->vtable = vtable;
  cm->sz = sz;

  graph *aliases_tc = g_transitive_closure(&aliases);

  unsigned int patch_call_offset = -1;
  const char *patch_call = sp_intern_string(&stringpool, "__patch_call");

  unsigned int patch_entry_offset = -1;
  const char *patch_entry = sp_intern_string(&stringpool, "__patch_entry");

  for (cnt = 0; cnt < numsym; cnt++) {
    char *symname = strtab + sym[cnt].st_name;
    if (0 == strncmp(symname, "__mcfi_dcj_", 11)) {
      symbol *dcjsym = alloc_sym();
      dcjsym->name = sp_intern_string(&stringpool, eat_hex_and_udscore(symname + 11));
      dcjsym->offset = sym[cnt].st_value - cm->base_addr;
      //dprintf(STDERR_FILENO, "%x, %s\n", dcjsym->offset, dcjsym->name);
      DL_APPEND(cm->rad, dcjsym);
      /* save the original code bytes */
      dict_add(&(cm->ra_orig), (void*)dcjsym->offset,
               (void*)*((unsigned long*)(elf + dcjsym->offset - 8)));
    } else if (0 == strncmp(symname, "__mcfi_icj_", 11)) {
      symbol *icjsym = alloc_sym();
      icjsym->name = sp_intern_string(&stringpool, eat_hex_and_udscore(symname + 11));
      icjsym->offset = sym[cnt].st_value - cm->base_addr;
      //dprintf(STDERR_FILENO, "%s, %x\n", icjsym->name, icjsym->offset);
      DL_APPEND(cm->rai, icjsym);
      /* save the original code bytes */
      keyvalue *patch = dict_add(&(cm->ra_orig), (void*)icjsym->offset,
                                 (void*)*((unsigned long*)(elf + icjsym->offset - 8)));
      patch->key = _mark_ra_ic(patch->key);
    } else if (0 == strncmp(symname, "__mcfi_at_", 10)) {
      /* save the original code bytes for function address taken */
      size_t offset = sym[cnt].st_value - cm->base_addr;
      char *name = sp_intern_string(&stringpool, eat_hex_and_udscore(symname + 10));
      dict_add(&(cm->at_func), (void*)offset, name);
    } else if (0 == strncmp(symname, "__mcfi_lp_", 10)) {
      symbol *lpsym = alloc_sym();
      /* no needs to record the name for a landing pad symbol */
      lpsym->offset = sym[cnt].st_value - cm->base_addr;
      DL_APPEND(cm->lp, lpsym);
      g_add_directed_edge(&flp,
                          sp_intern_string(&stringpool, eat_hex_and_udscore(symname + 10)),
                          (void*)lpsym->offset);
    }
#ifdef NOCFI
    else if (0 == strncmp(symname, "catchbranch", 11)) {
      /* catch branch: cmpb $0xfe, %gs:(%r11) defined in libunwind setcontext.S */
      char *addr = elf + sym[cnt].st_value - cm->base_addr;
      memcpy(addr, five_byte_nop, 5);
      if (addr[5] == 0x0f) {
        memcpy(addr, six_byte_nop, 6);
      } else {
        memcpy(addr, two_byte_nop, 2);
      }
    }
#endif
    else if (0 == strncmp(symname, "__mcfi_bary_", 12)) {
      //dprintf(STDERR_FILENO, "%s\n", symname);
      symbol *icfsym = alloc_sym();
      icfsym->name = sp_intern_string(&stringpool, symname + 12);
      unsigned int bid_slot = alloc_bid_slot();
#ifndef NOCFI
      memcpy(elf + sym[cnt].st_value - cm->base_addr - sizeof(unsigned int),
             &bid_slot,
             sizeof(unsigned int));
#else
      /* replace the instrumentation with nop */
      char *addr = elf + sym[cnt].st_value - cm->base_addr;
      /* 9-byte BID read */
      memcpy(addr - 9, nine_byte_nop, 9);
      /* 4/5 byte TID load, only need to check the possible sib byte */
      if (addr[4] == 0 || addr[4] == 0x24) {
        memcpy(addr, five_byte_nop, 5);
        addr += 5;
      } else {
        memcpy(addr, four_byte_nop, 4);
        addr += 4;
      }
      /* 3-byte BID TID comparison */
      memcpy(addr, three_byte_nop, 3);
      addr += 3;
      if (*addr == 0x0f) { /* jcc A, A, A, A */
        memcpy(addr, six_byte_nop, 6);
      } else { /* jcc A */
        memcpy(addr, two_byte_nop, 2);
      }
#endif
      icfsym->offset = bid_slot;
      //dprintf(STDERR_FILENO, "icfsym: %s, %x, %x\n", icfsym->name, icfsym->offset,
      //        sym[cnt].st_value - cm->base_addr);
      DL_APPEND(cm->icfsyms, icfsym);
    } else if (ELF64_ST_TYPE(sym[cnt].st_info) == STT_FUNC &&
               sym[cnt].st_shndx != SHN_UNDEF) {
      //dprintf(STDERR_FILENO, "%s\n", symname);
      symbol *funcsym = alloc_sym();
      funcsym->name = sp_intern_string(&stringpool, symname);
      funcsym->offset = sym[cnt].st_value - cm->base_addr;
      //dprintf(STDERR_FILENO, "%lx, %s\n", funcsym->offset, funcsym->name);
      DL_APPEND(cm->funcsyms, funcsym);

      keyvalue *kv = dict_find(ctor, funcsym->name);
      // if kv is a virtual method and its class's vtable is nos taken yet
      if (kv && !dict_find(vtabletaken, kv->value) &&
          dict_find(vtable, kv->value)) {
        dict_add(&(cm->func_orig), (void*)funcsym->offset,
                 (void*)*((unsigned long*)(elf + funcsym->offset)));
        dict_add(&(cm->ctor), (void*)funcsym->offset, kv->value);
        // this class' constructor has a body
        if (!dict_find(cm->defined_ctors, kv->value))
          dict_add(&(cm->defined_ctors), kv->value, 0);
        //dprintf(STDERR_FILENO, "reg ctor: %s\n", kv->value);
      }

      if (funcsym->name == patch_call)
        patch_call_offset = funcsym->offset;

      if (ELF64_ST_BIND(sym[cnt].st_info) == STB_WEAK) {
        /* since we are traversing the symbol table, all aliases would be added */
        g_add_directed_edge(&(cm->weakfuncs), (void*)funcsym->offset, funcsym->name);
      }
    }
  }

  // traverse the function symbol list to register landing pads
  {
    symbol *f;
    DL_FOREACH(cm->funcsyms, f) {
      keyvalue *kv = dict_find(flp, f->name);
      // this function comes with landing pads
      if (kv) {
        keyvalue *v, *tmp;
        HASH_ITER(hh, (dict*)(kv->value), v, tmp) {
          g_add_edge(&(cm->flp), (void*)f->offset, v->key);
        }
        // this function might be a constructor which would register
        // virtual methods to the cfg. In such a case, the original
        // code bytes should have been registered. We should handle
        // the opposite case.
        if (!dict_find(cm->func_orig, (void*)f->offset)) {
          dict_add(&(cm->func_orig), (void*)f->offset,
                   (void*)*((unsigned long*)(elf + f->offset)));
        }
      }
    }
    g_dtor(&flp);
  }
  /* Function's whose names appear in .dynsym may have their
     addresses taken during runtime */
  for (cnt = 0; cnt < numdynsym; ++cnt) {
    if (ELF64_ST_BIND(dynsym[cnt].st_info) == STB_GLOBAL &&
        ELF64_ST_TYPE(dynsym[cnt].st_info) == STT_FUNC &&
        dynsym[cnt].st_shndx != STN_UNDEF) {
      char *fname = sp_intern_string(&stringpool,
                                     dynstr + dynsym[cnt].st_name);
      void *addr = (void*)(dynsym[cnt].st_value - cm->base_addr);
      //dprintf(STDERR_FILENO, "%x, %s\n", addr, dynstr+dynsym[cnt].st_name);
      /* add all functions who are alised with fname*/
      keyvalue *kv = dict_find(aliases_tc, fname);
      if (!kv) {
        g_add_directed_edge(&(cm->dynfuncs), addr, fname);
      } else {
        keyvalue *adrv, *tmp;
        HASH_ITER(hh, (dict*)(kv->value), adrv, tmp) {
          g_add_directed_edge(&(cm->dynfuncs), addr, adrv->key);
        }
      }
    }
  }
  g_free_transitive_closure(&aliases_tc);
  /* PLT entries are essentially functions */
  for (cnt = 0; cnt < numrelaplt; cnt++) {
    size_t dynsymidx = ELF64_R_SYM(relaplt[cnt].r_info);
    symbol *funcsym = alloc_sym();
    funcsym->name =
      sp_intern_string(&stringpool, dynstr + dynsym[dynsymidx].st_name);
    funcsym->offset = plt_offset + (cnt + 1) * PLT_ENT_SIZE;
    DL_APPEND(cm->funcsyms, funcsym);
    //dprintf(STDERR_FILENO, "%x, %x, %s\n", cnt + 1, funcsym->offset, funcsym->name);
    dict_add(&(cm->gpfuncs), (void*)relaplt[cnt].r_offset - cm->gotplt, funcsym->name);
    //dprintf(STDERR_FILENO, "%x, %s\n", relaplt[cnt].r_offset - cm->gotplt, funcsym->name);
    if (funcsym->name == patch_call)
      patch_call_offset = funcsym->offset;
    else if (funcsym->name == patch_entry)
      patch_entry_offset = funcsym->offset;
  }
  dict_clear(&ctor);
  /* if not explicitly shutdown, just enable online patching */
#ifndef NO_ONLINE_PATCHING
  if (patch_call_offset != -1) {
    /* patch direct calls */
    keyvalue *kv, *tmp;
    unsigned int callsite_offset;
    unsigned int patch;
    HASH_ITER(hh, cm->ra_orig, kv, tmp) {
      if (_is_marked_ra_ic(kv->key)) {
        kv->key = _unmark_ptr(kv->key);
        //dprintf(STDERR_FILENO, "RAIC: %lx\n", kv->key);
        unsigned five_byte_nop_offset;
        callsite_offset = (unsigned int)kv->key;
        char *p = elf + callsite_offset - 8;
        if (p[5] != 0) { /* call *%r8--*%r15 */
          five_byte_nop_offset = 0;
          callsite_offset -= 3;
        } else {
          five_byte_nop_offset = 1;
          callsite_offset -= 2;
        }
        p[five_byte_nop_offset] = 0xe8;
        patch = patch_call_offset - callsite_offset;
        memcpy(p + five_byte_nop_offset + 1, &patch, 4);
      } else {
        callsite_offset = (unsigned int)kv->key;
        patch = patch_call_offset - callsite_offset;
        memcpy(elf + callsite_offset - 4, &patch, 4);
      }
    }
  }
  {
    keyvalue *kv, *tmp;
    HASH_ITER(hh, cm->func_orig, kv, tmp) {
      if (dict_find(cm->ra_orig, (void*)((unsigned long)kv->key + 8))) {
        // one place is patched twice, establish the order
        kv->value = (void*)(*(unsigned long*)(elf + (unsigned long)kv->key));
      }
    }
  }
  if (patch_entry_offset != -1) {
    keyvalue *kv, *tmp;
    unsigned int patch;
    HASH_ITER(hh, cm->func_orig, kv, tmp) {
      unsigned int callsite = (unsigned int)kv->key;
      char *p = elf + callsite;
      *p = 0xe8;
      patch = patch_entry_offset - (callsite+5);
      memcpy(p+1, &patch, 4);
    }
  }
#endif
  return cm;
}

char *load_elf_content(int fd, /*out*/size_t *elf_size_rounded_to_page_boundary) {
  struct stat st;

  if (0 != fstat(fd, &st)) {
    dprintf(STDERR_FILENO, "[load_elf_into_memory] fstat failed with %d\n", errn);
    quit(-1);
  }

  *elf_size_rounded_to_page_boundary =
    (st.st_size + PAGE_SIZE - 1) / PAGE_SIZE * PAGE_SIZE;

  void *elf = mmap(0, *elf_size_rounded_to_page_boundary,
                   PROT_READ | PROT_WRITE, MAP_PRIVATE,
                   fd, 0);

  if (elf == MAP_FAILED) {
    dprintf(STDERR_FILENO, "[load_opened_elf_into_memory] memory mmap failed\n");
    quit(-1);
  }
  return elf;
}

static void remove_trampoline_type(code_module *m) {
  function *f;
  static dict *tramps = 0;
  if (!tramps) {
    g_add_vertex(&tramps, sp_intern_string(&stringpool, "trampoline_mmap"));
    g_add_vertex(&tramps, sp_intern_string(&stringpool, "trampoline_mremap"));
    g_add_vertex(&tramps, sp_intern_string(&stringpool, "trampoline_mprotect"));
    g_add_vertex(&tramps, sp_intern_string(&stringpool, "trampoline_munmap"));
    g_add_vertex(&tramps, sp_intern_string(&stringpool, "trampoline_brk"));
    g_add_vertex(&tramps, sp_intern_string(&stringpool, "trampoline_set_tcb"));
    g_add_vertex(&tramps, sp_intern_string(&stringpool, "trampoline_allocset_tcb"));
    g_add_vertex(&tramps, sp_intern_string(&stringpool, "trampoline_free_tcb"));
    g_add_vertex(&tramps, sp_intern_string(&stringpool, "trampoline_load_native_code"));
    g_add_vertex(&tramps, sp_intern_string(&stringpool, "trampoline_gen_cfg"));
  }
  DL_FOREACH(m->functions, f) {
    if (g_in(tramps, f->name))
      f->type = 0;
  }
}


/* create a parallel mapping for insandbox base, and return the mapped
 * pages outside of the sandbox
 */
void *create_parallel_mapping(void *base,
                              size_t size,
                              int prot) {
  /* TODO: replace /dev/shm/mcfi with a random name */
  const char *shmname = "/dev/shm/mcfi";
  int fd = -1;
  while (TRUE) {
    fd = shm_open(shmname, O_RDWR | O_CREAT | O_EXCL, 0744);
    if (fd >= 0)
      break;
    if (fd == -1 && errn == EEXIST)
      continue;
    dprintf(STDERR_FILENO,
            "[create_parallel_mapping] shm_open failed with %d\n", errn);
    quit(-1);
  }

  if (0 != ftruncate(fd, size)) {
    dprintf(STDERR_FILENO,
            "[create_parallel_mapping] ftruncate failed with %d\n", errn);
    quit(-1);
  }

  assert(size % PAGE_SIZE == 0);
  base = mmap(base, size, prot, MAP_SHARED | MAP_FIXED,
              fd, 0);
  if (base == (void*)-1) {
    dprintf(STDERR_FILENO,
            "[create_parallel_mapping] mmap base failed with %d\n", errn);
    quit(-1);
  }

  void *osb_base = mmap(0, size, PROT_WRITE, MAP_SHARED, fd, 0);
  if (osb_base == (void*)-1) {
    dprintf(STDERR_FILENO,
            "[create_parallel_mapping] mmap osb_base failed with %d\n", errn);
    quit(-1);
  }

  close(fd);
  shm_unlink(shmname);
  return osb_base;
}

/* load elf from fd to base,
 * if base == 0, allocate a place to load
 * else, load at base.
 * Return the loaded base address
 */
void *load_elf(int fd, int is_exe, char **entry) {
  char *base = 0;
  size_t elf_size = 0;
  /* load elf in a separate position */
  char *elf = load_elf_content(fd, &elf_size);
  close(fd);
  /* TODO:
   *
   * CHECK THE LOADED ELF FILE.
   * CHECK THE LOADED ELF FILE.
   */
  /* elf will be rewritten */
  code_module *cm = load_mcfi_metadata(elf, elf_size);
  remove_trampoline_type(cm);
  cm->is_exe = is_exe;
  DL_APPEND(modules, cm);

  /* compute how many consecutive memory pages we need */
  size_t phdr_vaddr_end = 0;

  Elf64_Phdr *phdr = (Phdr*)(elf + ((Elf64_Ehdr*)elf)->e_phoff);
  size_t cnt = ((Ehdr*)elf)->e_phnum;

  for (; cnt; cnt--, ++phdr) {
    if (phdr->p_type == PT_LOAD) {
      if (phdr->p_vaddr + phdr->p_memsz > phdr_vaddr_end)
        phdr_vaddr_end = phdr->p_vaddr + phdr->p_memsz;
    }
  }

  /* align the addresses */
  phdr_vaddr_end = RoundToPage(phdr_vaddr_end);

  if (!is_exe) {
    /* find a consecutive region of memory */
    uintptr_t pa =
      VmmapFindSpace(&VM, phdr_vaddr_end >> PAGESHIFT);

    if (pa == 0) {
      dprintf(STDERR_FILENO, "[load_elf] VmmapFindSpace pa failed\n");
      quit(-1);
    }

    pa <<= PAGESHIFT;
    base = (char*)pa;
  } else {
    base = (char*)X64ABIBASE;
  }

  //dprintf(STDERR_FILENO, "base: %p\n", base);
  /* load the actual program headers */
  phdr = (Phdr*)(elf + ((Ehdr*)elf)->e_phoff);
  cnt = ((Ehdr*)elf)->e_phnum;

#define VADDR(vaddr, is_exe) (is_exe ? (vaddr - X64ABIBASE) : vaddr)
  
  for (; cnt; cnt--, ++phdr) {
    if (phdr->p_type == PT_LOAD) {
      uintptr_t curpage = CurPage(base + VADDR(phdr->p_vaddr, is_exe));
      size_t sz = RoundToPage(base + VADDR(phdr->p_vaddr, is_exe) + phdr->p_memsz) - curpage;
      char *content;
      if (_prot(phdr->p_flags) & PROT_EXEC) {
        content = create_parallel_mapping(base, sz, PROT_EXEC);
        cm->osb_base_addr = (uintptr_t)content;
        cm->sz = sz;
      } else {
        content = mmap((char*)curpage,
                       sz,
                       PROT_READ | PROT_WRITE,
                       MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED,
                       -1, 0);
        if (content != (char*)curpage) {
          dprintf(STDERR_FILENO, "[load_elf] mmap failed for alloc a program section\n");
          quit(-1);
        }
      }
      /* copy the content of the ELF */
      memcpy(content + (phdr->p_vaddr & (PAGE_SIZE-1)),
             elf + phdr->p_offset, phdr->p_filesz);
      VmmapAdd(&VM, curpage >> PAGESHIFT,
               sz >> PAGESHIFT,
               _prot(phdr->p_flags), _prot(phdr->p_flags),
               VMMAP_ENTRY_ANONYMOUS);
    }
  }
  /* copy .got.plt out from the loaded segment */
  void *gotplt = malloc(cm->gotpltsz);
  if (!gotplt) {
    dprintf(STDERR_FILENO, "[load_elf] gotplt malloc failed\n");
    quit(-1);
  }
  memcpy(gotplt, base + VADDR(cm->gotplt, is_exe), cm->gotpltsz);
  /* .got.plt should be remapped with a parallel mapping */
  if (0 != munmap(base + VADDR(cm->gotplt, is_exe), cm->gotpltsz)) {
    dprintf(STDERR_FILENO, "[load_elf] .got.plt unmap failed with %d\n", errn);
    quit(-1);
  }
  void *osb_gp = create_parallel_mapping(base + VADDR(cm->gotplt, is_exe),
                                         cm->gotpltsz, PROT_READ);/* readonly */
  cm->gotplt = (size_t)(base + VADDR(cm->gotplt, is_exe));
  cm->osb_gotplt = (uintptr_t)osb_gp;
  memcpy(osb_gp, gotplt, cm->gotpltsz);
  free(gotplt);
#undef VADDR
  if (entry) {
    *entry = base + ((Ehdr*)elf)->e_entry;
    //dprintf(STDERR_FILENO, "Entry: %x\n", *entry);
  }
  cm->base_addr = (unsigned long)base;
  /* release the elf file */
  munmap(elf, elf_size);
  return base;
}

static void *load_elf_name(const char* name, int is_exe, char **entry) {
  int fd = open(name, O_RDONLY, 0);
  if (fd == -1) {
    dprintf(STDERR_FILENO, "[load_elf_name] open %s failed with %d\n", name);
    quit(-1);
  }
  return load_elf(fd, is_exe, entry);
}

static void load_libc(void) {
  char libc_path[MAX_PATH];
  libc_path[MAX_PATH-1] = '\0';
  if (MCFI_SDK) {
    snprintf(libc_path, MAX_PATH-1, "%s/lib/libc.so", MCFI_SDK);
  } else {
    snprintf(libc_path, MAX_PATH-1, "%s/MCFI/toolchain/lib/libc.so", HOME);
  }
  /* dprintf(STDERR_FILENO, "%s\n", libc_path); */
  libc_base = load_elf_name(libc_path, FALSE, &libc_entry);
}

static void populate_vtabletaken(dict **vtt) {
#define ADD(V) dict_add(vtt, sp_intern_string(&stringpool, V), 0)
  ADD("__cxxabiv1::__shim_type_info");
  ADD("__cxxabiv1::__fundamental_type_info");
  ADD("__cxxabiv1::__array_type_info");
  ADD("__cxxabiv1::__function_type_info");
  ADD("__cxxabiv1::__enum_type_info");
  ADD("__cxxabiv1::__class_type_info");
  ADD("__cxxabiv1::__si_class_type_info");
  ADD("__cxxabiv1::__vmi_class_type_info");
  ADD("__cxxabiv1::__pbase_type_info");
  ADD("__cxxabiv1::__pointer_type_info");
  ADD("__cxxabiv1::__pointer_to_member_type_info");
#undef ADD
}

/* main function of the runtime */
void* runtime_init(int argc, char **argv) {
  /* let's first collect some basic information of this ELF loading */
  extract_elf_load_data(argc, argv);

  /* initialize the sandbox memory pager */
  if (!VmmapCtor(&VM)) {
    dprintf(STDERR_FILENO, "[runtime_init] memory pager init failed\n");
    quit(-1);
  }

  /* reserve the lowest 4GB by considering the loaded app. */
  alloc_sandbox();
  
  /* reserve another 4GB memory for Bary and Tary,
     and set %gs
  */
  reserve_table_region();

  /* install the trampolines through which the sandboxed
     application can escape the sandbox securely */
  install_trampolines();

  /* set up the trusted thread-control block */
  set_tcb_pointer(alloc_tcb());
  
  /* load the libc */
  load_libc();

  /* populate vtabletaken */
  populate_vtabletaken(&vtabletaken);

  /* reload the application by creating a parallel mapping for it */
  load_elf_name(argv[0], TRUE, 0);
  
  /* copy data from kernel-allocated stack to sandbox-stack */
  stack_init();

#ifdef PROFILING
  {
    struct timeval tv;
    gettimeofday(&tv);
    dprintf(STDERR_FILENO, "[%lu:%lu] Started!\n", tv.tv_sec, tv.tv_usec);
  }
#endif
  return stack;
}
