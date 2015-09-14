.section .init
	pop %rax
        #ret
        popq %rcx
        movl %ecx, %ecx
try1:   movq %gs:0x1000, %rdi
__mcfi_bary__libc_init:
        movq %gs:(%rcx), %rsi
	cmpq %rdi, %rsi
        jne check1
        # addq $1, %fs:0x108 # icj_count
        jmpq *%rcx
check1:
        testb $0x1, %sil
        jz die1
        cmpl %esi, %edi
        jne try1
die1:
        leaq try1(%rip), %rdi
        jmp __report_cfi_violation_for_return@PLT

.section .fini
	pop %rax
	#ret
        popq %rcx
        movl %ecx, %ecx
try2:   movq %gs:0x1000, %rdi
__mcfi_bary__libc_fini:
        movq %gs:(%rcx), %rsi
	cmpq %rdi, %rsi
        jne check2
        # addq $1, %fs:0x108 # icj_count
        jmpq *%rcx
check2:
        testb $0x1, %sil
        jz die2
        cmpl %esi, %edi
        jne try2
die2:
        leaq try2(%rip), %rdi
        jmp __report_cfi_violation_for_return@PLT

        # trigger generation of a PLT entry for __patch_call
        jmp __patch_call@PLT
        # trigger generation of a PLT entry for __patch_at
        jmp __patch_at@PLT
        # trigger generation of a PLT entry for __patch_entry
        jmp __patch_entry@PLT

        .section	.MCFIFuncInfo,"",@progbits
        .ascii "{ _init\nY void!\nR _libc_init\n}"
        .byte 0
        .ascii "{ _fini\nY void!\nR _libc_fini\n}"
        .byte 0

        .section        .MCFIAddrTaken,"",@progbits
        .ascii "_init"
        .byte 0
        .ascii "_fini"
        .byte 0
