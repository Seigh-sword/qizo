#pragma once

typedef unsigned char u8;
typedef signed char i8;
typedef unsigned short u16;
typedef signed short i16;
typedef unsigned int u32;
typedef signed int i32;
typedef unsigned long long u64;
typedef signed long long i64;
typedef __UINTPTR_TYPE__ size_t;
typedef __INTPTR_TYPE__ ssize_t;

typedef unsigned long ulong;
typedef unsigned long offsetof_t;
#define offsetof(t, m) __builtin_offsetof(t, m)
typedef unsigned long upage;

#define QIZO_KTEXT_VIRT 0x100000ULL
#define QIZO_HHDM_VIRT 0xFFFF888000000000ULL
#define QIZO_PAGE_SIZE 4096ULL
#define QIZO_PMAP_PAGES 512
#define QIZO_STACK_SIZE 32768
#define QIZO_BSS_SIZE 32768

#define QIZO_DEBUG 1

#define QIZO_VERSION_MAJOR 0
#define QIZO_VERSION_MINOR 1
#define QIZO_VERSION_PATCH 0


#include "qizo_bootinfo.h"

struct qizo_regs {
	u64 r15, r14, r13, r12, r11, r10, r9, r8;
	u64 rbp, rdi, rsi, rdx, rcx, rbx, rax;
	u64 vector, code;
	u64 rip, rflags;
} __attribute__((packed));

void qizo_paging_setup(void);
void qizo_hlt_loop(void);
void qizo_cpuid(u32 *out, u32 leaf, u32 sub);
u64 qizo_rdmsr(u32 msr);
void qizo_wrmsr(u32 msr, u64 value);
u64 qizo_rdtsc(void);
void qizo_idt_install(void);
void qizo_gdt_install(void);
void qizo_trace(const char *stage);

void qizo_kmain(struct qizo_bootinfo *info);
void qizo_console_init(struct qizo_bootinfo *info);
void qizo_console_write(const char *data, size_t len);
void qizo_puts(const char *s);
void qizo_putc(char c);
void qizo_printf(const char *fmt, ...);
void qizo_clear(void);

void *qizo_memcpy(void *dst, const void *src, size_t n);
void *qizo_memset(void *dst, int value, size_t n);
void qizo_memcpy_sse2(void *dst, const void *src, size_t n);
void qizo_memset_sse2(void *dst, int value, size_t n);
size_t qizo_strlen(const char *s);
int qizo_strcmp(const char *a, const char *b);
int qizo_strncmp(const char *a, const char *b, size_t n);
int qizo_strcasecmp(const char *a, const char *b);
char *qizo_strchr(const char *s, int c);
int qizo_isdigit(int c);
int qizo_tolower(int c);

void qizo_cpu_init(struct qizo_bootinfo *info);
const char *qizo_cpu_brand(void);
const char *qizo_cpu_vendor(void);
u64 qizo_cpu_features(void);
u32 qizo_cpu_family(void);
u32 qizo_cpu_model(void);
int qizo_cpu_has(u64 feature);

#define QIZO_FEAT_SSE2 (1ULL << 0)
#define QIZO_FEAT_SSE3 (1ULL << 1)
#define QIZO_FEAT_SSSE3 (1ULL << 2)
#define QIZO_FEAT_SSE41 (1ULL << 3)
#define QIZO_FEAT_SSE42 (1ULL << 4)
#define QIZO_FEAT_AVX (1ULL << 5)
#define QIZO_FEAT_AVX2 (1ULL << 6)
#define QIZO_FEAT_AVX512F (1ULL << 7)
#define QIZO_FEAT_BMI2 (1ULL << 8)
#define QIZO_FEAT_FSGSBASE (1ULL << 9)
#define QIZO_FEAT_SMEP (1ULL << 10)
#define QIZO_FEAT_SMAP (1ULL << 11)
#define QIZO_FEAT_ERMS (1ULL << 12)
#define QIZO_FEAT_1GB (1ULL << 13)
#define QIZO_FEAT_POPCNT (1ULL << 14)
#define QIZO_FEAT_LAHF (1ULL << 15)

void qizo_pma_init(struct qizo_bootinfo *info);
u64 qizo_pma_alloc(void);
void qizo_pma_free(u64 phys);
u64 qizo_pma_total(void);
u64 qizo_pma_free_count(void);
u64 qizo_pma_reserved(void);

void qizo_kbd_init(void);
int qizo_kbd_get(void);
int qizo_kbd_ready(void);

u64 qizo_ticks(void);
void qizo_sleep(u64 ms);

struct qizo_bootinfo *qizo_boot(void);
void qizo_set_boot(struct qizo_bootinfo *info);
void qizo_banner(void);
void qizo_cpuinfo(void);
void qizo_calibrate(void);
void qizo_timer_init(void);
void qizo_msr_probe(void);
void qizo_panic(const char *why);
void qizo_kbd_interrupt(u8 code);
u64 qizo_cycles_per_ms(void);
u64 qizo_ticks_to_ms(u64 t);
u64 qizo_keyboard_irqs(void);
u64 qizo_spurious_irqs(void);
u64 qizo_pma_max_phys(void);
u32 qizo_cpu_stepping(void);
void *qizo_page_alloc(size_t pages);
void qizo_page_free(void *addr, size_t pages);
void qizo_copy_byte(void *dst, const void *src, size_t n);
void qizo_fill_byte(void *dst, int value, size_t n);
u64 qizo_read_cr4(void);
u64 qizo_read_cr0(void);
void qizo_shell(void);
void qizo_bench(void);
void qizo_sysinfo(void);
void qizo_reboot(void);

static inline void qizo_outb(u16 port, u8 value)
{
	__asm__ volatile("outb %0, %1" :: "a"(value), "Nd"(port));
}

static inline u8 qizo_inb(u16 port)
{
	u8 value;
	__asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
	return value;
}

static inline void qizo_io_wait(void)
{
	qizo_outb(0x80, 0);
}

static inline void qizo_cli(void)
{
	__asm__ volatile("cli");
}

static inline void qizo_sti(void)
{
	__asm__ volatile("sti");
}

static inline void qizo_halt(void)
{
	for (;;)
		__asm__ volatile("hlt");
}
