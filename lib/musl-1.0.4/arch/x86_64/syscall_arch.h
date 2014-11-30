#define __SYSCALL_LL_E(x) (x)
#define __SYSCALL_LL_O(x) (x)

static __inline void __syscall_enter(void) {
  __asm__ __volatile__ ("movb $1, %%fs:0x18":::"memory");
}

static __inline void __syscall_exit(void) {
  __asm__ __volatile__ ("movb $0, %%fs:0x18":::"memory");
}

static __inline long __syscall0(long n)
{
	unsigned long ret;
        __syscall_enter();
	__asm__ __volatile__ ("syscall\n\t"
                              "movq %%fs:0x10, %%r11\n\t"
                              "addq $1, %%r11\n\t"
                              "movq %%r11, %%fs:0x10\n\t":
                              "=a"(ret) : "a"(n) : "rcx", "r11", "memory");
        __syscall_exit();
	return ret;
}

static __inline long __syscall1(long n, long a1)
{
	unsigned long ret;
        __syscall_enter();
	__asm__ __volatile__ ("syscall\n\t"
                              "movq %%fs:0x10, %%r11\n\t"
                              "addq $1, %%r11\n\t"
                              "movq %%r11, %%fs:0x10\n\t":
                              "=a"(ret) : "a"(n), "D"(a1) : "rcx", "r11", "memory");
        __syscall_exit();
	return ret;
}

static __inline long __syscall2(long n, long a1, long a2)
{
	unsigned long ret;
        __syscall_enter();
	__asm__ __volatile__ ("syscall\n\t"
                              "movq %%fs:0x10, %%r11\n\t"
                              "addq $1, %%r11\n\t"
                              "movq %%r11, %%fs:0x10\n\t":
                              "=a"(ret) : "a"(n), "D"(a1), "S"(a2)
						  : "rcx", "r11", "memory");
        __syscall_exit();
	return ret;
}

static __inline long __syscall3(long n, long a1, long a2, long a3)
{
	unsigned long ret;
        __syscall_enter();
	__asm__ __volatile__ ("syscall\n\t"
                              "movq %%fs:0x10, %%r11\n\t"
                              "addq $1, %%r11\n\t"
                              "movq %%r11, %%fs:0x10\n\t":
                              "=a"(ret) : "a"(n), "D"(a1), "S"(a2),
						  "d"(a3) : "rcx", "r11", "memory");
        __syscall_exit();
	return ret;
}

static __inline long __syscall4(long n, long a1, long a2, long a3, long a4)
{
	unsigned long ret;
	register long r10 __asm__("r10") = a4;
        __syscall_enter();
	__asm__ __volatile__ ("syscall\n\t"
                              "movq %%fs:0x10, %%r11\n\t"
                              "addq $1, %%r11\n\t"
                              "movq %%r11, %%fs:0x10\n\t":
                              "=a"(ret) : "a"(n), "D"(a1), "S"(a2),
						  "d"(a3), "r"(r10): "rcx", "r11", "memory");
        __syscall_exit();
	return ret;
}

static __inline long __syscall5(long n, long a1, long a2, long a3, long a4, long a5)
{
	unsigned long ret;
	register long r10 __asm__("r10") = a4;
	register long r8 __asm__("r8") = a5;
        __syscall_enter();
	__asm__ __volatile__ ("syscall\n\t"
                              "movq %%fs:0x10, %%r11\n\t"
                              "addq $1, %%r11\n\t"
                              "movq %%r11, %%fs:0x10\n\t":
                              "=a"(ret) : "a"(n), "D"(a1), "S"(a2),
						  "d"(a3), "r"(r10), "r"(r8) : "rcx", "r11", "memory");
        __syscall_exit();
	return ret;
}

static __inline long __syscall6(long n, long a1, long a2, long a3, long a4, long a5, long a6)
{
	unsigned long ret;
	register long r10 __asm__("r10") = a4;
	register long r8 __asm__("r8") = a5;
	register long r9 __asm__("r9") = a6;
        __syscall_enter();
	__asm__ __volatile__ ("syscall\n\t"
                              "movq %%fs:0x10, %%r11\n\t"
                              "addq $1, %%r11\n\t"
                              "movq %%r11, %%fs:0x10\n\t":
                              "=a"(ret) : "a"(n), "D"(a1), "S"(a2),
						  "d"(a3), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
        __syscall_exit();
	return ret;
}
