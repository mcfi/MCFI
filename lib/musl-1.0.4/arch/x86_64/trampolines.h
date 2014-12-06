#ifndef MCFI_TRAMPOLINES_H
#define MCFI_TRAMPOLINES_H
#define ROCK_MMAP         0
#define ROCK_MPROTECT     0x8
#define ROCK_MUNMAP       0x10
#define ROCK_BRK          0x18
#define ROCK_SIGACTION    0x20
#define ROCK_READV        0x28
#define ROCK_MREMAP       0x30
#define ROCK_SHMAT        0x38
#define ROCK_CLONE        0x40
#define ROCK_EXECVE       0x48
#define ROCK_SHMDT        0x50
#define ROCK_REMAP_FILE_PAGES 0x58
#define ROCK_PREADV       0x60
#define ROCK_PROCESS_VM_READY 0x68
#define ROCK_SET_TCB      0x70
#define ROCK_ALLOCSET_TCB 0x78
#define ROCK_FREE_TCB     0x80

#define STRING(x) #x
#define XSTR(x) STRING(x)

#define TRAMP_CALL(x) "leaq 1f(%%rip), %%r11\n\t"       \
  "movq %%r11, %%fs:0x20\n\t"                           \
  "jmpq *%%gs:"XSTR(x)"\n\t"                            \
  "1:\n\t": "=a"(ret) :                                 \

static __inline
long trampoline_mmap(unsigned long n1, unsigned long n2, int n3,
                     int n4, int n5, long n6)
{
  long ret;
  register int r8 __asm__("r8") = n5;
  register long r9 __asm__("r9") = n6;
                
  __asm__ __volatile__(TRAMP_CALL(ROCK_MMAP)
                       "D"(n1), "S"(n2), "d"(n3),
                       "c"(n4), "r"(r8), "r"(r9):
                       "memory");
  return ret;
}

static __inline
long trampoline_mremap(unsigned long n1, unsigned long n2,
                       unsigned long n3,
                       int n4, unsigned long n5)
{
  long ret;
  register int r8 __asm__("r8") = n5;
                
  __asm__ __volatile__(TRAMP_CALL(ROCK_MREMAP)
                       "D"(n1), "S"(n2), "d"(n3),
                       "c"(n4), "r"(r8):
                       "memory");
  return ret;
}

static __inline
long trampoline_mprotect(unsigned long n1, unsigned long n2, int n3) {
  long ret;
  __asm__ __volatile__(TRAMP_CALL(ROCK_MPROTECT)
                       "D"(n1), "S"(n2), "d"(n3):
                       "memory");
  return ret;
}

static __inline
long trampoline_munmap(unsigned long n1, unsigned long n2) {
  long ret;
  __asm__ __volatile__(TRAMP_CALL(ROCK_MUNMAP)
                       "D"(n1), "S"(n2):
                       "memory");
  return ret;
}

static __inline
long trampoline_brk(unsigned long n1) {
  long ret;
  __asm__ __volatile__(TRAMP_CALL(ROCK_BRK)
                       "D"(n1):
                       "memory");
  return ret;
}

static __inline
void trampoline_set_tcb(unsigned long n1) {
  long ret;
  __asm__ __volatile__(TRAMP_CALL(ROCK_SET_TCB)
                       "D"(n1):
                       "memory");
}

static __inline
long trampoline_allocset_tcb(unsigned long n1) {
  long ret;
  __asm__ __volatile__(TRAMP_CALL(ROCK_ALLOCSET_TCB)
                       "D"(n1):
                       "memory");
  return ret;
}

static __inline
void trampoline_free_tcb(unsigned long n1) {
  long ret;
  __asm__ __volatile__(TRAMP_CALL(ROCK_FREE_TCB)
                       "D"(n1):
                       "memory");
}
#endif
