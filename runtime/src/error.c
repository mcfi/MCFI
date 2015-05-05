/**
 * Error handling of the runtime
 * The basic idea is to create an error report file
 * with details.
 */

#include <errno.h>
#include <syscall.h>
#include <io.h>
#include <stdarg.h>
#ifdef VERBOSE
#include <cfggen/cfggen.h>
#endif

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
  
  int fd = open(ROCK_ERROR_REPORT, O_WRONLY | O_CREAT,
                S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH); /* 0644 */
  if (fd == -1)
    goto hell;
  
  write(fd, buf, i);
  close(fd);
 hell:
  quit(-1);
}

#ifdef VERBOSE
extern code_module *modules;
extern void *table;
#endif

void report_cfi_violation(unsigned long icf,
                          unsigned long target) {
  unsigned int pid = __syscall0(SYS_getpid);
  dprintf(STDERR_FILENO,
          "\n[%u] CFI violated: "
          "indirect branch at 0x%lx illegally targeted 0x%lx\n\n",
          pid, icf, target);

#ifdef VERBOSE
  {
    code_module *m;
    unsigned int bidslot = *(unsigned int*)(icf + 5);
    unsigned long bid = *(unsigned long*)((char*)table + bidslot);
    DL_FOREACH(modules, m) {
      unsigned long *p = (unsigned long*)((char*)table + m->base_addr);
      size_t sz = m->sz / sizeof(unsigned long);
      size_t i;
      for (i = 0; i < sz; i++) {
        if (p[i] == bid)
          dprintf(STDERR_FILENO, "%x\n", (unsigned long)p + 8*i - (unsigned long)table);
      }
    }
  }
#endif

  report_error("[%u] CFI violated: "
               "indirect branch at 0x%lx illegally targeted 0x%lx\n",
               pid, icf, target);
}
