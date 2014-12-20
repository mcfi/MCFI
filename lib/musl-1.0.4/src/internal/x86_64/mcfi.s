        .globl __report_invalid_sandbox_escape
        .align 16, 0x90
        .type __report_invalid_sandbox_escape, @function
__report_invalid_sandbox_escape:
        hlt

        .global call_dtor
        .align 16, 0x90
        .type call_dtor,@function
call_dtor:
        push %rbp
        movq %rsp, %rbp
        movq %rdi, %rax
        movq %rsi, %rdi
        callq *%rax
        popq %rbp
        ret

        .global call_exn_dtor
        .align 16, 0x90
        .type call_exn_dtor,@function
call_exn_dtor:
        pushq %rbp
        movq %rsp, %rbp
        movq %rdi, %rax
        movq %rsi, %rdi
        callq *%rax
        popq %rbp
        ret
