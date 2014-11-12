#include <tcb.h>
#include <string.h>
#include <mm.h>

TCB* alloc_tcb(void) {
  TCB* tcb = malloc(sizeof(*tcb));
  memset(tcb, 0, sizeof(*tcb));
  tcb->self = tcb;
}
