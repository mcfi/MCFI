#include <syscall.h>

void* __brk(void* newbrk)
{
  return __syscall1(SYS_brk, newbrk);
}
