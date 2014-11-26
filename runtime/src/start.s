/* Entry point of the runtime
   Ben Niu (niuben003@gmail.com)
*/
.text
.global _start
_start:        
	xor %rbp,%rbp           /* mark as zero 0 (ABI) */
	movq (%rsp),  %rdi      /* 1nd arg: argc */
	leaq 8(%rsp), %rsi      /* 2rd arg: argv */
	callq runtime_init
        movl %eax, %ebp
        movq (%rsp), %rdi
        leaq 8(%rsp), %rsi
        movl %ebp, %edx
        callq stack_init
        movl %eax, %esp
        jmpq *%rbp              /* runtime_init returns user-program interpreter's entry */
