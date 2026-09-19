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
	movw $0x8FF0, %sp
.if QIZO_BOOT_TRACE
	call qizo_com1_init
	movb $0x32, %al
	call qizo_trace_putc
	movb $0x3A, %al
	call qizo_trace_putc
.endif

	movl $QIZO_BOOTINFO_SEG, %eax
	movw %ax, %es
	movl $0x00010001, %es:QIZO_BI_MAGIC
	movb QIZO_BIOS_ARG + QIZO_ARG_DRIVE, %al
	movb %al, %es:QIZO_BI_DRIVE
	movb $1, %es:QIZO_BI_MODE
	movl QIZO_BIOS_ARG + QIZO_ARG_LBA, %eax
	movl %eax, %es:QIZO_BI_BLOB_LBA
	movzwl QIZO_BIOS_ARG + QIZO_ARG_TOTAL, %eax
	movw %ax, %es:QIZO_BI_BLOB_SECTORS
	shll $9, %eax
	subl $(QIZO_BLOB_PHYS - QIZO_STAGE2), %eax
	movl %eax, %es:QIZO_BI_BLOB_SIZE
	movl $QIZO_BLOB_PHYS, %eax
	movl %eax, %es:QIZO_BI_BLOB_PHYS
	movl QIZO_BIOS_ARG + QIZO_ARG_CRC, %eax
	movl %eax, %es:QIZO_BI_BLOB_CRC
	movl $QIZO_SCRATCH, %eax
	movl %eax, %es:QIZO_BI_SCRATCH
	movl $QIZO_KTEXT, %eax
	movl %eax, %es:QIZO_BI_KTEXT
	movl $QIZO_BOOTVER, %eax
	movl %eax, %es:QIZO_BI_FLAGS
	movb $0, %es:QIZO_BI_ERR

	movb %ds:0x1FF0, %cl
	movb %es:0x1FF0, %ch
	movb $0x55, %al
	movb %al, %es:0x1FF0
	notb %al
	movb %al, %ds:0x1FF0
	cmpb %al, %es:0x1FF0
	jne a20_on
	inb $0x92, %al
	orb $2, %al
	outb %al, $0x92
	inb $0xE8, %al
	inb $0x92, %al
	movb $0xAA, %al
	movb %al, %ds:0x1FF0
	cmpb %al, %es:0x1FF0
	je a20_stuck
a20_on:
	movb %ch, %es:0x1FF0
	movb %cl, %ds:0x1FF0
	movb $1, %es:QIZO_BI_A20
.if QIZO_BOOT_TRACE
	movb $0x41, %al
	call qizo_trace_putc
	movb $0x31, %al
	call qizo_trace_putc
.endif
	jmp a20_done

a20_stuck:
	movb %ch, %es:0x1FF0
	movb %cl, %ds:0x1FF0
	movb $0, %es:QIZO_BI_A20
.if QIZO_BOOT_TRACE
	movb $0x41, %al
	call qizo_trace_putc
	movb $0x30, %al
	call qizo_trace_putc
.endif
a20_done:
	pushfl
	popw %ax
	xorw $0x200, %ax
	pushw %ax
	popfl
	pushfl
	popw %ax
	testw $0x200, %ax
	je fail_noflags
	cli
	xorl %eax, %eax
	cpuid
	movl %eax, %es:QIZO_BI_CPUID_MAX
	movl $0x80000001, %eax
	cpuid
	testl $0x20000000, %edx
	je fail_nocpu
	movb $1, %es:QIZO_BI_LONGMODE
	jmp e820_start

fail_nocpu:
	movb $0x12, %es:QIZO_BI_ERR
	jmp qizo_fail16

fail_noflags:
	movb $0x13, %es:QIZO_BI_ERR
	jmp qizo_fail16

e820_start:
	movw $QIZO_E820_SEG, %ax
	movw %ax, %ds
	movw %ax, %es
	xorw %di, %di
	xorl %ebp, %ebp
	xorl %ebx, %ebx
	movw $QIZO_E820_CNT_OFS, %si
	movl $0, (%si)

e820_next:
	cmpl $QIZO_E820_MAX, %ebp
	jae e820_done
	movl $QIZO_E820_ENT, %ecx
	movl $0xE820, %eax
	movl $0x534D4150, %edx
	int $0x15
	jc e820_check
	cmpl $0x534D4150, %eax
	jne e820_check
	cmpl $20, %ecx
	jb e820_check
	addw $QIZO_E820_ENT, %di
	incw %bp
	incl (%si)
	testl %ebx, %ebx
	jne e820_next

e820_check:
	testl %ebp, %ebp
	jne e820_done
	movw $QIZO_BOOTINFO_SEG, %ax
	movw %ax, %es
	movb $0x14, %es:QIZO_BI_ERR
	jmp qizo_fail16

e820_done:
	movw $QIZO_BOOTINFO_SEG, %ax
	movw %ax, %es
	movw $QIZO_E820_CNT_OFS, %si
	movl (%si), %eax
	movl %eax, %es:QIZO_BI_E820_N
	movw $QIZO_STAGE2_SEG, %ax
	movw %ax, %ds
	movw $(qizo_gdtr - _start), %bx
	lgdt (%bx)
.if QIZO_BOOT_TRACE
	movb $0x61, %al
	call qizo_trace_putc
.endif
	movl $0x630, %eax
	movl %eax, %cr4
	movl %cr0, %eax
	andl $0x9FFFFFFF, %eax
	orl $1, %eax
	movl %eax, %cr0
.if QIZO_BOOT_TRACE
	pushw %ax
	movb $0x50, %al
	call qizo_trace_putc
	popw %ax
.endif
	ljmpl $0x18, $(qizo_pm16 - QIZO_STAGE2)

qizo_pm16:
	movb $0x6D, %al
	call qizo_trace_putc
	movw $0x10, %ax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %ss
	movl $QIZO_PM_STACK, %esp
	ljmpl $0x08, $qizo_pm32

print16:
	lodsb
	testb %al, %al
	jz halt16
	movb $0x0E, %ah
	xorw %bx, %bx
	movb %al, %dl
	int $0x10
	jmp print16

halt16:
	hlt
	jmp halt16

.global qizo_fail16
qizo_fail16:
.if QIZO_BOOT_TRACE
	pushw %ax
	pushw %ds
	movw $QIZO_BOOTINFO_SEG, %ax
	movw %ax, %ds
	movb QIZO_BI_ERR, %al
	call qizo_trace_hex8
	popw %ds
	popw %ax
.endif
	movw %cs, %ax
	movw %ax, %ds
	movw $(qizo_msg - _start), %si
	xorw %bx, %bx
	jmp print16

qizo_gdt:
	.byte 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	.byte 0xff, 0xff, 0x00, 0x00, 0x00, 0x9a, 0xcf, 0x00
	.byte 0xff, 0xff, 0x00, 0x00, 0x00, 0x92, 0xcf, 0x00
	.byte 0xff, 0xff, 0x00, 0x00, 0x02, 0x9a, 0x0f, 0x00
	.byte 0xff, 0xff, 0x00, 0x00, 0x00, 0x92, 0xcf, 0x00
	.byte 0xff, 0xff, 0x00, 0x00, 0x00, 0x9a, 0xaf, 0x00
	.byte 0xff, 0xff, 0x00, 0x00, 0x00, 0x92, 0xcf, 0x00

qizo_gdtr:
	.word 55
	.long QIZO_STAGE2 + (qizo_gdt - _start)


.if QIZO_BOOT_TRACE
qizo_com1_init:
	pushw %ax
	pushw %dx
	movb $0x00, %al
	movw $0x3F9, %dx
	outb %al, %dx
	movb $0x80, %al
	movw $0x3FB, %dx
	outb %al, %dx
	movb $0x01, %al
	movw $0x3F8, %dx
	outb %al, %dx
	movb $0x00, %al
	movw $0x3F9, %dx
	outb %al, %dx
	movb $0x03, %al
	movw $0x3FB, %dx
	outb %al, %dx
	movb $0xC7, %al
	movw $0x3F2, %dx
	outb %al, %dx
	movb $0x0B, %al
	movw $0x3FD, %dx
	outb %al, %dx
	popw %dx
	popw %ax
	ret

qizo_trace_putc:
	pushw %ax
	pushw %cx
	pushw %dx
	movb %al, %ah
	movw $60000, %cx
	movw $0x3FD, %dx
qizo_tr_wait:
	inb %dx, %al
	andb $0x20, %al
	jnz qizo_tr_send
	decw %cx
	jnz qizo_tr_wait
	jmp qizo_tr_done
qizo_tr_send:
	movb %ah, %al
	movw $0x3F8, %dx
	outb %al, %dx
qizo_tr_done:
	popw %dx
	popw %cx
	popw %ax
	ret

qizo_trace_hexnib:
	andb $0x0F, %al
	cmpb $10, %al
	jb qizo_tr_dec
	addb $0x37, %al
	jmp qizo_tr_emit
qizo_tr_dec:
	addb $0x30, %al
qizo_tr_emit:
	call qizo_trace_putc
	ret

qizo_trace_hex8:
	pushw %bx
	movb %al, %bl
	movb $0x45, %al
	call qizo_trace_putc
	movb %bl, %al
	shrb $4, %al
	call qizo_trace_hexnib
	movb %bl, %al
	call qizo_trace_hexnib
	popw %bx
	ret
.endif

qizo_msg:
	.asciz "QIZO BOOT ERR "

.section .qizo_pm32,"ax"
.code32

.global qizo_pm32
qizo_pm32:
.if QIZO_BOOT_TRACE
	movb $0x6D, %al
	movl $4000, %ecx
	movl $0x3FD, %edx
30:	inb %dx, %al
	andb $0x20, %al
	jnz 31f
	decl %ecx
	jnz 30b
31:	movb $0x6D, %al
	movl $0x3F8, %edx
	outb %al, %dx
.endif
	movw $0x10, %ax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %ss
	movl $QIZO_PM_STACK, %esp
.if QIZO_BOOT_TRACE
	movb $0x4D, %al
	movl $4000, %ecx
	movl $0x3FD, %edx
1:	inb %dx, %al
	andb $0x20, %al
	jnz 2f
	decl %ecx
	jnz 1b
2:	movb $0x4D, %al
	movl $0x3F8, %edx
	outb %al, %dx
.endif

	movl QIZO_BOOTINFO + QIZO_BI_E820_N, %ecx
	cmpl $QIZO_E820_MAX, %ecx
	jbe e820_clamp
	movl $QIZO_E820_MAX, %ecx

e820_clamp:
	movl %ecx, QIZO_BOOTINFO + QIZO_BI_E820_N
	shll $3, %ecx
	leal (%ecx,%ecx,2), %ecx
	movl $QIZO_E820_BUF, %esi
	movl $(QIZO_BOOTINFO + QIZO_BI_E820), %edi
	rep movsb

	movl $QIZO_BOOTINFO + QIZO_BI_E820, %esi
	xorl %edx, %edx
	movl QIZO_BOOTINFO + QIZO_BI_E820_N, %ecx

ram_next:
	testl %ecx, %ecx
	je ram_done
	cmpl $1, 16(%esi)
	jne ram_skip
	movl 8(%esi), %eax
	orl 12(%esi), %eax
	jne ram_sat
	addl %eax, %edx
	jnc ram_skip

ram_sat:
	movl $0xFFFFFFFF, %edx

ram_skip:
	addl $QIZO_E820_ENT, %esi
	decl %ecx
	jne ram_next

ram_done:
	movl %edx, QIZO_BOOTINFO + QIZO_BI_TOTAL_RAM

	movl QIZO_BOOTINFO + QIZO_BI_BLOB_PHYS, %esi
	movl QIZO_BOOTINFO + QIZO_BI_BLOB_SIZE, %ecx
	call qizo_adler
	movl QIZO_BOOTINFO + QIZO_BI_BLOB_PHYS, %esi
	movl QIZO_BOOTINFO + QIZO_BI_BLOB_SIZE, %ecx
	cmpl QIZO_BOOTINFO + QIZO_BI_BLOB_CRC, %eax
	jne fail_c1

	movl QIZO_BOOTINFO + QIZO_BI_BLOB_PHYS, %esi
	movl QIZO_BOOTINFO + QIZO_BI_BLOB_SIZE, %ecx
	movl QIZO_BOOTINFO + QIZO_BI_SCRATCH, %edi
	call qizo_decompress
	testl %eax, %eax
	je fail_c2

	movl QIZO_BOOTINFO + QIZO_BI_SCRATCH, %edi
	cmpl $QIZO_QIZOK_MAGIC, 0(%edi)
	jne fail_c3
	movl 4(%edi), %ecx
	cmpl $QIZO_KERNEL_MAX, %ecx
	ja fail_c4
	movl 12(%edi), %eax
	cmpl $QIZO_KERNEL_MAX, %eax
	ja fail_c5
	cmpl %ecx, %eax
	jb fail_c6
	movl 16(%edi), %eax
	cmpl $QIZO_KTEXT, %eax
	jne fail_c7
	movl 8(%edi), %edx
	cmpl %ecx, %edx
	jae fail32
	movl %ecx, QIZO_BOOTINFO + QIZO_BI_KERNEL_SIZE
	movl %edx, QIZO_BOOTINFO + QIZO_BI_KERNEL_ENTRY
	movl 12(%edi), %eax
	movl %eax, QIZO_BOOTINFO + QIZO_BI_KERNEL_TOTAL
	subl %ecx, %eax
	movl %eax, QIZO_BOOTINFO + QIZO_BI_KERNEL_BSS

	movl QIZO_BOOTINFO + QIZO_BI_SCRATCH, %esi
	addl $(QIZO_QIZOK_HDR + 8), %esi
	movl QIZO_BOOTINFO + QIZO_BI_KTEXT, %edi
	movl QIZO_BOOTINFO + QIZO_BI_KERNEL_SIZE, %ecx
	call qizo_copy

	movl QIZO_BOOTINFO + QIZO_BI_KTEXT, %edi
	addl QIZO_BOOTINFO + QIZO_BI_KERNEL_SIZE, %edi
	movl QIZO_BOOTINFO + QIZO_BI_KERNEL_BSS, %ecx
	call qizo_zero

	call qizo_build_pages
	movl $0x630, %eax
	movl %eax, %cr4
	movl $QIZO_PAGE_PML4, %eax
	movl %eax, %cr3
	movl $0xC0000080, %ecx
	rdmsr
	orl $0x901, %eax
	wrmsr
	movl %cr0, %eax
	orl $0x80000001, %eax
	movl %eax, %cr0
.if QIZO_BOOT_TRACE
	movb $0x4C, %al
	movl $4000, %ecx
	movl $0x3FD, %edx
3:	inb %dx, %al
	andb $0x20, %al
	jnz 4f
	decl %ecx
	jnz 3b
4:	movb $0x4C, %al
	movl $0x3F8, %edx
	outb %al, %dx
.endif
	ljmpl $0x28, $qizo_long64

qizo_pm_putc:
	movzbl %al, %eax
	pushl %eax
	movl $4000, %ecx
	movl $0x3FD, %edx
20:	inb %dx, %al
	andb $0x20, %al
	jnz 21f
	decl %ecx
	jnz 20b
21:	popl %eax
	movl $0x3F8, %edx
	outb %al, %dx
	ret

qizo_pm_nib:
	andl $0x0F, %eax
	cmpl $9, %eax
	jbe 22f
	addl $0x37, %eax
	jmp 23f
22:	addl $0x30, %eax
23:	call qizo_pm_putc
	ret

qizo_fail_pm:
	movb $0x45, %al
	call qizo_pm_putc
	movzbl QIZO_BOOTINFO + QIZO_BI_ERR, %eax
	movl %eax, %ebp
	shrl $4, %eax
	call qizo_pm_nib
	movl %ebp, %eax
	call qizo_pm_nib
24:	cli
	hlt
	jmp 24b

fail32:
	movb $0x16, QIZO_BOOTINFO + QIZO_BI_ERR

fail32_r:
	movl %cr0, %eax
	andl $0x7FFFFFFF, %eax
	movl %eax, %cr0
	movl %cr0, %eax
	andl $0x7FFFFFFF, %eax
	movl %eax, %cr0
	jmp qizo_fail_pm

.section .qizo_long64,"ax"
.code64

.global qizo_long64
qizo_long64:
	movw $0x10, %ax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %ss
	movabsq $QIZO_PM_STACK, %rsp
.if QIZO_BOOT_TRACE
	movb $0x36, %al
	movl $4000, %ecx
	movl $0x3FD, %edx
5:	inb %dx, %al
	andb $0x20, %al
	jnz 6f
	decl %ecx
	jnz 5b
6:	movb $0x36, %al
	movl $0x3F8, %edx
	outb %al, %dx
.endif
	movabsq $QIZO_BOOTINFO, %rdi
	movl QIZO_BOOTINFO + QIZO_BI_KTEXT, %eax
	addl QIZO_BOOTINFO + QIZO_BI_KERNEL_ENTRY, %eax
	jmp *%rax

.section .qizo_aux,"ax"
.code32

qizo_adler:
	movl $1, %eax
	xorl %edi, %edi
	testl %ecx, %ecx
	je adler_done

adler_loop:
	movzbl (%esi), %edx
	addl %edx, %eax
	addl %eax, %edi
	cmpl $65521, %eax
	jb adler_a
	subl $65521, %eax

adler_a:
	cmpl $65521, %edi
	jb adler_b
	subl $65521, %edi

adler_b:
	incl %esi
	decl %ecx
	jne adler_loop
	shll $16, %edi
	orl %edi, %eax

adler_done:
	ret

qizo_decompress:
	pushl %ebx
	pushl %ebp
	pushl %esi
	pushl %edi
	movl %ecx, %ebp
	movl (%esi), %eax
	movl 4(%esi), %edx
	addl $8, %esi
	subl $8, %ebp
	cmpl $QIZO_LZSS_MAGIC, %eax
	jne decomp_raw
	addl %edi, %edx
	movl $0x100, %ebx
	xorl %ecx, %ecx
	jmp decomp_top

decomp_raw:
	movl %esi, %edx
	addl %ebp, %edx
	movl %ebp, %ecx

decomp_top:
	cmpl %edx, %edi
	jae decomp_ok
	testl %ecx, %ecx
	jne decomp_copy_byte
	shrl $1, %ebx
	testl $0x100, %ebx
	jnz decomp_item
	movzbl (%esi), %eax
	incl %esi
	movl %eax, %ebx
	orl $0x100, %ebx

decomp_item:
	testl $1, %ebx
	jnz decomp_match
	movb (%esi), %al
	incl %esi
	movb %al, (%edi)
	incl %edi
	jmp decomp_top

decomp_match:
	movzwl (%esi), %eax
	addl $2, %esi
	movl %eax, %ecx
	shrl $4, %ecx
	incl %ecx
	andl $0xF, %eax
	addl $3, %eax
	movl %edi, %ebp
	subl %ecx, %ebp
	pushl %eax

decomp_copy:
	popl %ecx
	testl %ecx, %ecx
	je decomp_top

decomp_copy_loop:
	movb (%ebp), %al
	movb %al, (%edi)
	incl %ebp
	incl %edi
	decl %ecx
	jne decomp_copy_loop
	jmp decomp_top
	movl QIZO_BOOTINFO + QIZO_BI_BLOB_PHYS, %esi
	addl $8, %esi
	jmp decomp_top

decomp_copy_byte:
	movb (%esi), %al
	incl %esi
	movb %al, (%edi)
	incl %edi
	decl %ecx
	jmp decomp_top

decomp_ok:
	movl $1, %eax
	jmp decomp_ret

decomp_fail:
	movb $0x15, QIZO_BOOTINFO + QIZO_BI_ERR
	xorl %eax, %eax

decomp_ret:
	popl %edi
	popl %esi
	popl %ebp
	popl %ebx
	ret

qizo_copy:
	rep movsb
	ret

qizo_zero:
	xorl %eax, %eax
	rep stosb
	ret

qizo_build_pages:
	movl $QIZO_PAGE_PML4, %edi
	movl $0x6000, %ecx
	call qizo_zero
	movl $QIZO_PAGE_PML4, %edi
	movl $4, %ecx
	movl $QIZO_PAGE_PDPT, %eax

pml4_fill:
	movl %eax, %edx
	orl $QIZO_PT_PAGE, %edx
	movl %edx, (%edi)
	movl $0, 4(%edi)
	addl $0x1000, %eax
	addl $8, %edi
	decl %ecx
	jne pml4_fill

	movl $QIZO_PAGE_PDPT, %edi
	xorl %eax, %eax
	movl $2048, %ecx

pdpt_fill:
	movl %eax, %edx
	orl $QIZO_PT_PAGE, %edx
	movl %edx, (%edi)
	movl $0, 4(%edi)
	addl $0x200000, %eax
	addl $8, %edi
	decl %ecx
	jne pdpt_fill
	ret

fail_c1:
	movb $0x17, QIZO_BOOTINFO + QIZO_BI_ERR
	jmp fail32_r

fail_c2:
	movb $0x18, QIZO_BOOTINFO + QIZO_BI_ERR
	jmp fail32_r

fail_c3:
	movb $0x19, QIZO_BOOTINFO + QIZO_BI_ERR
	jmp fail32_r

fail_c4:
	movb $0x1a, QIZO_BOOTINFO + QIZO_BI_ERR
	jmp fail32_r

fail_c5:
	movb $0x1b, QIZO_BOOTINFO + QIZO_BI_ERR
	jmp fail32_r

fail_c6:
	movb $0x1c, QIZO_BOOTINFO + QIZO_BI_ERR
	jmp fail32_r

fail_c7:
	movb $0x1d, QIZO_BOOTINFO + QIZO_BI_ERR
	jmp fail32_r


