        .global expm1l
        .align 16, 0x90
        .type expm1l,@function
expm1l:
	fldt 8(%rsp)
	fldl2e
	fmulp
	movl $0xc2820000,-4(%rsp)
	flds -4(%rsp)
	fucomp %st(1)
	fnstsw %ax
	sahf
	fld1
	jb 1f
		# x*log2e <= -65, return -1 without underflow
	fstp %st(1)
	fchs
	jmp 2f
1:	fld %st(1)
	fabs
	fucom %st(1)
	fnstsw %ax
	fstp %st(0)
	fstp %st(0)
	sahf
	ja 1f
	f2xm1
	jmp 2f
1:	push %rax
        .byte 0x0f, 0x1f, 0x40, 0x00
	call 1f
__mcfi_dcj_1_exp2l:
	pop %rax
	fld1
	fsubrp
	#ret
2:
        popq %rcx
        movl %ecx, %ecx
try1:   movq %gs:0x1000, %rdi
__mcfi_bary_expm1l:
        cmpq %rdi, %gs:(%rcx)
        jne check1
        # addq $1, %fs:0x108 # icj_count
        jmpq *%rcx
check1:
        movq %gs:(%rcx), %rsi
        testb $0x1, %sil
        jz die1
        cmpl %esi, %edi
        jne try1
die1:
        leaq try1(%rip), %rdi
        jmp __report_cfi_violation_for_return@PLT

        .global exp2l
        .align 16, 0x90
        .type exp2l,@function
exp2l:
	fldt 8(%rsp)
1:	fld %st(0)
	sub $16,%esp
	fstpt (%rsp)
	mov 8(%rsp),%ax
	and $0x7fff,%ax
	cmp $0x3fff+13,%ax
	jb 4f             # |x| < 8192
	cmp $0x3fff+15,%ax
	jae 3f            # |x| >= 32768
	fsts (%rsp)
	cmpl $0xc67ff800,(%rsp)
	jb 2f             # x > -16382
	movl $0x5f000000,(%rsp)
	flds (%rsp)       # 0x1p63
	fld %st(1)
	fsub %st(1)
	faddp
	fucomp %st(1)
	fnstsw
	sahf
	je 2f             # x - 0x1p63 + 0x1p63 == x
	movl $1,(%rsp)
	flds (%rsp)       # 0x1p-149
	fdiv %st(1)
	fstps (%rsp)      # raise underflow
2:	fld1
	fld %st(1)
	frndint
	fxch %st(2)
	fsub %st(2)       # st(0)=x-rint(x), st(1)=1, st(2)=rint(x)
	f2xm1
	faddp             # 2^(x-rint(x))
1:	fscale
	fstp %st(1)
	add $16,%esp
	jmp 5f
3:	xor %eax,%eax
4:	cmp $0x3fff-64,%ax
	fld1
	jb 1b             # |x| < 0x1p-64
	fstpt (%rsp)
	fistl 8(%rsp)
	fildl 8(%rsp)
	fsubrp %st(1)
	addl $0x3fff,8(%rsp)
	f2xm1
	fld1
	faddp             # 2^(x-rint(x))
	fldt (%rsp)       # 2^rint(x)
	fmulp
	add $16,%esp
5:      #ret
        popq %rcx
        movl %ecx, %ecx
try2:   movq %gs:0x1000, %rdi
__mcfi_bary_exp2l:     
        cmpq %rdi, %gs:(%rcx)
        jne check2
        # addq $1, %fs:0x108 # icj_count
        jmpq *%rcx
check2:
        movq %gs:(%rcx), %rsi
        testb $0x1, %sil
        jz die2
        cmpl %esi, %edi
        jne try2
die2:
        leaq try2(%rip), %rdi
        jmp __report_cfi_violation_for_return@PLT

	.section	.MCFIFuncInfo,"",@progbits
	.ascii	"{ expm1l\nY x86_fp80!x86_fp80@\nR expm1l\n}"
	.byte	0
	.ascii	"{ exp2l\nY x86_fp80!x86_fp80@\nR exp2l\n}"
	.byte	0
