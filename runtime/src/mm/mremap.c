#include <mm.h>
#include <syscall.h>

void *mremap(void *old_addr, size_t old_len, size_t new_len, int flags, void* new_addr)
{
  return (void *)__syscall5(SYS_mremap, old_addr, old_len, new_len, flags, new_addr);
}
