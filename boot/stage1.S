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
	movb %dl, QIZO_ARG_ADDR + QIZO_ARG_BOOTDRIVE
	movb $0, QIZO_ARG_ADDR + QIZO_ARG_ERR
	movb	$0x10, QIZO_DAP_ADDR
	movb	$0, QIZO_DAP_ADDR + 1
	cmpb $0x80, %dl
	jae drive_ok
	cmpb $0xE0, %dl
	jae drive_ok
	movb $0x80, %dl
	movb %dl, QIZO_ARG_ADDR + QIZO_ARG_DRIVE
drive_ok:
	xorw %di, %di
	xorw %cx, %cx
	movw $qizo_cand, %si
drive_try:
	movw $0x55AA, %bx
	movb $0x41, %ah
	int $0x13
	jnc drive_got
	incw %cx
	cmpw $4, %cx
	jae drive_got
	lodsb
	movb %al, QIZO_ARG_ADDR + QIZO_ARG_DRIVE
	movb %al, %dl
	jmp drive_try
drive_got:
	cmpb $4, %cl
	jb drive_sel
	movb QIZO_ARG_ADDR + QIZO_ARG_BOOTDRIVE, %dl
	movb %dl, QIZO_ARG_ADDR + QIZO_ARG_DRIVE
drive_sel:
	movb QIZO_ARG_ADDR + QIZO_ARG_DRIVE, %dl
	movw $0x55AA, %bx
	movb $0x41, %ah
	int $0x13
.if QIZO_BOOT_TRACE
	pushw %ax
	pushw %cx
	movb $0x58, %al
	call qizo_tty
	movb %ah, %al
	call qizo_tty_byte
	movb %cl, %al
	call qizo_tty_byte
	movb QIZO_ARG_ADDR + QIZO_ARG_DRIVE, %al
	call qizo_tty_byte
	popw %cx
	popw %ax
.endif
	movw $QIZO_DAP_ADDR, %bx

read_chunk:
	movw QIZO_ARG_ADDR + QIZO_ARG_TOTAL, %ax
	subw %di, %ax
	jbe go_stage2
	cmpw $64, %ax
	jbe chunk_set
	movw $64, %ax

chunk_set:
	movw	%ax, QIZO_DAP_ADDR + 2
	movl	%edi, %eax
	shll	$9, %eax
	movl	$QIZO_STAGE2_SEG, %edx
	shll	$4, %edx
	addl	%edx, %eax
	movl	%eax, %ecx
	shrl	$4, %eax
	movw	%ax, QIZO_DAP_ADDR + 6
	andl	$0x000F, %ecx
	movw	%cx, QIZO_DAP_ADDR + 4
	movl	QIZO_ARG_ADDR + QIZO_ARG_LBA, %eax
	addl	%edi, %eax
	movl	%eax, QIZO_DAP_ADDR + 8
	movl	$0, QIZO_DAP_ADDR + 12
	movb	$0x42, %ah
	movb	QIZO_ARG_ADDR + QIZO_ARG_DRIVE, %dl
	int	$0x13
	jnc read_ok
	movb %ah, QIZO_ARG_ADDR + QIZO_ARG_ERR
	jmp boot_fail

read_ok:
.if QIZO_BOOT_TRACE
	movb $0x52, %al
	movw $0x3F8, %dx
	outb %al, %dx
.endif
	movw QIZO_DAP_ADDR + 2, %ax
	addw %ax, %di
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
	call qizo_tty
	movb QIZO_ARG_ADDR + QIZO_ARG_ERR, %al
	call qizo_tty_byte
	movb QIZO_ARG_ADDR + QIZO_ARG_DRIVE, %al
	call qizo_tty_byte
.endif
	movw $qizo_fail_msg, %si
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

qizo_tty:
	movw $0x3F8, %dx
	outb %al, %dx
	ret

qizo_tty_byte:
	movb %al, %ah
	shrb $4, %al
	call qizo_tty_nibble
	movb %ah, %al
	andb $0x0F, %al
	call qizo_tty_nibble
	ret

qizo_tty_nibble:
	cmpb $9, %al
	jbe tty_dec
	addb $0x37, %al
	jmp tty_out
tty_dec:
	addb $0x30, %al
tty_out:
	call qizo_tty
	ret

qizo_fail_msg:
	.asciz "QIZO STAGE1 DISK ERR"

qizo_cand:
	.byte 0x80, 0xE8, 0xF0, 0xF8

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
	.long 0
	.long 0
