#include <tcb.h>
#include <string.h>
#include <mm.h>

TCB* alloc_tcb(void) {
  void* tcb = mmap(0, STACK_SIZE, PROT_WRITE,
                   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (MAP_FAILED == tcb) {
    // report_error
    quit(-1);
  }
  memset(tcb, 0, STACK_SIZE);
  /* create a guard page between the tcb and the thread-specific stack */
  if (0 != munmap(tcb + PAGE_SIZE, PAGE_SIZE)) {
    // report_error
    quit(-1);
  }
  return (TCB*)tcb;
}
