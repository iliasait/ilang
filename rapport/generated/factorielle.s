# Genere par ILANG (backend i386, hors perimetre du projet)
	.text

	.globl il_factorielle
il_factorielle:
	pushl %ebp
	movl %esp, %ebp
	movl 8(%ebp), %eax
	pushl %eax
	movl $1, %eax
	movl %eax, %ecx
	popl %eax
	cmpl %ecx, %eax
	setle %al
	movzbl %al, %eax
	cmpl $0, %eax
	jz .L0
	movl $1, %eax
	leave
	ret
	jmp .L1
.L0:
.L1:
	movl 8(%ebp), %eax
	pushl %eax
	movl 8(%ebp), %eax
	pushl %eax
	movl $1, %eax
	movl %eax, %ecx
	popl %eax
	subl %ecx, %eax
	pushl %eax
	call il_factorielle
	addl $4, %esp
	movl %eax, %ecx
	popl %eax
	imull %ecx, %eax
	leave
	ret
	movl $0, %eax
	leave
	ret

	.globl main
main:
	pushl %ebp
	movl %esp, %ebp
	andl $-16, %esp
	movl $5, %eax
	pushl %eax
	call il_factorielle
	addl $4, %esp
	pushl %eax
	pushl $.LCout
	call printf
	addl $8, %esp
	movl $0, %eax
	leave
	ret

	.section .rodata
.LCout:
	.string "%d\n"
.LCin:
	.string "%d"

	.bss
	.align 4
gv__readtmp:
	.zero 4
