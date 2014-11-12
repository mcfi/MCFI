#include <elf.h>
#include <tcb.h>
#include <mm.h>
#include <string.h>
#include <syscall.h>
#include <errno.h>

unsigned errn = 0;

#define SixtyFourKB      ((unsigned long)(1UL << 16))
#define OneTwentyEightKB ((unsigned long)(1UL << 17))
#define FourGB           ((unsigned long)(1UL << 32))
#define STDERR_FILENO    2

/* the table region holding Bary and Tary */
void* table = 0;

/* auxiliary information */
#define AUX_CNT 38

static size_t aux[AUX_CNT] = {0};

void spin_lock(void) {
}

void spin_unlock(void) {
}

static void init_aux(char **envp) {
  unsigned i;
  size_t *auxv;
  for (i = 0; envp[i]; i++) {
    auxv = (void*)(envp + i + 1);
  }

  for (i = 0; auxv[i]; i += 2) {
    if (auxv[i] < AUX_CNT)
      aux[auxv[i]] = auxv[i+1];
  }
}

static void report_error(char* s, unsigned err) {
  write(STDERR_FILENO, s, strlen(s));  /* we can use the standard output now. */
  //write(STDERR_FILENO, &err, sizeof(err));
  exit(-1);
}

static void alloc_sandbox(void) {
  /* reserve memory from 64KB to 8GB, [4GB, 8GB) serves as a guard region. */
  
  void *ptr = mmap((void*)SixtyFourKB, FourGB * 2 - SixtyFourKB,
                   PROT_NONE, MAP_ANONYMOUS | MAP_FIXED | MAP_PRIVATE,
                   -1, 0);
  if (ptr != (void*)SixtyFourKB) {
    report_error("[alloc_sandbox] mmap failed\n", errn);
  }

  /* not each page is required, so we just give the kernel some hints
     to allocate physical pages as late as possible */
  if (0 != madvise(ptr, FourGB - SixtyFourKB, MADV_DONTNEED)) {
    report_error("[alloc_sandbox] madvise failed\n", errn);
  }
}

int arch_prctl(int code, unsigned long addr);

static void reserve_table_region(void) {
  /* reserve another 4GB memory region */
  table = mmap((void*)0, FourGB,
               PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE,
               -1, 0);
  if ((long)table < 0) {
    report_error("[reserve_table_region] mmap failed\n", errn);
  }

  /* not needed now, advise the kernel to map physical pages as late
     as possible */
  if (0 != madvise(table, FourGB, MADV_DONTNEED)) {
    report_error("[reserve_table_region] madvise failed\n", errn);
  }

  /* set %gs */
  if (0 != arch_prctl(ARCH_SET_GS, table)) {
    report_error("[reserve_table_region] arch_prctl failed\n", errn);
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

  exit(0);
  return 0;
}
