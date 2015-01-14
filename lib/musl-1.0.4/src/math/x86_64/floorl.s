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
        jmpq *%rcx
check:
        movq %gs:(%rcx), %rsi
        testb $0x1, %sil
        jne die
        cmpl %esi, %edi
        jne try
die:
        hlt

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
	.ascii "{ floorl\nTY x86_fp80!x86_fp80@\nRT floorl\n}"
	.byte 0
        .ascii "{ truncl\nTY x86_fp80!x86_fp80@\nDT floorl\n}"
	.byte 0
	.ascii "{ ceill\nTY x86_fp80!x86_fp80@\nDT floorl\n}"
	.byte 0
