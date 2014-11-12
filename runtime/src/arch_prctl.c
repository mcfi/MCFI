#include <syscall.h>

int arch_prctl(int code, unsigned long addr)
{
  return __syscall2(SYS_arch_prctl, code, addr);
}
