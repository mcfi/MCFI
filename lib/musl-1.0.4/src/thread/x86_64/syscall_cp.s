        .text
        .global __syscall_cp_asm
        .align 16, 0x90
        .type   __syscall_cp_asm,@function
__syscall_cp_asm:

        .global __cp_begin
__cp_begin:
	mov (%rdi),%eax
	test %eax,%eax
	jnz __cancel
	mov %rdi,%r11
	mov %rsi,%rax
	mov %rdx,%rdi
	mov %rcx,%rsi
	mov %r8,%rdx
	mov %r9,%r10
	mov 8(%rsp),%r8
	mov 16(%rsp),%r9
	mov %r11,8(%rsp)
        movb   $0x1,%fs:0x18  # enter_syscall
	syscall
        movq   %fs:0x10, %r11 # read thread_escape
        addq   $1, %r11
        movq   %r11, %fs:0x10 # write thread_escape back
        movb   $0x0,%fs:0x18  # exit_syscall
.global __cp_end
__cp_end:
	#ret
        popq %rcx
        movl %ecx, %ecx
try:    movq %gs:0x1000, %rdi
__mcfi_bary___syscall_cp_asm:
        movq %gs:(%rcx), %rsi
        cmpq %rdi, %rsi
        jne check
        # addq $1, %fs:0x108 # icj_count
go:
        jmpq *%rcx
check:
        cmpb  $0xfc, %sil
        je    go
        testb $0x1, %sil
        jz die
        cmpl %esi, %edi
        jne try
die:
        leaq try(%rip), %rdi
        jmp __report_cfi_violation_for_return@PLT

        .section	.MCFIFuncInfo,"",@progbits
        .ascii "{ __syscall_cp_asm\nR __syscall_cp_asm\n}"
        .byte 0
