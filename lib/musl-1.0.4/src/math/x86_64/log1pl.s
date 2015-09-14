        .global log1pl
        .align 16, 0x90
        .type log1pl,@function
log1pl:
	mov 14(%rsp),%eax
	fldln2
	and $0x7fffffff,%eax
	fldt 8(%rsp)
	cmp $0x3ffd9400,%eax
	ja 1f
	fyl2xp1
	#ret
        jmp try
1:	fld1
	faddp
	fyl2x
	#ret
try:    movq %gs:0x1000, %rdi
__mcfi_bary_log1pl:
        movq %gs:(%rcx), %rsi
        cmpq %rdi, %rsi
        jne check
        # addq $1, %fs:0x108 # icj_count
        jmpq *%rcx
check:
        testb $0x1, %sil
        jz die
        cmpl %esi, %edi
        jne try
die:
        leaq try(%rip), %rdi
        jmp __report_cfi_violation_for_return@PLT

	.section	.MCFIFuncInfo,"",@progbits
        .ascii	"{ log1pl\nY x86_fp80!x86_fp80@\nR log1pl\n}"
	.byte	0

