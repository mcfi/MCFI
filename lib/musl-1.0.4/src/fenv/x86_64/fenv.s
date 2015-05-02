        .global feclearexcept
        .align 16, 0x90
        .type feclearexcept,@function
feclearexcept:
		# maintain exceptions in the sse mxcsr, clear x87 exceptions
	mov %edi,%ecx
	and $0x3f,%ecx
	fnstsw %ax
	test %eax,%ecx
	jz 1f
	fnclex
1:	stmxcsr -8(%rsp)
	and $0x3f,%eax
	or %eax,-8(%rsp)
	test %ecx,-8(%rsp)
	jz 1f
	not %ecx
	and %ecx,-8(%rsp)
	ldmxcsr -8(%rsp)
1:	xor %eax,%eax
	#ret
        popq %rcx
        movl %ecx, %ecx
try1:   movq %gs:0x1000, %rdi
__mcfi_bary_feclearexcept:
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

        .global feraiseexcept
        .align 16, 0x90
        .type feraiseexcept,@function
feraiseexcept:
	and $0x3f,%edi
	stmxcsr -8(%rsp)
	or %edi,-8(%rsp)
	ldmxcsr -8(%rsp)
	xor %eax,%eax
	#ret
        popq %rcx
        movl %ecx, %ecx
try2:   movq %gs:0x1000, %rdi
__mcfi_bary_feraiseexcept:
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


        .global __fesetround
        .align 16, 0x90
        .type __fesetround,@function
__fesetround:
	push %rax
	xor %eax,%eax
	mov %edi,%ecx
	fnstcw (%rsp)
	andb $0xf3,1(%rsp)
	or %ch,1(%rsp)
	fldcw (%rsp)
	stmxcsr (%rsp)
	shl $3,%ch
	andb $0x9f,1(%rsp)
	or %ch,1(%rsp)
	ldmxcsr (%rsp)
	pop %rcx
	#ret
        popq %rcx
        movl %ecx, %ecx
try3:   movq %gs:0x1000, %rdi
__mcfi_bary___fesetround:
        cmpq %rdi, %gs:(%rcx)
        jne check3
        # addq $1, %fs:0x108 # icj_count
        jmpq *%rcx
check3:
        movq %gs:(%rcx), %rsi
        testb $0x1, %sil
        jz die3
        cmpl %esi, %edi
        jne try3
die3:
        leaq try3(%rip), %rdi
        jmp __report_cfi_violation_for_return@PLT

        .global fegetround
        .align 16, 0x90
        .type fegetround,@function
fegetround:
	push %rax
	stmxcsr (%rsp)
	pop %rax
	shr $3,%eax
	and $0xc00,%eax
	#ret
        popq %rcx
        movl %ecx, %ecx
try4:   movq %gs:0x1000, %rdi
__mcfi_bary_fegetround:
        cmpq %rdi, %gs:(%rcx)
        jne check4
        # addq $1, %fs:0x108 # icj_count
        jmpq *%rcx
check4:
        movq %gs:(%rcx), %rsi
        testb $0x1, %sil
        jz die4
        cmpl %esi, %edi
        jne try4
die4:
        leaq try4(%rip), %rdi
        jmp __report_cfi_violation_for_return@PLT


        .global fegetenv
        .align 16, 0x90
        .type fegetenv,@function
fegetenv:
	xor %eax,%eax
	fnstenv (%edi)
	stmxcsr 28(%edi)
	#ret
        popq %rcx
        movl %ecx, %ecx
try5:   movq %gs:0x1000, %rdi
__mcfi_bary_fegetenv:
        cmpq %rdi, %gs:(%rcx)
        jne check5
        # addq $1, %fs:0x108 # icj_count
        jmpq *%rcx
check5:
        movq %gs:(%rcx), %rsi
        testb $0x1, %sil
        jz die5
        cmpl %esi, %edi
        jne try5
die5:
        leaq try5(%rip), %rdi
        jmp __report_cfi_violation_for_return@PLT

        .global fesetenv
        .align 16, 0x90
        .type fesetenv,@function
fesetenv:
	xor %eax,%eax
	inc %rdi
	jz 1f
	fldenv -1(%rdi)
	ldmxcsr 27(%rdi)
	jmp 2f
1:	push %rax
	push %rax
	pushq $0xffff
	pushq $0x37f
	fldenv (%rsp)
	pushq $0x1f80
	ldmxcsr (%rsp)
	add $40,%esp
	#ret
2:
        popq %rcx
        movl %ecx, %ecx
try6:   movq %gs:0x1000, %rdi
__mcfi_bary_fesetenv:
        cmpq %rdi, %gs:(%rcx)
        jne check6
        # addq $1, %fs:0x108 # icj_count
        jmpq *%rcx
check6:
        movq %gs:(%rcx), %rsi
        testb $0x1, %sil
        jz die6
        cmpl %esi, %edi
        jne try6
die6:
        leaq try6(%rip), %rdi
        jmp __report_cfi_violation_for_return@PLT

        .global fetestexcept
        .align 16, 0x90
        .type fetestexcept,@function
fetestexcept:
	and $0x3f,%edi
	push %rax
	stmxcsr (%rsp)
	pop %rsi
	fnstsw %ax
	or %esi,%eax
	and %edi,%eax
	#ret
        popq %rcx
        movl %ecx, %ecx
try7:   movq %gs:0x1000, %rdi
__mcfi_bary_fetestexcept:
        cmpq %rdi, %gs:(%rcx)
        jne check7
        # addq $1, %fs:0x108 # icj_count
        jmpq *%rcx
check7:
        movq %gs:(%rcx), %rsi
        testb $0x1, %sil
        jz die7
        cmpl %esi, %edi
        jne try7
die7:
        leaq try7(%rip), %rdi
        jmp __report_cfi_violation_for_return@PLT

        .section	.MCFIFuncInfo,"",@progbits
        .ascii	"{ feclearexcept\nY i32!i32@\nR feclearexcept\n}"
	.byte	0
        .ascii	"{ feraiseexcept\nY i32!i32@\nR feraiseexcept\n}"
	.byte	0
        .ascii	"{ fegetround\nY i32!\nR fegetround\n}"
	.byte	0
	.ascii	"{ fegetenv\nY i32!%struct.fenv_t*@\nR fegetenv\n}"
	.byte	0
	.ascii	"{ fesetenv\nY i32!%struct.fenv_t*@\nR fesetenv\n}"
	.byte	0
	.ascii	"{ fetestexcept\nY i32!i32@\nR fetestexcept\n}"
	.byte	0
	.ascii	"{ __fesetround\nR __fesetround\n}"
	.byte	0
