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

#define MAX_PATH 256

/* x64 Linux loads images from 0x400000 */
#define X64ABIBASE 0x400000UL

/* size of the plt entry */
#define PLT_ENT_SIZE 32

/* the table region holding Bary and Tary */
void* table = 0;

/* after we load libc, we set the following data */
char* libc_base = 0;
void* libc_entry = 0;
char* stack = 0;

/* load-time data */
char **lt_argv = 0;
int lt_argc = 0;

char **lt_envp = 0;
int lt_envc = 0;

/* cfg generation data */
code_module *modules = 0; /* code modules */
str *stringpool = 0;

/* default SDK path */
const char *MCFI_SDK = 0;
const char *HOME = 0;
const char MCFI_SDK_NAME[] = "MCFI_SDK";

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
  
  Phdr *phdr = (Phdr*)aux[AT_PHDR];
  size_t cnt = aux[AT_PHNUM];
  for (; cnt; cnt--, phdr = (void *)((char *)phdr + aux[AT_PHENT])) {
    if (phdr->p_type == PT_LOAD) {
      start = CurPage(phdr->p_vaddr);
      end = RoundToPage(phdr->p_vaddr + phdr->p_memsz);
      int prot = _prot(phdr->p_flags);

      if ((prot & PROT_EXEC) && (prot & PROT_WRITE)) {
        dprintf(STDERR_FILENO, "[alloc_sandbox] W + E segments are allowed in this app!\n");
        quit(-1);
      }
      VmmapAdd(&VM, start >> PAGESHIFT, (end-start) >> PAGESHIFT,
               prot, prot, VMMAP_ENTRY_ANONYMOUS);
    }
  }

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
    void *dyncode_install;
    void *dyncode_modify;
    void *dyncode_delete;
    void *report_cfi_violation;
    void *online_patch;
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
  extern unsigned long runtime_dyncode_install;
  extern unsigned long runtime_dyncode_modify;
  extern unsigned long runtime_dyncode_delete;
  extern unsigned long runtime_report_cfi_violation;
  extern unsigned long runtime_online_patch;
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
  tp->dyncode_install = &runtime_dyncode_install;
  tp->dyncode_modify = &runtime_dyncode_modify;
  tp->dyncode_delete = &runtime_dyncode_delete;
  tp->report_cfi_violation = &runtime_report_cfi_violation;
  tp->online_patch = &runtime_online_patch;

  if (0 != mprotect(table,  0x11000, PROT_READ)) {
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
  argc = ((unsigned long*)stack)[0];
  argv = (char**)&((unsigned long*)stack)[1];
  /* dump_stack(argc, argv); */
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

static unsigned int alloc_bid_slot(void) {
  /* the first page after the first 64KB pointed to by %gs is used for trampolines,
   * so the bid slots start from the second page.
   * Later we should extend this function to be an ID allocation routine.
   */
  static unsigned int bid_slot = 0x11000;
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
  dict *fats = 0;

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
                      &functions, &stringpool);
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
                 &fats, &stringpool);
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
    }
  }

  /* Function's whose names appear in .dynsym also have their addresses taken */
  for (cnt = 0; cnt < numdynsym; ++cnt) {
    if (ELF64_ST_BIND(dynsym[cnt].st_info) == STB_GLOBAL &&
        ELF64_ST_TYPE(dynsym[cnt].st_info) == STT_FUNC &&
        dynsym[cnt].st_shndx != STN_UNDEF) {
      //dprintf(STDERR_FILENO, "%d, %s\n", numdynsym, dynstr+dynsym[cnt].st_name);
      char *fname = sp_intern_string(&stringpool,
                                     dynstr + dynsym[cnt].st_name);
      g_add_vertex(&fats, fname);
    }
  }

  code_module *cm = alloc_code_module();
  if (ehdr->e_type == ET_EXEC) {
    cm->base_addr = X64ABIBASE;
    /* add the entry point of the exe as a fake function */
    function *f = alloc_function();
    f->name = sp_intern_string(&stringpool, "__exe_elf_entry");
    f->type = sp_intern_string(&stringpool, "ExeElfEntry");
    DL_APPEND(functions, f);

    /* the fake function's address is taken */
    dict_add(&fats, f->name, 0);

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
  cm->fats = fats;
  cm->sz = sz;

  unsigned int patch_call_offset = -1;
  const char *patch_call = sp_intern_string(&stringpool, "__patch_call");

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
    } else if (0 == strncmp(symname, "__mcfi_lp_", 10)) {
      symbol *lpsym = alloc_sym();
      /* no needs to record the name for a landing pad symbol */
      lpsym->offset = sym[cnt].st_value - cm->base_addr;
      DL_APPEND(cm->lp, lpsym);
    } else if (0 == strncmp(symname, "__mcfi_bary_", 12)) {
      //dprintf(STDERR_FILENO, "%s\n", symname);
      symbol *icfsym = alloc_sym();
      icfsym->name = sp_intern_string(&stringpool, symname + 12);
      unsigned int bid_slot = alloc_bid_slot();
      memcpy(elf + sym[cnt].st_value - cm->base_addr - sizeof(unsigned int),
             &bid_slot,
             sizeof(unsigned int));
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
      if (funcsym->name == patch_call)
        patch_call_offset = funcsym->offset;
    }
  }

  /* PLT entries are essentially functions */
  for (cnt = 0; cnt < numrelaplt; cnt++) {
    size_t dynsymidx = ELF64_R_SYM(relaplt[cnt].r_info);
    symbol *funcsym = alloc_sym();
    funcsym->name =
      sp_intern_string(&stringpool, dynstr + dynsym[dynsymidx].st_name);
    funcsym->offset = plt_offset + (cnt + 1) * PLT_ENT_SIZE;
    DL_APPEND(cm->funcsyms, funcsym);
    //dprintf(STDERR_FILENO, "%x, %x, %s\n", cnt + 1, funcsym->offset, funcsym->name);
    if (funcsym->name == patch_call)
      patch_call_offset = funcsym->offset;
  }

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
  return cm;
}

char *load_opened_elf_into_memory(int fd,
                                  /*out*/size_t *elf_size_rounded_to_page_boundary) {
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

static char *load_elf_into_memory(const char *path,
                                  /*out*/size_t *elf_size_rounded_to_page_boundary) {
  int fd = open(path, O_RDONLY, 0);
  if (fd == -1) {
    dprintf(STDERR_FILENO, "[load_elf_into_memory] %s open failed with %d\n",
            path, errn);
    quit(-1);
  }
  
  char* elf = load_opened_elf_into_memory(fd, elf_size_rounded_to_page_boundary);
  close(fd);
  return elf;
}

static void remove_trampoline_type(code_module *m) {
  function *f;
  dict *tramps = 0;
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

  DL_FOREACH(m->functions, f) {
    if (g_in(tramps, f->name))
      f->type = 0;
  }
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
  size_t libc_size_rounded_to_page_boundary = 0;
  char *libc_elf = load_elf_into_memory(libc_path, &libc_size_rounded_to_page_boundary);

  /* Load symbols and rewrite bary entries */
  code_module *cm = load_mcfi_metadata(libc_elf, libc_size_rounded_to_page_boundary);
  /* For trampolines in the libc module, remove their types */
  remove_trampoline_type(cm);
  
  DL_APPEND(modules, cm);

  /* compute how many consecutive memory pages we need */
  size_t phdr_vaddr_end = 0;
  
  Phdr *phdr = (Phdr*)(libc_elf + ((Ehdr*)libc_elf)->e_phoff);
  size_t cnt = ((Ehdr*)libc_elf)->e_phnum;
  
  for (; cnt; cnt--, phdr = (void *)((char *)phdr + ((Ehdr*)libc_elf)->e_phentsize)) {
    if (phdr->p_type == PT_LOAD) {
      if (phdr->p_vaddr + phdr->p_memsz > phdr_vaddr_end)
        phdr_vaddr_end = phdr->p_vaddr + phdr->p_memsz;
    }
  }
  
  /* find a consecutive region of memory */
  uintptr_t libc = VmmapFindSpace(&VM, RoundToPage(phdr_vaddr_end) >> PAGESHIFT);

  if (libc == 0) {
    dprintf(STDERR_FILENO, "[load_libc] sandbox mem alloc failed\n");
    quit(-1);
  }

  libc <<= PAGESHIFT;
  libc_base = (void*)libc;
  cm->base_addr = (uintptr_t)libc_base;

  //dprintf(STDERR_FILENO, "libc: %p\n", libc_base);
  /* load the actual program headers */
  phdr = (Phdr*)(libc_elf + ((Ehdr*)libc_elf)->e_phoff);
  cnt = ((Ehdr*)libc_elf)->e_phnum;

  for (; cnt; cnt--, phdr = (void *)((char *)phdr + ((Ehdr*)libc_elf)->e_phentsize)) {
    if (phdr->p_type == PT_LOAD) {
      uintptr_t curpage = CurPage(libc + phdr->p_vaddr);
      size_t sz = RoundToPage(libc + phdr->p_vaddr + phdr->p_memsz) - curpage;
      char* content = mmap((char*)curpage,
                           sz,
                           PROT_READ | PROT_WRITE,
                           MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED,
                           -1, 0);
      if (content != (char*)curpage) {
        dprintf(STDERR_FILENO, "[load_libc] sandbox mem alloc failed for a program section\n");
        quit(-1);
      }

      /* copy the content of the ELF */
      memcpy(content + (phdr->p_vaddr & (PAGE_SIZE-1)), libc_elf + phdr->p_offset, phdr->p_filesz);
      if (_prot(phdr->p_flags) & PROT_EXEC) {
        if (mprotect(content, RoundToPage(phdr->p_memsz), PROT_EXEC | PROT_WRITE)) {
          dprintf(STDERR_FILENO,
                  "[load_libc] writable permission of executable code memory cannot be dropped\n");
          quit(-1);
        }
        libc_entry = ((Ehdr*)libc_elf)->e_entry + content;
      }
      VmmapAdd(&VM, curpage >> PAGESHIFT,
               sz >> PAGESHIFT,
               _prot(phdr->p_flags), _prot(phdr->p_flags),
               VMMAP_ENTRY_ANONYMOUS);
    }
  }
  /* VmmapDebug(&VM, "After libc loaded\n"); */
  munmap(libc_elf, libc_size_rounded_to_page_boundary);
}

/* replace the dest elf's executable segment with src elf's executable segment*/
void replace_prog_seg(char *dest, char *src) {
  Elf64_Ehdr *ehdr = (Elf64_Ehdr*)dest;
  Elf64_Phdr *phdr = (Elf64_Phdr*)(dest + ehdr->e_phoff);
  size_t cnt = ehdr->e_phnum;
  for (; cnt; cnt--, ++phdr) {
    if (phdr->p_type == PT_LOAD) {
      int prot = _prot(phdr->p_flags);
      if (prot & PROT_EXEC) {
        if (0 != mprotect(dest, RoundToPage(phdr->p_memsz), PROT_READ | PROT_WRITE)) {
          dprintf(STDERR_FILENO, "[replace_prog_seg] mprotect W failed with %d\n", errn);
          quit(-1);
        }
        memcpy(dest, src, phdr->p_memsz);
        //dprintf(STDERR_FILENO, "[replace_prog_seg] %p, %x\n", dest, phdr->p_memsz);
        if (0 != mprotect(dest, RoundToPage(phdr->p_memsz), PROT_EXEC | PROT_WRITE/*prot*/)) {
          dprintf(STDERR_FILENO, "[replace_prog_seg] mprotect RE failed with %d\n", errn);
          quit(-1);
        }
        /* compare whether the copied code has been changed by attackers */
        if (memcmp(dest, src, phdr->p_memsz)) {
          report_error("[replace_prog_seg]: concurrent code changes detected\n");
          quit(-1);
        }
      }
    }
  }
}

/* load the bid rewritten exe */
static void load_bid_rewritten_exe(const char *exe_name) {
  size_t exe_size = 0;
  void *exe = load_elf_into_memory(exe_name, &exe_size);
  code_module *cm = load_mcfi_metadata(exe, exe_size);
  DL_APPEND(modules, cm);
  cm->base_addr = X64ABIBASE;

  /* copy the modified code into the loaded region */
  replace_prog_seg((void*)X64ABIBASE, exe);
  munmap(exe, exe_size);
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

  /* copy data from kernel-allocated stack to sandbox-stack */
  stack_init();

  /* load mcfi metadata for the exe */
  load_bid_rewritten_exe(argv[0]);

  return stack;
}
