#include "qizo.h"

#define M_QUEUE 64
#define M_SPIN 200000

static volatile u8 in_packet[3];
static volatile u32 in_len;
static volatile u64 m_irqs;
static u32 m_dropped;
static struct qizo_mouse last;
static struct qizo_mouse queue[M_QUEUE];
static u32 q_head;
static u32 q_used;
static int reporting;
static u8 mouse_id;

static int aux_ready_write(void)
{
	u32 i;

	for (i = 0; i < M_SPIN; i++) {
		if (!(qizo_inb(0x64) & 0x02))
			return 1;
	}
	return 0;
}

static int aux_ready_read(void)
{
	u32 i;

	for (i = 0; i < M_SPIN; i++) {
		if (qizo_inb(0x64) & 0x01)
			return 1;
	}
	return 0;
}

static u8 aux_read_byte(void)
{
	if (!aux_ready_read())
		return 0;
	return qizo_inb(0x60);
}

static void aux_send(u8 value)
{
	if (!aux_ready_write())
		return;
	qizo_outb(0x64, 0xD4);
	if (!aux_ready_write())
		return;
	qizo_outb(0x60, value);
	aux_ready_write();
}

static u8 aux_config(void)
{
	if (!aux_ready_write())
		return 0;
	qizo_outb(0x64, 0x20);
	return aux_read_byte();
}

static void aux_set_config(u8 value)
{
	if (!aux_ready_write())
		return;
	qizo_outb(0x64, 0x60);
	qizo_outb(0x60, value);
	aux_ready_write();
}

int qizo_mouse_probe(void)
{
	u8 cfg;

	if (!aux_ready_write())
		return 0;
	qizo_outb(0x64, 0xA8);
	if (!aux_ready_write())
		return 0;
	cfg = aux_config();
	if (!(cfg & 0x20))
		aux_set_config(cfg | 0x20);
	aux_send(0xF2);
	if (!aux_ready_read())
		return 0;
	mouse_id = aux_read_byte();
	if (mouse_id == 0xFA || mouse_id == 0x00)
		mouse_id = 0x00;
	return 1;
}

void qizo_mouse_start(void)
{
	u8 cfg = aux_config();

	if (!(cfg & 0x22))
		aux_set_config(cfg | 0x22);
	aux_send(0xF3);
	aux_send(100);
	aux_send(0xE8);
	aux_send(2);
	aux_send(0xF4);
	reporting = 1;
	last.x = 0;
	last.y = 0;
	last.dx = 0;
	last.dy = 0;
	last.buttons = 0;
	last.packets = 0;
	last.errors = 0;
	last.ready = 1;
	q_head = 0;
	q_used = 0;
	in_len = 0;
}

void qizo_mouse_interrupt(u8 b)
{
	struct qizo_mouse move;
	i32 dx;
	i32 dy;

	m_irqs++;
	if (!(b & 0x08) || in_len == 3) {
		in_packet[0] = b;
		in_len = 1;
		last.errors++;
		return;
	}
	in_packet[in_len++] = b;
	if (in_len != 3)
		return;
	in_len = 0;
	if (in_packet[0] & 0xC0) {
		last.errors++;
		return;
	}
	dx = (i32)in_packet[1];
	dy = (i32)in_packet[2];
	if (in_packet[0] & 0x10)
		dx -= 256;
	if (in_packet[0] & 0x20)
		dy -= 256;
	move.buttons = in_packet[0] & 0x07;
	move.dx = dx;
	move.dy = -dy;
	move.x = last.x + dx;
	move.y = last.y - dy;
	move.packets = last.packets + 1;
	move.errors = last.errors;
	move.ready = 1;
	last = move;
	if (q_used < M_QUEUE) {
		queue[(q_head + q_used) % M_QUEUE] = move;
		q_used++;
	} else {
		qizo_memcpy(&queue[q_head], &move, sizeof(move));
		q_head = (q_head + 1) % M_QUEUE;
		m_dropped++;
	}
}

int qizo_mouse_read(struct qizo_mouse *out)
{
	if (!q_used)
		return 0;
	qizo_memcpy(out, &queue[q_head], sizeof(*out));
	q_head = (q_head + 1) % M_QUEUE;
	q_used--;
	return 1;
}

void qizo_mouse_flush(void)
{
	q_head = 0;
	q_used = 0;
	last.x = 0;
	last.y = 0;
}

void qizo_mouse_now(struct qizo_mouse *out)
{
	*out = last;
}

int qizo_mouse_reporting(void)
{
	return reporting;
}

u64 qizo_mouse_irqs(void)
{
	return m_irqs;
}

u32 qizo_mouse_dropped(void)
{
	return m_dropped;
}

const char *qizo_mouse_detail(void)
{
	if (!reporting)
		return "i8042 aux, idle";
	return "ps2 aux, irq12";
}

static u8 aux_last(void)
{
	return in_len ? in_packet[in_len - 1] : 0;
}

void qizo_mouse_report(void)
{
	struct qizo_mouse now;

	qizo_drivers_start();
	qizo_mouse_now(&now);
	qizo_printf("mouse: %s, %lu packets, %lu irqs, %u half packets dropped\r\n",
		    reporting ? "reporting" : "idle", now.packets, qizo_mouse_irqs(),
		    qizo_mouse_dropped());
	qizo_printf("  position %d %d, buttons %x, partial byte %x\r\n", now.x, now.y,
		    now.buttons, aux_last());
	if (aux_ready_read()) {
		u32 i;

		for (i = 0; i < 8 && (qizo_inb(0x64) & 0x01); i++)
			qizo_inb(0x60);
	}
}
