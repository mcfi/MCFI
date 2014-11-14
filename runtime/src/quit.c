#include <syscall.h>

void quit(int code) {
  __syscall1(SYS_exit_group, code);
}
