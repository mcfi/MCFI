#include <mm.h>
#include <syscall.h>
#include <errno.h>

void *mremap(void *old_addr, size_t old_len, size_t new_len, int flags, void* new_addr)
{
  long rc = __syscall5(SYS_mremap, (long)old_addr, old_len, new_len, flags, (long)new_addr);
  if (rc < 0) {
    errn = -rc;
    rc = -1;
  }
  return (void*)rc;
}
