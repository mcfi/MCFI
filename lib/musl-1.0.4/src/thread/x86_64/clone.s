.text
.global __clone
.type   __clone,@function
__clone:
	xor %eax,%eax
	mov $56,%al
	mov %rdi,%r11
	mov %rdx,%rdi
	mov %r8,%rdx
	mov %r9,%r8
	mov 8(%rsp),%r10
	mov %r11,%r9
	and $-16,%rsi
	sub $8,%rsi
	mov %rcx,(%esi)
	syscall
	test %eax,%eax
	jnz 1f
	xor %ebp,%ebp
	pop %rdi
        movl %r9d, %r9d
try1:   movq %gs:0x1000, %r11
__mcfi_bary_thread_func:
        cmpq %r11, %gs:(%r9)
        jne check1
        .byte 0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00
	call *%r9
__mcfi_icj_thread_func:
	mov %eax,%edi
	xor %eax,%eax
	mov $60,%al
	syscall
	hlt
1:	#ret
        popq %rcx
        movl %ecx, %ecx
try:    movq %gs:0x1000, %rdi
__mcfi_bary___clone:
        cmpq %rdi, %gs:(%rcx)
        jne check
        jmpq *%rcx
check:
        movq %gs:(%rcx), %rsi
        testb $0x1, %sil
        jne die
        cmpl %esi, %edi
        jne try
die:
        hlt

check1:
        movq %gs:(%r9), %r10
        testb $0x1, %r10b
        jne die1
        cmpl %r11d, %r10d
        jne try1
die1:
        hlt
