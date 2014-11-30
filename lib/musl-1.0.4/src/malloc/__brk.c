#include <stdint.h>
#include "syscall.h"
#include "trampolines.h"

uintptr_t __brk(uintptr_t newbrk)
{
  //return __syscall(SYS_brk, newbrk);
  return (uintptr_t)trampoline_brk(newbrk);
}
