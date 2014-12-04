/* Runtime support for C++ */

typedef void (*func_ptr) (void);

/* __dso_handle */
/* Itanium C++ ABI requires that each shared object (*.so) should have its
   own __dso_handle, and when that shared object is unloaded, the __dso_handle
   is an argument that is passed to __cxa_finalize. For a shared object,
   __dso_handle is assigned itself, for the main executable, __dso_handle is 0
*/
#ifdef SHARED
void *__dso_handle = &__dso_handle;
#else
void *__dso_handle = 0;
#endif

extern void __cxa_finalize (void *) __attribute__((weak));

static void __attribute__((used))
__do_global_dtors_aux(void) {
  static int completed;

  if (__builtin_expect (completed, 0))
    return;

#ifdef SHARED
  if (__cxa_finalize)
    __cxa_finalize (__dso_handle);
#endif
  completed = 1;
}

static void __attribute__((used))
__do_global_ctors_aux(void) {
  static int completed;

  if (__builtin_expect (completed, 0))
    return;

  completed = 1;
}

__asm__ (".section .init");
__asm__ (".align 8, 0x90");
__asm__ (".byte 0x0f, 0x1f, 0x00");
__asm__ ("call __do_global_ctors_aux");

__asm__ (".section .fini");
__asm__ (".align 8, 0x90");
__asm__ (".byte 0x0f, 0x1f, 0x00");
__asm__ ("call __do_global_dtors_aux");
