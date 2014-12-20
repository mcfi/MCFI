.text
.global dlsym
        .align 16, 0x90
.type dlsym,@function
dlsym:
	mov (%rsp),%rdx
	jmp __dlsym
