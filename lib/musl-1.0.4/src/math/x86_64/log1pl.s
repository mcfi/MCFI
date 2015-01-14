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

	.section	.MCFIFuncInfo,"",@progbits
        .ascii	"{ log1pl\nTY x86_fp80!x86_fp80@\nRT log1pl\n}"
	.byte	0

