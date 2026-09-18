.section .qizo_boot,"ax"
.code16

.include "qizoboot.inc"

.global _start
_start:
	cli
	cld
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
	shlw $11, %ax
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
	movw QIZO_DAP_ADDR + 2, %ax
	addw %ax, %bx
	jmp read_chunk

go_stage2:
	ljmp $QIZO_STAGE2_SEG, $0

boot_fail:
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
