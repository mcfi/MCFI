/*
 * This files defines the Thread Control Block used by the trusted runtime
 * to track each thread outside of the sandbox.
 */

#ifndef TCB_H
#define TCB_H
#include <def.h>

#define STACK_SIZE 0x10000

struct Context {
  unsigned long rax;                 /* 0x00 */
  unsigned long rbx;                 /* 0x08 */
  unsigned long rcx;                 /* 0x10 */
  unsigned long rdx;                 /* 0x18 */
  unsigned long rdi;                 /* 0x20 */
  unsigned long rsi;                 /* 0x28 */
  unsigned long rbp;                 /* 0x30 */
  unsigned long rsp;                 /* 0x38 */
  unsigned long r8;                  /* 0x40 */
  unsigned long r9;                  /* 0x48 */
  unsigned long r10;                 /* 0x50 */
  unsigned long r11;                 /* 0x58 */
  unsigned long r12;                 /* 0x60 */
  unsigned long r13;                 /* 0x68 */
  unsigned long r14;                 /* 0x70 */
  unsigned long r15;                 /* 0x78 */
  unsigned long fcw;                 /* 0x80 */
  unsigned long mxcsr;               /* 0x88 */
  unsigned long xmm0;                /* 0x90 */
  unsigned long xmm1;                /* 0x98 */
  unsigned long xmm2;                /* 0xa0 */
  unsigned long xmm3;                /* 0xa8 */
  unsigned long xmm4;                /* 0xb0 */
  unsigned long xmm5;                /* 0xb8 */
  unsigned long xmm6;                /* 0xc0 */
  unsigned long xmm7;                /* 0xc8 */
};

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
  /* in-syscall */
  unsigned long insyscall;           /* 0x18 */
  unsigned long continuation;        /* 0x20 */

  /* stack canary according to the x64 ABI */
  unsigned long canary;              /* 0x28 */
  /* application context */
  struct Context user_ctx;           /* 0x30 */
  unsigned long plt;                 /* 0x100 = 0x30 + 0xd0*/
  /* How many indirect branches have been executed */
#ifdef COLLECT_STAT
  unsigned long icj_count;           /* 0x108 */
#endif
  /* next tcb in the tcb list */
  struct TCB_t *next;
  /* this tcb is marked removed and should be reclaimed */
  int remove;
} TCB;

static TCB* thread_self(void) {
  TCB *self;
  __asm__ __volatile__("movq %%fs:0x8, %0" : "=r" (self) );
  return self;
}

static unsigned long thread_escapes(void) {
  unsigned long numEscape;
  __asm__ __volatile__("movq %%fs:0x10, %0" : "=r" (numEscape));
  return numEscape;
}

static bool thread_in_syscall(void) {
  bool in_syscall;
  __asm__ __volatile__("movb %%fs:0x18, %0" : "=r" (in_syscall));
  return in_syscall;
}

TCB* alloc_tcb(void);
void dealloc_tcb(TCB *);
void set_tcb_pointer(TCB *);
#endif
