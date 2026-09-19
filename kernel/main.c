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
	qizo_outb(0xA1, 0xEF);
	qizo_io_wait();
	qizo_outb(0x21, 0xFC);
	qizo_io_wait();
}

static inline u64 qizo_cr2(void)
{
	u64 v;

	__asm__ volatile("mov %%cr2, %0" : "=r"(v));
	return v;
}

static void qizo_exception(struct qizo_regs *regs)
{
	u64 vector = regs->vector;
	u64 fault = vector == 14 ? qizo_cr2() : 0;

	qizo_cli();
	qizo_trace("panic");
	qizo_printf("qizo: cpu exception %u rip=%p code=%p cr2=%p\r\n",
		    (u32)vector, regs->rip, regs->code, fault);
	qizo_printf("qizo: faulting frame at %p, vector list in docs\r\n", (u64)(regs + 1));
	qizo_halt();
}

void qizo_irq_handler(struct qizo_regs *regs)
{
	u64 vector = regs->vector;

	if (vector < 32) {
		qizo_exception(regs);
		return;
	}
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
	if (vector == 44) {
		u8 status = qizo_inb(0x64);

		if ((status & 0x21) == 0x21)
			qizo_mouse_interrupt(qizo_inb(0x60));
		else
			qizo_inb(0x60);
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
	u64 target, limit;
	volatile u64 spin;

	if (!timer_alive) {
		for (spin = ms * 4000; spin; --spin)
			;
		return;
	}
	target = ticks + (ms + ms_per_tick - 1) / ms_per_tick;
	limit = qizo_rdtsc() + (ms + 40) * qizo_cycles_per_ms();
	while (ticks < target && qizo_rdtsc() < limit)
		halt_once();
}

void qizo_calibrate(void)
{
	u64 c0, c1, seen, ticks0;
	u32 a, b, spin;
	u64 ms = 0;

	qizo_trace("calibrating");
	ticks0 = ticks;
	a = qizo_inb(0x40);
	c0 = qizo_rdtsc();
	for (spin = 0; spin < 400000 && ms < 8; ++spin) {
		b = qizo_inb(0x40);
		if (b > a) {
			a = b;
			++ms;
			c1 = qizo_rdtsc();
		}
	}
	if (ms) {
		cycles_per_ms = (c1 - c0) / ms;
		if (!cycles_per_ms)
			cycles_per_ms = 1;
	}
	seen = ticks;
	for (spin = 0; spin < 200000 && ticks == seen; ++spin)
		qizo_io_wait();
	if (ticks != seen) {
		timer_alive = 1;
		qizo_printf("timer : %u ticks/s, %u Mcycles/s, pit wraps %u\r\n",
			    (u32)(ticks - ticks0), (u32)cycles_per_ms, (u32)ms);
	} else {
		qizo_puts("timer : irq0 silent, pit polling only\r\n");
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
	qizo_drivers_start();
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
