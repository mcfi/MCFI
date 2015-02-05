        .global asinl
        .align 16, 0x90
        .type asinl,@function
asinl:
	fldt 8(%rsp)
1:	fld %st(0)
	fld1
	fsub %st(0),%st(1)
	fadd %st(2)
	fmulp
	fsqrt
	fpatan
	#ret
        popq %rcx
        movl %ecx, %ecx
try:    movq %gs:0x1000, %rdi
__mcfi_bary_asinl:
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
	.ascii	"{ asinl\nY x86_fp80!x86_fp80@\nR asinl\n}"
	.byte	0
