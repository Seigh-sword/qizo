.section .qizo_text,"ax"
.code64

.global qizo_memcpy_sse2
.type qizo_memcpy_sse2, @function
qizo_memcpy_sse2:
	cmpq $16, %rdx
	jb tail
	movq %rdx, %rcx
	andq $-16, %rcx
	addq %rcx, %rsi
	addq %rcx, %rdi
	leaq -16(%rsi), %rsi
	leaq -16(%rdi), %rdi

loop16:
	movdqu (%rsi), %xmm0
	movdqu %xmm0, (%rdi)
	movdqu -16(%rsi), %xmm1
	movdqu %xmm1, -16(%rdi)
	subq $32, %rsi
	subq $32, %rdi
	subq $32, %rcx
	jg loop16

	addq $32, %rsi
	addq $32, %rdi
	movq %rdx, %rcx
	andq $15, %rcx
	jz done

tail:
	addq %rcx, %rsi
	addq %rcx, %rdi

tail1:
	decq %rsi
	decq %rdi
	movzbl (%rsi), %eax
	movb %al, (%rdi)
	decq %rcx
	jnz tail1

done:
	ret
.size qizo_memcpy_sse2, . - qizo_memcpy_sse2

.global qizo_memset_sse2
.type qizo_memset_sse2, @function
qizo_memset_sse2:
	movl %esi, %eax
	movd %eax, %xmm0
	punpcklbw %xmm0, %xmm0
	punpcklwd %xmm0, %xmm0
	pshufd $0, %xmm0, %xmm0
	cmpq $16, %rdx
	jb tail

	movq %rdx, %rcx
	andq $-16, %rcx
	addq %rcx, %rdi
	leaq -16(%rdi), %rdi

mloop:
	movdqu %xmm0, (%rdi)
	movdqu %xmm0, -16(%rdi)
	subq $32, %rdi
	subq $32, %rcx
	jg mloop

	addq $32, %rdi
	movq %rdx, %rcx
	andq $15, %rcx
	jz mdone

mtail:
	addq %rcx, %rdi

mtail1:
	decq %rdi
	movb %al, (%rdi)
	decq %rcx
	jnz mtail1

mdone:
	ret
.size qizo_memset_sse2, . - qizo_memset_sse2

.global qizo_copy_byte
.type qizo_copy_byte, @function
qizo_copy_byte:
	movq %rdx, %rcx
	rep movsb
	ret
.size qizo_copy_byte, . - qizo_copy_byte

.global qizo_fill_byte
.type qizo_fill_byte, @function
qizo_fill_byte:
	movq %rdx, %rcx
	rep stosb
	ret
.size qizo_fill_byte, . - qizo_fill_byte

.global qizo_pause
.type qizo_pause, @function
qizo_pause:
	pause
	ret
.size qizo_pause, . - qizo_pause

.global qizo_read_cr0
.type qizo_read_cr0, @function
qizo_read_cr0:
	movq %cr0, %rax
	ret
.size qizo_read_cr0, . - qizo_read_cr0

.global qizo_read_cr4
.type qizo_read_cr4, @function
qizo_read_cr4:
	movq %cr4, %rax
	ret
.size qizo_read_cr4, . - qizo_read_cr4
