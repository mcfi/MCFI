        .global atan2l
        .align 16, 0x90
        .type atan2l,@function
atan2l:
	fldt 8(%rsp)
	fldt 24(%rsp)
	fpatan
	#ret
        popq %rcx
        movl %ecx, %ecx
try:    movq %gs:0x1000, %rdi
__mcfi_bary_atan2l:
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
        .ascii	"{ atan2l\nY x86_fp80!x86_fp80@x86_fp80@\nR atan2l\n}"
	.byte	0
