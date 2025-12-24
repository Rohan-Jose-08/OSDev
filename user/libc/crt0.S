.section .text
.global _start
.extern main
.extern exit

_start:
	call main
	pushl %eax
	call exit
