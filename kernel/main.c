#include "qizo.h"

static volatile u64 ticks;
static volatile u64 keyboard_irqs;
static volatile u64 spurious;
static struct qizo_bootinfo *boot;
static u64 ms_per_tick;
static u64 cycles_per_ms;

u64 qizo_ticks(void)
{
	return ticks;
}

struct qizo_bootinfo *qizo_boot(void)
{
	return boot;
}

static void pic_init(void)
{
	qizo_outb(0x20, 0x11);
	qizo_io_wait();
	qizo_outb(0xA0, 0x11);
	qizo_io_wait();
	qizo_outb(0x21, 0x20);
	qizo_io_wait();
	qizo_outb(0xA1, 0x28);
	qizo_io_wait();
	qizo_outb(0x21, 0x04);
	qizo_io_wait();
	qizo_outb(0xA1, 0x02);
	qizo_io_wait();
	qizo_outb(0x21, 0x01);
	qizo_io_wait();
	qizo_outb(0xA1, 0xFF);
	qizo_io_wait();
	qizo_outb(0x21, 0xFC);
	qizo_io_wait();
}

void qizo_irq_handler(struct qizo_regs *regs)
{
	u64 vector = regs->vector;

	if (vector == 32) {
		ticks++;
		return;
	}
	if (vector == 33) {
		u8 status = qizo_inb(0x64);

		keyboard_irqs++;
		if (status & 1)
			qizo_kbd_interrupt((u8)qizo_inb(0x60));
		return;
	}
	spurious++;
}

void qizo_irq_eoi(struct qizo_regs *regs)
{
	if (regs->vector >= 32 && regs->vector < 48) {
		if (regs->vector >= 40)
			qizo_outb(0xA0, 0x20);
		qizo_outb(0x20, 0x20);
	}
}

void qizo_timer_init(void)
{
	qizo_outb(0x43, 0x34);
	qizo_outb(0x40, 0x00);
	qizo_outb(0x40, 0x04);
	ms_per_tick = 55;
}

u64 qizo_ticks_to_ms(u64 t)
{
	return t * ms_per_tick;
}

void qizo_sleep(u64 ms)
{
	u64 target = ticks + (ms + ms_per_tick - 1) / ms_per_tick;

	while (ticks < target)
		__asm__ volatile("hlt");
}

void qizo_calibrate(void)
{
	u64 t0, c0, c1;
	u64 dt;

	if (!cycles_per_ms) {
		t0 = ticks;
		while (ticks == t0)
			;
		c0 = qizo_rdtsc();
		while (ticks == t0)
			;
		c1 = qizo_rdtsc();
		cycles_per_ms = (c1 - c0) / ms_per_tick;
		if (!cycles_per_ms)
			cycles_per_ms = 1;
	}
	t0 = ticks;
	c0 = qizo_rdtsc();
	qizo_sleep(100);
	c1 = qizo_rdtsc();
	dt = ticks - t0;
	if (dt >= 2) {
		u64 freq = (c1 - c0) / (dt * ms_per_tick);

		if (freq > cycles_per_ms)
			cycles_per_ms = freq;
	}
}

u64 qizo_cycles_per_ms(void)
{
	if (!cycles_per_ms)
		return 1000000;
	return cycles_per_ms;
}

u64 qizo_keyboard_irqs(void)
{
	return keyboard_irqs;
}

u64 qizo_spurious_irqs(void)
{
	return spurious;
}

void qizo_msr_probe(void)
{
	u64 efer = qizo_rdmsr(0xC0000080);

	if (!(efer & (1ULL << 0)))
		qizo_wrmsr(0xC0000080, efer | (1ULL << 0));
}

void qizo_kernel_main_entry(struct qizo_bootinfo *info)
{
	boot = info;
	qizo_paging_setup();
	qizo_console_init(info);
	qizo_cpu_init(info);
	qizo_pma_init(info);
	qizo_idt_install();
	pic_init();
	qizo_timer_init();
	qizo_kbd_init();
	qizo_sti();
	qizo_msr_probe();
	qizo_calibrate();
	qizo_sysinfo();
	qizo_shell();
	qizo_cli();
	qizo_puts("qizo: shell exit, halting\r\n");
	qizo_hlt_loop();
}

void qizo_reboot(void)
{
	u32 spin = 0x10000;

	qizo_cli();
	qizo_puts("qizo: rebooting\r\n");
	for (;;) {
		if (!(qizo_inb(0x64) & 2) && spin-- == 0) {
			qizo_outb(0x64, 0xFE);
			break;
		}
	}
	qizo_hlt_loop();
}

void qizo_kmain(struct qizo_bootinfo *info)
{
	qizo_kernel_main_entry(info);
}
