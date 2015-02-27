.section .init
	pop %rax
        #ret
        popq %rcx
        movl %ecx, %ecx
try1:   movq %gs:0x1000, %rdi
__mcfi_bary__libc_init:
	cmpq %rdi, %gs:(%rcx)
        jne check1
        jmpq *%rcx
check1:
        movq %gs:(%rcx), %rsi
        testb $0x1, %sil
        jz die1
        cmpl %esi, %edi
        jne try1
die1:
        hlt

.section .fini
	pop %rax
	#ret
        popq %rcx
        movl %ecx, %ecx
try2:   movq %gs:0x1000, %rdi
__mcfi_bary__libc_fini:
	cmpq %rdi, %gs:(%rcx)
        jne check2
        jmpq *%rcx
check2:
        movq %gs:(%rcx), %rsi
        testb $0x1, %sil
        jz die2
        cmpl %esi, %edi
        jne try2
die2:
        hlt

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
