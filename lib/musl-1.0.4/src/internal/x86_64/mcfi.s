        .globl __report_invalid_sandbox_escape
        .align 16, 0x90
        .type __report_invalid_sandbox_escape, @function
__report_invalid_sandbox_escape:
        hlt

        .global __call_dtor
        .align 16, 0x90
        .type __call_dtor,@function
__call_dtor:
        push %rbp
        movq %rsp, %rbp
        movq %rdi, %rax
        movq %rsi, %rdi
        callq *%rax
        popq %rbp
        ret

        .global __call_exn_dtor
        .align 16, 0x90
        .type __call_exn_dtor,@function
__call_exn_dtor:
        pushq %rbp
        movq %rsp, %rbp
        movq %rdi, %rax
        movq %rsi, %rdi
        callq *%rax
        popq %rbp
        ret

        .global __call_thread_func
        .align 16, 0x90
        .type __call_thread_func,@function
__call_thread_func:
        pushq %rbp
        movq %rsp, %rbp
        movq %rdi, %rax
        movq %rsi, %rdi
        callq *%rax
        popq %rbp
        ret
