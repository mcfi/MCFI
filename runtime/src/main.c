#include <elf.h>
#include <tcb.h>
#include <mm.h>
#include <io.h>
#include <string.h>
#include <syscall.h>
#include <errno.h>
#include <stdarg.h>
#include "pager.h"

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
  end = 0x400000;
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
    void *load_native_code;
    void *unload_native_code;
    void *create_code_heap;
    void *dyncode_install;
    void *dyncode_modify;
    void *dyncode_delete;
    void *report_cfi_violation;
    void *online_patch;
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
  tp->mmap = &runtime_rock_mmap;
  tp->mprotect = &runtime_rock_mprotect;
  tp->munmap = &runtime_rock_munmap;
  tp->mremap = &runtime_rock_mremap;
  tp->brk = &runtime_rock_brk;
  tp->set_tcb = &runtime_set_tcb;
  tp->load_native_code = &runtime_load_native_code;
  tp->unload_native_code = &runtime_unload_native_code;
  tp->create_code_heap = &runtime_create_code_heap;
  tp->dyncode_install = &runtime_dyncode_install;
  tp->dyncode_modify = &runtime_dyncode_modify;
  tp->dyncode_delete = &runtime_dyncode_delete;
  tp->report_cfi_violation = &runtime_report_cfi_violation;
  tp->online_patch = &runtime_online_patch;

  if (0 != mprotect(table,  PAGE_SIZE, PROT_READ)) {
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
  int i;

  top -= lt_stack_size;
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

  return stack_top - lt_stack_size;
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
  
  for (i = 0; lt_envp[i]; i++)
    lt_stack_size += (strlen(lt_envp[i]) + 1); /* each envp[i] length */

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

  lt_stack_size = (lt_stack_size + 15)/16*16;/* ABI requires stack pointer to 16-byte aligned */
}

void load_libc(void) {
  int fd = open("libc.so", O_RDONLY, 0);
  if (fd == -1) {
    dprintf(STDERR_FILENO, "[load_libc] libc open failed with %d\n", errn);
    quit(-1);
  }
  char *libc_elf= mmap(0, (1 << 23),
                       PROT_READ | PROT_WRITE, MAP_PRIVATE,
                       fd, 0);
  if (libc_elf == MAP_FAILED) {
    dprintf(STDERR_FILENO, "[load_libc] memory allocation failed\n");
    quit(-1);
  }

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
  if (phdr_vaddr_end >= (1 << 23)) {
    dprintf(STDERR_FILENO, "[load_libc] suspicious libc is provided\n");
    quit(-1);
  }
  
  /* find a consecutive region of memory */
  uintptr_t libc = VmmapFindSpace(&VM, RoundToPage(phdr_vaddr_end) >> PAGESHIFT);

  if (libc == 0) {
    dprintf(STDERR_FILENO, "[load_libc] sandbox mem alloc failed\n");
    quit(-1);
  }

  libc <<= PAGESHIFT;
  libc_base = (void*)libc;
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
        if (mprotect(content, RoundToPage(phdr->p_memsz), PROT_EXEC | PROT_READ)) {
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
  VmmapDebug(&VM, "After libc loaded\n");
  munmap(libc_elf, (1 << 23));
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
  alloc_tcb();
  
  /* load the libc */
  load_libc();

  /* copy data from kernel-allocated stack to sandbox-stack */
  stack_init();
  
  return stack;
}
