#define SELF           0x08
#define THREAD_ESCAPES 0x10
#define IN_SYSCALL     0x18
#define CONTINUATION   0x20
#define USER_CTX       0x30
#define STACK_SIZE     $0x10000
#define FCW            0x80
#define MXCSR          0x88

# empty state of the SSE and FP control status
#        .rodata
#        .balign 64
#fxrstor_default_state:
#        .space 512

        .data
locked:
        .byte 0

        .text

.macro spin_lock
1:
        movb $1, %r11b
        lock
        xchgb %r11b, locked(%rip)
        testb %r11b, %r11b
        jnz 1b
.endm

.macro spin_unlock
        movb $0, %r11b
        lock
        xchgb %r11b, locked(%rip)
.endm
        
.macro switch_runtime_stack
        movq %fs:SELF, %rsp
        addq $0xffc0, %rsp # %rsp should be 16-bit aligned
.endm

.macro atomic_incr_thread_escapes scratchreg=%rax
        movq %fs:THREAD_ESCAPES, \scratchreg
        addq $1, \scratchreg
        movq \scratchreg, %fs:THREAD_ESCAPES
.endm

        .global runtime_online_patch
runtime_online_patch:
        # entering a trusted call
        movb $1, %fs:IN_SYSCALL

        # registers that might be clobbered during rock_patch should be saved
        movq %rax, %fs:USER_CTX
        movq %rcx, %fs:USER_CTX+0x10
        movq %rdx, %fs:USER_CTX+0x18
        movq %rdi, %fs:USER_CTX+0x20
        movq %rsi, %fs:USER_CTX+0x28
        movq %rsp, %fs:USER_CTX+0x38
        movq %r8,  %fs:USER_CTX+0x40
        movq %r9,  %fs:USER_CTX+0x48
        movq %r10, %fs:USER_CTX+0x50

        # XMMs
        movq %xmm0, %fs:USER_CTX+0x90
        movq %xmm1, %fs:USER_CTX+0x98
        movq %xmm2, %fs:USER_CTX+0x100
        movq %xmm3, %fs:USER_CTX+0x108
        movq %xmm4, %fs:USER_CTX+0x110
        movq %xmm5, %fs:USER_CTX+0x118
        movq %xmm6, %fs:USER_CTX+0x120
        movq %xmm7, %fs:USER_CTX+0x128

        # st(0) and st(1) do not need to be saved, since
        # rock_patch would not deal with long double

        # control and status words
        stmxcsr %fs:USER_CTX+MXCSR
        fnstcw  %fs:USER_CTX+FCW

        # atomically increase the number of escapes
        atomic_incr_thread_escapes
        # load system stack pointer
        switch_runtime_stack
        movq %r11, %rdi
        spin_lock
        callq rock_patch
        spin_unlock
        # restore states
        movq %fs:USER_CTX, %rax
        movq %fs:USER_CTX+0x10, %rcx
        movq %fs:USER_CTX+0x18, %rdx
        movq %fs:USER_CTX+0x20, %rdi
        movq %fs:USER_CTX+0x28, %rsi
        movq %fs:USER_CTX+0x38, %rsp
        movq %fs:USER_CTX+0x40, %r8
        movq %fs:USER_CTX+0x48, %r9
        movq %fs:USER_CTX+0x50, %r10
        movq %fs:USER_CTX+0x58, %r11

        # XMMs
        movq %fs:USER_CTX+0x90,  %xmm0
        movq %fs:USER_CTX+0x98,  %xmm1
        movq %fs:USER_CTX+0x100, %xmm2
        movq %fs:USER_CTX+0x108, %xmm3
        movq %fs:USER_CTX+0x110, %xmm4
        movq %fs:USER_CTX+0x118, %xmm5
        movq %fs:USER_CTX+0x120, %xmm6
        movq %fs:USER_CTX+0x128, %xmm7

        # restore control and status words
        fldcw   %fs:USER_CTX+FCW
        ldmxcsr %fs:USER_CTX+MXCSR

        # exiting a trusted call
        movb $0, %fs:IN_SYSCALL

        # jump to the place
        jmpq *%fs:CONTINUATION

# The ABI requires that fcw and mxcsr are both callee-saved, so
# we do not need to save them.
.macro save_context
        movq %rsp, %fs:USER_CTX+0x38
.endm
.macro restore_context
        movq %fs:USER_CTX+0x38, %rsp
.endm

.macro runtime_function func
        .global runtime_\func
runtime_\func:
        movb $1, %fs:IN_SYSCALL # entering a trusted call
        save_context
        atomic_incr_thread_escapes
        # load system stack pointer
        switch_runtime_stack
        spin_lock
        callq \func
        spin_unlock
        restore_context
        movb $0, %fs:IN_SYSCALL # exiting a trusted call
        jmpq *%fs:CONTINUATION
.endm
        runtime_function rock_mmap
        runtime_function rock_mprotect
        runtime_function rock_munmap
        runtime_function rock_mremap
        runtime_function rock_brk
        runtime_function rock_clone
        runtime_function rock_execve
        runtime_function rock_shmat
        runtime_function set_tcb
        runtime_function allocset_tcb
        runtime_function free_tcb
        runtime_function load_native_code
        runtime_function gen_cfg
        runtime_function unload_native_code
        runtime_function create_code_heap
        runtime_function dyncode_install
        runtime_function dyncode_modify
        runtime_function dyncode_delete
        runtime_function report_cfi_violation
        runtime_function take_addr_and_gen_cfg
        runtime_function set_gotplt
        runtime_function rock_fork
        runtime_function collect_stat
        runtime_function reg_cfg_metadata
        runtime_function add_cfg_edge_combo
