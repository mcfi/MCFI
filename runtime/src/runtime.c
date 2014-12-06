#include <def.h>
#include <syscall.h>
#include <mm.h>
#include <io.h>
#include <string.h>
#include <tcb.h>
#include <errno.h>
#include "pager.h"

static void* prog_brk;

extern struct Vmmap VM;

void set_tcb(unsigned long sb_tcb) {
  if (sb_tcb > FourGB) {
    report_error("[set_tcb] sandbox tcb is out of sandbox\n");
  }

  TCB* tcb = thread_self();
  tcb->tcb_inside_sandbox = (void*)sb_tcb;
}

void* allocset_tcb(unsigned long sb_tcb) {
  TCB* tcb = alloc_tcb();
  
  if (sb_tcb > FourGB) {
    report_error("[set_tcb] sandbox tcb is out of sandbox\n");
  }
  
  tcb->tcb_inside_sandbox = (void*)sb_tcb;
  return tcb;
}

void free_tcb(unsigned long tcb) {

}

void rock_patch(unsigned long patchpoint) {
}

void *rock_mmap(void *start, size_t len, int prot, int flags, int fd, off_t off) {
  /* executable page mapping is not allowed
  if (prot & PROT_EXEC) {
    dprintf(STDERR_FILENO,
            "[rock_mmap] mmap(%p, %lx, %d, %d, %d, %ld) maps executable pages!\n",
            addr, len, prot, flags, fd, off);
    quit(-1);
    }*/
  /* return mmap(start, len, prot, flags | MAP_32BIT, fd, off); */
  void *result = MAP_FAILED;
  uintptr_t page = 0;
  size_t pages = RoundToPage(len) >> PAGESHIFT;

  if ((unsigned long)start & ((1<<PAGESHIFT)-1)) {
    return (void*)-EINVAL;
  }
  
  /* if the program tries to map a fixed out-sandbox address, return failure */
  if ((unsigned long)start > FourGB && (flags & MAP_FIXED)) {
    return (void*)-ENOMEM;
  }

  /* not fixed mapping */
  if (!(flags & MAP_FIXED)) {
    if ((unsigned long)start > FourGB || start == 0)
      page = VmmapFindSpace(&VM, pages);
    else
      page = VmmapFindMapSpaceAboveHint(&VM, (uintptr_t)start, pages);

    /* no memory is available */
    if (!page) {
      return (void*)-ENOMEM;
    }
  } else {
    /* fixed mapping, check whether the mapping would be safe. */
    if (len > FourGB || page + len > FourGB)
      return (void*)-ENOMEM;
    page = (uintptr_t)start >> PAGESHIFT;
  }
  result = mmap((void*)(page << PAGESHIFT), len, prot, flags | MAP_FIXED, fd, off);
  if (result == (void*)(page << PAGESHIFT))
    VmmapAddWithOverwrite(&VM, page,
                          pages,
                          prot,
                          prot,
                          VMMAP_ENTRY_ANONYMOUS);
  return result;
}

/**
 * Trusted mprotect that guarantees W xor X. The attackers may set executable
 * pages to be writable, but that drops the executable permissions and will
 * crash the program.
 */
int rock_mprotect(void *addr, size_t len, int prot) {
  if ((unsigned long) addr > FourGB || len > FourGB) {
    dprintf(STDERR_FILENO, "[rock_mprotect] mprotect(%ld, %lx, %d) is insecure!\n",
            (size_t)addr, len, prot);
    quit(-1);
  }
  return mprotect(addr, len, prot);
}

int rock_munmap(void *start, size_t len) {
  /* return munmap(start, len); */
  if ((unsigned long)start > FourGB ||
      (unsigned long)len > FourGB ||
      (unsigned long)start + len > FourGB) {
    dprintf(STDERR_FILENO, "[rock_munmap] munmap(%ld, %lx) is insecure!\n",
            (size_t)start, len);
    quit(-1);
  }

  int rv = munmap(start, len);
  if(!rv) {
    VmmapRemove(&VM, RoundToPage((uintptr_t)start) >> PAGESHIFT,
                RoundToPage(len) >> PAGESHIFT, VMMAP_ENTRY_ANONYMOUS);
  }
  return rv;
}

void *rock_mremap(void *old_addr, size_t old_len, size_t new_len,
                  int flags, void* new_addr) {
  void *ptr = rock_mmap(0, new_len, PROT_READ | PROT_WRITE,
                        MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if ((long)ptr > 0) {
    memcpy(ptr, old_addr, old_len > new_len ? new_len : old_len);
    rock_munmap(old_addr, old_len);
  }
  return ptr;
}

void* rock_brk(void* newbrk) {
  return __syscall1(SYS_brk, newbrk);
}

void load_native_code(const char* code_file_name) {
}

void unload_native_code(const char* code_file_name) {
}

void rock_clone(void) {
}

void rock_execve(void) {
}

void rock_shmat(void) {
}

void rock_shmdt(void) {
}
