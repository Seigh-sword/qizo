#include "qizo.h"

static volatile u64 ticks;
static volatile u64 keyboard_irqs;
static volatile u64 spurious;
static struct qizo_bootinfo *boot;
static u64 ms_per_tick = 1;
static u64 cycles_per_ms;
static int timer_alive;

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
	qizo_outb(0x43, 0x36);
	qizo_io_wait();
	qizo_outb(0x40, 0x49);
	qizo_io_wait();
	qizo_outb(0x40, 0x04);
	qizo_io_wait();
	ms_per_tick = 1;
}

u64 qizo_ticks_to_ms(u64 t)
{
	return t * ms_per_tick;
}

static inline void halt_once(void)
{
	__asm__ volatile("hlt");
}

void qizo_sleep(u64 ms)
{
	u64 target;
	volatile u64 spin;

	if (!timer_alive) {
		for (spin = ms * 4000; spin; --spin)
			;
		return;
	}
	target = ticks + (ms + ms_per_tick - 1) / ms_per_tick;
	while (ticks < target)
		halt_once();
}

void qizo_calibrate(void)
{
	u64 budget = 8000000;
	u64 start, seen, c0, c1;

	qizo_trace("calibrating");
	start = ticks;
	seen = start;
	c0 = qizo_rdtsc();
	while (ticks == seen && budget--)
		halt_once();
	c1 = qizo_rdtsc();
	if (ticks != seen) {
		timer_alive = 1;
		cycles_per_ms = (c1 - c0) / ((ticks - start) * ms_per_tick);
		if (!cycles_per_ms)
			cycles_per_ms = 1;
		qizo_printf("timer : %u ticks/s, %u Mcycles/s\r\n",
			    (u32)(1000 / ms_per_tick), (u32)cycles_per_ms);
		return;
	}
	qizo_puts("timer : no irq0 ticks, using tsc fallback\r\n");
	cycles_per_ms = 2000000;
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
	qizo_printf("qizo %u.%u.%u at %lx\r\n", QIZO_VERSION_MAJOR,
		    QIZO_VERSION_MINOR, QIZO_VERSION_PATCH, (u64)&qizo_kernel_main_entry);
	qizo_trace("console up");
	qizo_cpu_init(info);
	qizo_pma_init(info);
	qizo_trace("memory up");
	qizo_idt_install();
	pic_init();
	qizo_timer_init();
	qizo_kbd_init();
	qizo_sti();
	qizo_msr_probe();
	qizo_trace("irqs live");
	qizo_calibrate();
	qizo_trace("reporting");
	qizo_sysinfo();
	qizo_trace("shell");
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
