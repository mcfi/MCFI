        .global floorl
        .align 16, 0x90
        .type floorl,@function
floorl:
	fldt 8(%rsp)
1:	mov $0x7,%al
1:	fstcw 8(%rsp)
	mov 9(%rsp),%ah
	mov %al,9(%rsp)
	fldcw 8(%rsp)
	frndint
	mov %ah,9(%rsp)
	fldcw 8(%rsp)
	#ret
        popq %rcx
        movl %ecx, %ecx
try:    movq %gs:0x1000, %rdi
__mcfi_bary_floorl:
        cmpq %rdi, %gs:(%rcx)
        jne check
        # addq $1, %fs:0x108 # icj_count
        jmpq *%rcx
check:
        movq %gs:(%rcx), %rsi
        testb $0x1, %sil
        jz die
        cmpl %esi, %edi
        jne try
die:
        leaq try(%rip), %rdi
        jmp __report_cfi_violation_for_return@PLT

        .global ceill
        .align 16, 0x90
        .type ceill,@function
ceill:
	fldt 8(%rsp)
	mov $0xb,%al
	jmp 1b

        .global truncl
        .align 16, 0x90
        .type truncl,@function
truncl:
	fldt 8(%rsp)
	mov $0xf,%al
	jmp 1b

        .section	.MCFIFuncInfo,"",@progbits
	.ascii "{ floorl\nY x86_fp80!x86_fp80@\nR floorl\n}"
	.byte 0
        .ascii "{ truncl\nY x86_fp80!x86_fp80@\nT floorl\n}"
	.byte 0
	.ascii "{ ceill\nY x86_fp80!x86_fp80@\nT floorl\n}"
	.byte 0
