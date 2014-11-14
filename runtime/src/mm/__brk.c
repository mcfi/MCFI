#include <def.h>
#include <syscall.h>

uintptr_t __brk(uintptr_t newbrk)
{
  return (uintptr_t)__syscall1(SYS_brk, (long)newbrk);
}
