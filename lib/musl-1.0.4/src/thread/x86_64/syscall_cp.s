.text
.global __syscall_cp_asm
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
	ret
