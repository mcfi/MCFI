/**
 * Error handling of the runtime
 * The basic idea is to create an error report file
 * with details.
 */

#include <errno.h>
#include <syscall.h>
#include <io.h>
#include <stdarg.h>

int vsnprintf(char *str, size_t size, const char *format, va_list ap);

#define ROCK_ERROR_REPORT "rock_error_report"
unsigned errn = 0; /* errno for the runtime */

void report_error(const char* fmt, ...) {
  va_list args;
  
  char buf[PAGE_SIZE];
  buf[PAGE_SIZE-1] = '\0';
  int i;
  va_start(args, fmt);
  /* note that only PAGE_SIZE - 1 bytes are allowed */
  i = vsnprintf(buf, PAGE_SIZE - 1, fmt, args);
  va_end(args);
  
  int fd = open(ROCK_ERROR_REPORT, O_WRONLY | O_CREAT, S_IRUSR);
  if (fd == -1)
    goto hell;
  
  write(fd, buf, i);
  close(fd);
 hell:
  quit(-1);
}

void report_cfi_violation(unsigned long icf,
                          unsigned long target) {
  report_error("CFI violation detected:\n\t"
               "0x%lx ==> 0x%lx\n", icf, target);
}
