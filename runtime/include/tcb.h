/*
 * This files defines the Thread Control Block used by the trusted runtime
 * to track each thread outside of the sandbox.
 */

#ifndef TCB_H
#define TCB_H

typedef struct TCB_t {
  /* this will be set to point the the tcb
     inside the sandbox. Access to this
     is through %fs:0
  */
  void*         tcb_inside_sandbox;  /* 0x0 */

  /* the address of this TCB */
  void*         self;                /* 0x8 */
  
  /* sandbox_escape records how many times a thread
     has escaped the sandbox
   */
  unsigned long sandbox_escape;      /* 0x10 */

  /*
    reserved fields
  */  
  void*         unused1;             /* 0x18 */
  void*         unused2;             /* 0x20 */

  /* stack canary according to the x64 ABI */
  unsigned long canary;              /* 0x28 */
} TCB;

static TCB* thread_self(void) {
  TCB *self;
  __asm__ __volatile__("mov %%fs:8,%0" : "=r" (self) );
  return self;
}

static unsigned long thread_escape(void) {
  unsigned long numEscape;
  __asm__ __volatile__("mov %%fs:16,%0" : "=r" (numEscape));
  return numEscape;
}

#endif
