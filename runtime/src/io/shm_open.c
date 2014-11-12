#include <mm.h>
#include <io.h>
#include <tcb.h>

int shm_open(const char *name, int flag, mode_t mode)
{
  int cs;
  //pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cs);
  int fd = open(name, flag|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK, mode);
  //pthread_setcancelstate(cs, 0);
  return fd;
}

int shm_unlink(const char *name)
{  
  return unlink(name);
}
