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

void install_trampolines(void) {
}

void load_user_loader(void) {
}

/* main function of the runtime */
void* runtime_init(int argc, char **argv) {
  
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

  /* install the trampolines to 64KB to 128KB*/
  install_trampolines();
  
  /* load the dynamic loader of the user program */
  load_user_loader();
  dprintf(2, "load it \n");
  quit(0);
  return 0;
}
