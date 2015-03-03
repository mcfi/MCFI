.text
.global dlsym
        .align 16, 0x90
.type dlsym,@function
dlsym:
	mov (%rsp),%rdx
	jmp __dlsym

        .section	.MCFIFuncInfo,"",@progbits
        .ascii "{ dlsym\nT __dlsym\n}"
        .byte 0
