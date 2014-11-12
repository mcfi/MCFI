#include <syscall.h>

void exit(int code) {
  __syscall1(SYS_exit_group, code);
}
