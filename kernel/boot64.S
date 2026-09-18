.section .qizo_text,"ax"
.code64

.set QIZO_PG_SIZE, 0x6000
.set QIZO_HHDM_VIRT, 0xFFFF888000000000
.set QIZO_CR4_BITS, 0x00000370
.set QIZO_EFER, 0xC0000080
.set QIZO_STACK_BYTES, 32768

.global qizo_kernel_entry
.type qizo_kernel_entry, @function
qizo_kernel_entry:
	cli
	cld
	movq %rdi, %rbx
	movq $qizo_boot_stack + QIZO_STACK_BYTES, %rsp
	andq $-16, %rsp
	xorq %rbp, %rbp
	call qizo_gdt_install
	call qizo_idt_install
	call qizo_paging_setup
	movq %rbx, %rdi
	call qizo_kmain
	jmp qizo_hlt_loop
.size qizo_kernel_entry, . - qizo_kernel_entry

.global qizo_paging_setup
.type qizo_paging_setup, @function
qizo_paging_setup:
	movq $qizo_pg_tables, %r9
	movq %r9, %rdi
	movq %r9, %r8
	addq $QIZO_PG_SIZE, %r8
	xorl %eax, %eax

1:
	movq %rax, (%rdi)
	addq $8, %rdi
	cmpq %r8, %rdi
	jb 1b

	movq %r9, %rax
	addq $0x1000, %rax
	orq $0x03, %rax
	movq %rax, (%r9)

	movq %r9, %rax
	addq $0x3000, %rax
	orq $0x03, %rax
	movq $QIZO_HHDM_VIRT, %rdx
	shrq $39, %rdx
	andl $511, %edx
	movq %rax, (%r9,%rdx,8)

	movq %r9, %rax
	addq $0x2000, %rax
	orq $0x03, %rax
	movq %rax, 0x1000(%r9)

	movq %r9, %rax
	addq $0x4000, %rax
	orq $0x03, %rax
	movq $QIZO_HHDM_VIRT, %rdx
	shrq $30, %rdx
	andl $511, %edx
	movq %rax, 0x3000(%r9,%rdx,8)

	movq %r9, %rdi
	addq $0x2000, %rdi
	xorl %edx, %edx

2:
	movq %rdx, %rcx
	shlq $21, %rcx
	orq $0x83, %rcx
	movq %rcx, (%rdi)
	addq $8, %rdi
	incq %rdx
	cmpq $512, %rdx
	jb 2b

	movq %r9, %rdi
	addq $0x4000, %rdi
	xorl %edx, %edx

3:
	movq %rdx, %rcx
	shlq $21, %rcx
	orq $0x83, %rcx
	movq %rcx, (%rdi)
	addq $8, %rdi
	incq %rdx
	cmpq $512, %rdx
	jb 3b

	movq %r9, %rax
	movq %rax, %cr3
	pushq %rbx
	movl $1, %eax
	xorl %ecx, %ecx
	cpuid
	movl $0x620, %r10d
	testl $0x08, %edx
	jz 4f
	orl $0x10, %r10d
4:
	testl $0x01, %edx
	jz 5f
	orl $0x40, %r10d
5:
	popq %rbx
	movq %r10, %rax
	movq %rax, %cr4
	movl $QIZO_EFER, %ecx
	rdmsr
	orl $0x901, %eax
	wrmsr
	movq %cr0, %rax
	andl $0x9FFFFFFF, %eax
	orl $0x80010001, %eax
	movq %rax, %cr0
	ret
.size qizo_paging_setup, . - qizo_paging_setup

.global qizo_hlt_loop
.type qizo_hlt_loop, @function
qizo_hlt_loop:
1:
	hlt
	jmp 1b
.size qizo_hlt_loop, . - qizo_hlt_loop

.global qizo_cpuid
.type qizo_cpuid, @function
qizo_cpuid:
	pushq %rbx
	movl 16(%rsp), %eax
	movl 20(%rsp), %ecx
	cpuid
	movl %eax, (%rdi)
	movl %ebx, 4(%rdi)
	movl %ecx, 8(%rdi)
	movl %edx, 12(%rdi)
	popq %rbx
	ret
.size qizo_cpuid, . - qizo_cpuid

.global qizo_rdmsr
.type qizo_rdmsr, @function
qizo_rdmsr:
	movl %edi, %ecx
	rdmsr
	shlq $32, %rdx
	orq %rdx, %rax
	ret
.size qizo_rdmsr, . - qizo_rdmsr

.global qizo_wrmsr
.type qizo_wrmsr, @function
qizo_wrmsr:
	movl %edi, %ecx
	movq %rsi, %rax
	movq %rsi, %rdx
	shrq $32, %rdx
	wrmsr
	ret
.size qizo_wrmsr, . - qizo_wrmsr

.global qizo_rdtsc
.type qizo_rdtsc, @function
qizo_rdtsc:
	lfence
	rdtsc
	shlq $32, %rdx
	orq %rdx, %rax
	ret
.size qizo_rdtsc, . - qizo_rdtsc

.section .qizo_bss,"aw",@nobits
.balign 4096
.global qizo_pg_tables
qizo_pg_tables:
	.skip QIZO_PG_SIZE
.balign 16
.global qizo_boot_stack
qizo_boot_stack:
	.skip QIZO_STACK_BYTES
