.section .qizo_text,"ax"
.code64

.macro QIZO_STUB num
.if \num != 8 && \num != 10 && \num != 11 && \num != 12 && \num != 13 && \num != 14 && \num != 17 && \num != 21 && \num != 29 && \num != 30
pushq $0
.endif
	pushq $\num
	jmp qizo_isr_common
.endm

.macro QIZO_IRQ num, vec
	pushq $0
	pushq $\vec
	jmp qizo_isr_common
.endm

.global qizo_stub_table
.type qizo_stub_table, @object
qizo_stub_table:
	.irp n, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
	QIZO_STUB \n
	.endr
	.irp n, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
	QIZO_IRQ \n, (32+\n)
	.endr
	.irp n, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127,128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223
	QIZO_IRQ (224+\n), (64+\n)
	.endr
.size qizo_stub_table, . - qizo_stub_table

.global qizo_stub_end
qizo_stub_end:

qizo_isr_common:
	fxsave qizo_fp_area(%rip)
	pushq %rax
	pushq %rbx
	pushq %rcx
	pushq %rdx
	pushq %rsi
	pushq %rdi
	pushq %rbp
	pushq %r8
	pushq %r9
	pushq %r10
	pushq %r11
	pushq %r12
	pushq %r13
	pushq %r14
	pushq %r15
	cld
	movq %rsp, %rdi
	call qizo_irq_handler
	movq %rsp, %rdi
	call qizo_irq_eoi
	fxrstor qizo_fp_area(%rip)
	popq %r15
	popq %r14
	popq %r13
	popq %r12
	popq %r11
	popq %r10
	popq %r9
	popq %r8
	popq %rbp
	popq %rdi
	popq %rsi
	popq %rdx
	popq %rcx
	popq %rbx
	popq %rax
	addq $16, %rsp
	iretq

.section .qizo_data,"aw"
.balign 16
.global qizo_idt
.type qizo_idt, @object
qizo_idt:
	.skip 256 * 16
.global qizo_idt_ptr
qizo_idt_ptr:
	.word 256 * 16 - 1
	.quad qizo_idt

.balign 16
qizo_gdt:
	.quad 0
	.quad 0x0020980000000000
	.quad 0x0000920000000000
	.quad 0x0020FA0000000000
	.quad 0x0000F20000000000
qizo_gdt_end:

qizo_gdt_ptr:
	.word qizo_gdt_end - qizo_gdt - 1
	.quad qizo_gdt

.balign 16
qizo_fp_area:
	.skip 512

.section .qizo_text,"ax"
.code64

.global qizo_gdt_install
.type qizo_gdt_install, @function
qizo_gdt_install:
	leaq qizo_gdt_ptr(%rip), %rax
	lgdt (%rax)
	movw $0x10, %ax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %ss
	movw %ax, %fs
	movw %ax, %gs
	leaq qizo_gdt_reload(%rip), %rax
	pushq $0x08
	pushq %rax
	lretq
qizo_gdt_reload:
	ret
.size qizo_gdt_install, . - qizo_gdt_install

.global qizo_idt_install
.type qizo_idt_install, @function
qizo_idt_install:
	leaq qizo_stub_table(%rip), %rsi
	leaq qizo_idt(%rip), %rdi
	movq %rsi, %rax
	movl $256, %ecx

1:
	movw %ax, (%rdi)
	movw $0x0008, 2(%rdi)
	movw $0x8E00, 4(%rdi)
	shrq $16, %rax
	movw %ax, 6(%rdi)
	shrq $16, %rax
	movw %ax, 8(%rdi)
	shrl $32, %eax
	movl %eax, 10(%rdi)
	movw $0, 14(%rdi)
	addq $16, %rdi
	addq $16, %rsi
	movq %rsi, %rax
	decl %ecx
	jnz 1b
	leaq qizo_idt_ptr(%rip), %rax
	lidt (%rax)
	ret
.size qizo_idt_install, . - qizo_idt_install
