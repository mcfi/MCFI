#include <sys/mman.h>
#include "syscall.h"

int munlockall(void)
{
  return munlock((void*)0x10000, 0xffff0000UL);
}
