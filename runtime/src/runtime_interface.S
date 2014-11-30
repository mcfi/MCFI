#define SELF     0x08
#define THREAD_ESCAPES 0x10
#define CONTINUATION  0x20
#define USER_CTX 0x30        
#define STACK_SIZE $0x10000

.macro switch_runtime_stack
        movq %fs:SELF, %rsp
        addq $0xffc0, %rsp
.endm

.macro atomic_incr_thread_escapes scratchreg=%rax
        movq %fs:THREAD_ESCAPES, \scratchreg
        addq $1, \scratchreg
        movq \scratchreg, %fs:THREAD_ESCAPES
.endm

        .text
        .global runtime_online_patch
runtime_online_patch:
        # save the user context first
        movq %rdi, %fs:USER_CTX+0x20
        popq %rdi # pop the return address
        movq (%rsp), %rdi
        movq %rdi, %fs:CONTINUATION # save the called routine
        movq %rax, %fs:USER_CTX
        movq %rcx, %fs:USER_CTX+0x10
        movq %rdx, %fs:USER_CTX+0x18
        movq %rsi, %fs:USER_CTX+0x28
        movq %rsp, %fs:USER_CTX+0x38
        movq %r8,  %fs:USER_CTX+0x40
        movq %r9,  %fs:USER_CTX+0x48
        movq %r10, %fs:USER_CTX+0x50
        # atomically increase the number of escapes
        atomic_incr_thread_escapes
        # load system stack pointer
        switch_runtime_stack
        callq rock_patch
        # patch might return 0 or 5
        subq %rax, %fs:CONTINUATION
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
        # jump to the place
        jmpq *%fs:CONTINUATION
        
.macro save_context
        movq %rsp, %fs:USER_CTX+0x38
.endm
.macro restore_context
        movq %fs:USER_CTX+0x38, %rsp
.endm

# %r11 contains the return address

.macro runtime_function func
        .global runtime_\func
runtime_\func:
        save_context
        movq %r11, %fs:CONTINUATION
        atomic_incr_thread_escapes
        # load system stack pointer
        switch_runtime_stack
        callq \func
        restore_context
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
        runtime_function load_native_code
        runtime_function unload_native_code
        runtime_function create_code_heap
        runtime_function dyncode_install
        runtime_function dyncode_modify
        runtime_function dyncode_delete
        runtime_function report_cfi_violation
