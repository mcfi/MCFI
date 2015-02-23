        .global atanl
        .align 16, 0x90
        .type atanl,@function
atanl:
	fldt 8(%rsp)
	fld1
	fpatan
	#ret
        popq %rcx
        movl %ecx, %ecx
try:    movq %gs:0x1000, %rdi
__mcfi_bary_atanl:
        cmpq %rdi, %gs:(%rcx)
        jne check
        jmpq *%rcx
check:
        movq %gs:(%rcx), %rsi
        testb $0x1, %sil
        jz die
        cmpl %esi, %edi
        jne try
die:
        hlt
        .section	.MCFIFuncInfo,"",@progbits
	.ascii	"{ atanl\nY x86_fp80!x86_fp80@\nR atanl\n}"
	.byte	0
