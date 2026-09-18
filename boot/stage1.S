.section .qizo_boot,"ax"
.code16

.include "qizoboot.inc"

.global _start
_start:
	cli
	cld
.if QIZO_BOOT_TRACE
	call qizo_com1_init
	movb $0x31, %al
	movw $0x3F8, %dx
	outb %al, %dx
	movb $0x3A, %al
	outb %al, %dx
.endif
	xorw %ax, %ax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %ss
	movw $0x7A00, %sp
	movb %dl, QIZO_ARG_ADDR + QIZO_ARG_DRIVE
	movb $0, QIZO_ARG_ADDR + QIZO_ARG_ERR
	movw $QIZO_DAP_ADDR, %si
	xorw %bx, %bx

read_chunk:
	movw QIZO_ARG_ADDR + QIZO_ARG_TOTAL, %ax
	subw %bx, %ax
	jbe go_stage2
	cmpw $64, %ax
	jbe chunk_set
	movw $64, %ax

chunk_set:
	movw %ax, QIZO_DAP_ADDR + 2
	movw $0, QIZO_DAP_ADDR + 4
	movw %bx, %ax
	shlw $5, %ax
	addw $QIZO_STAGE2_SEG, %ax
	movw %ax, QIZO_DAP_ADDR + 6
	movl QIZO_ARG_ADDR + QIZO_ARG_LBA, %eax
	addl %ebx, %eax
	movl %eax, QIZO_DAP_ADDR + 8
	movl $0, QIZO_DAP_ADDR + 12
	movb $0x42, %ah
	movb QIZO_ARG_ADDR + QIZO_ARG_DRIVE, %dl
	int $0x13
	jnc read_ok
	movb $0x01, QIZO_ARG_ADDR + QIZO_ARG_ERR
	jmp boot_fail

read_ok:
.if QIZO_BOOT_TRACE
	movb $0x52, %al
	movw $0x3F8, %dx
	outb %al, %dx
.endif
	movw QIZO_DAP_ADDR + 2, %ax
	addw %ax, %bx
	jmp read_chunk

go_stage2:
.if QIZO_BOOT_TRACE
	movb $0x4A, %al
	movw $0x3F8, %dx
	outb %al, %dx
.endif
	ljmp $QIZO_STAGE2_SEG, $0

boot_fail:
.if QIZO_BOOT_TRACE
	movb $0x45, %al
	movw $0x3F8, %dx
	outb %al, %dx
.endif
	movw $(qizo_fail_msg - _start), %si
	xorw %bx, %bx
	jmp print

print:
	lodsb
	testb %al, %al
	jz halt
	movb $0x0E, %ah
	xorw %bx, %bx
	movb %al, %dl
	int $0x10
	jmp print

halt:
	hlt
	jmp halt

qizo_fail_msg:
	.asciz "QIZO STAGE1 DISK ERR"

.if QIZO_BOOT_TRACE
qizo_com1_init:
	movw $0x3FB, %dx
	movb $0x83, %al
	outb %al, %dx
	movw $0x3F8, %dx
	movb $0x01, %al
	outb %al, %dx
	movw $0x3F9, %dx
	movb $0x00, %al
	outb %al, %dx
	movw $0x3FB, %dx
	movb $0x03, %al
	outb %al, %dx
	movw $0x3F2, %dx
	movb $0xC7, %al
	outb %al, %dx
	movw $0x3FD, %dx
	movb $0x0B, %al
	outb %al, %dx
	ret
.endif

.fill 464 - (. - _start), 1, 0

.section .qizo_data,"aw"
.balign 16
qizo_args:
	.byte 0
	.byte 0, 0, 0
	.long 0
	.word 0
	.word 0
	.long 0
	.byte 0
	.byte 0, 0, 0
qizo_dap:
	.byte 0x10
	.byte 0
	.word 0
	.word 0
	.word 0
	.long 0
	.long 0
