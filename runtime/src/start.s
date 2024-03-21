/* Entry point of the runtime
   Ben Niu (niuben003@gmail.com)
*/
.text
.global _start
_start:        
	xor %rbp,%rbp           /* mark as zero 0 (ABI) */
        movq (%rsp), %rdi       /* 1nd arg: argc */
        leaq 8(%rsp), %rsi      /* 2rd arg: argv */
	callq runtime_init
        movl %eax, %esp         /* switch to the sandbox stack */
        /* clear all the registers' values */
        xorl %eax, %eax
        xorl %edi, %edi
        xorl %edi, %edi
        jmpq *libc_entry(%rip)  /* runtime_init returns user-program interpreter's entry */

.section        .note.GNU-stack,"",@progbits
