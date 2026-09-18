#include "qizo.h"
#include "qizo_font.h"

#define VGA_BASE 0x000B8000ULL
#define VGA_FONT_BASE 0x000A0000ULL
#define TERM_COLS 80
#define TERM_ROWS 25
#define COM1 0x3F8

static u16 *vga;
static u32 cursor_col;
static u32 cursor_row;
static u32 base_attr;
static int serial_ok;
static int vga_ok;
static int plain_mode;

static void serial_init(void)
{
	u32 timeout = 100000;

	qizo_outb(COM1 + 1, 0x00);
	qizo_outb(COM1 + 3, 0x80);
	qizo_outb(COM1 + 0, 0x01);
	qizo_outb(COM1 + 1, 0x00);
	qizo_outb(COM1 + 3, 0x03);
	qizo_outb(COM1 + 2, 0xC7);
	qizo_outb(COM1 + 4, 0x0B);
	while (timeout--) {
		if ((qizo_inb(COM1 + 5) & 0x60) == 0x60) {
			serial_ok = 1;
			return;
		}
	}
	serial_ok = 0;
}

static void serial_putc(char c)
{
	u32 timeout = 200000;

	if (!serial_ok)
		return;
	while (timeout--) {
		if (qizo_inb(COM1 + 5) & 0x20)
			break;
	}
	qizo_outb(COM1, (u8)c);
	if (c == '\n')
		qizo_outb(COM1 + 0, 0x0D);
}

static void serial_puts(const char *s)
{
	while (*s)
		serial_putc(*s++);
}

static void serial_write(const char *data, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++) {
		if (data[i] == '\n' && i + 1 < len && data[i + 1] == '\r')
			continue;
		serial_putc(data[i]);
	}
}

static void term_scroll(void)
{
	u32 i;

	if (cursor_row < TERM_ROWS)
		return;
	for (i = 0; i < (TERM_ROWS - 1) * TERM_COLS; i++)
		vga[i] = vga[i + TERM_COLS];
	for (i = (TERM_ROWS - 1) * TERM_COLS; i < TERM_ROWS * TERM_COLS; i++)
		vga[i] = (u16)(' ' | (base_attr << 8));
	cursor_row = TERM_ROWS - 1;
}

static void term_putc(char c)
{
	if (c == '\r')
		return;
	if (c == '\n') {
		cursor_col = 0;
		cursor_row++;
		term_scroll();
		return;
	}
	if (c == '\b') {
		if (cursor_col) {
			cursor_col--;
			vga[cursor_row * TERM_COLS + cursor_col] = (u16)(' ' | (base_attr << 8));
		}
		return;
	}
	if (c < 32 || c > 126)
		c = '?';
	if (cursor_col >= TERM_COLS) {
		cursor_col = 0;
		cursor_row++;
		term_scroll();
	}
	vga[cursor_row * TERM_COLS + cursor_col] = (u16)((u8)c | (base_attr << 8));
	cursor_col++;
}

static void cursor_update(void)
{
	u16 pos = (u16)(cursor_row * TERM_COLS + cursor_col);

	if (!vga_ok || plain_mode)
		return;
	qizo_outb(0x3D4, 0x0F);
	qizo_outb(0x3D5, (u8)pos);
	qizo_outb(0x3D4, 0x0E);
	qizo_outb(0x3D5, (u8)(pos >> 8));
}

static void install_font(const unsigned char *rows)
{
	u32 i;
	u8 misc;

	if (!vga_ok || plain_mode)
		return;
	misc = qizo_inb(0x3CC);
	qizo_outb(0x3C4, 0x01);
	qizo_outb(0x3C5, misc & ~0x08);
	qizo_outb(0x3C4, 0x06);
	qizo_outb(0x3C5, 0x00);
	qizo_outb(0x3CE, 0x05);
	qizo_outb(0x3CF, 0x04);
	qizo_outb(0x3CE, 0x06);
	qizo_outb(0x3CF, 0x00);
	for (i = 0; i < 256 * 16; i++)
		((volatile u8 *)(u64)VGA_FONT_BASE)[8192 + i * 2] = rows[i];
	qizo_outb(0x3C4, 0x06);
	qizo_outb(0x3C5, 0x02);
	qizo_outb(0x3CE, 0x05);
	qizo_outb(0x3CF, 0x06);
	qizo_outb(0x3C4, 0x01);
	qizo_outb(0x3C5, misc | 0x08);
}

void qizo_console_init(struct qizo_bootinfo *info)
{
	u64 i;
	u16 *p;

	base_attr = 0x07;
	vga_ok = 0;
	plain_mode = 0;
	serial_init();
	if (info && info->mode == 1) {
		p = (u16 *)(u64)VGA_BASE;
		for (i = 0; i < 80; i++)
			p[i] = (u16)('A' | (0x07 << 8));
		for (i = 0; i < 80; i++) {
			if ((p[i] & 0xFF) == 'A')
				vga_ok = 1;
		}
		for (i = 0; i < (u64)TERM_COLS * TERM_ROWS; i++)
			p[i] = (u16)(' ' | (base_attr << 8));
		if (vga_ok)
			vga = p;
	}
	qizo_puts(vga_ok ? "\n" : "\r\n");
	if (vga_ok)
		cursor_update();
	install_font(qizo_font_data);
}

#if QIZO_DEBUG
void qizo_trace(const char *stage)
{
	serial_puts("qizo: ");
	serial_puts(stage);
	serial_puts("\r\n");
}
#else
void qizo_trace(const char *stage)
{
	(void)stage;
}
#endif

void qizo_console_write(const char *data, size_t len)
{
	size_t i;

	serial_write(data, len);
	if (!vga_ok)
		return;
	for (i = 0; i < len; i++) {
		term_putc(data[i]);
	}
	cursor_update();
}

void qizo_putc(char c)
{
	qizo_console_write(&c, 1);
}

void qizo_puts(const char *s)
{
	qizo_console_write(s, qizo_strlen(s));
}

void qizo_clear(void)
{
	u32 i;

	if (!vga_ok)
		return;
	for (i = 0; i < TERM_COLS * TERM_ROWS; i++)
		vga[i] = (u16)(' ' | (base_attr << 8));
	cursor_col = 0;
	cursor_row = 0;
	cursor_update();
}

static void put_uint(u64 value, u32 base, int signed_val)
{
	char buf[68];
	int i = 0;
	int neg = 0;
	const char *digits = "0123456789abcdef";

	if (signed_val && (i64)value < 0) {
		neg = 1;
		value = (u64)(-(i64)value);
	}
	if (!value)
		buf[i++] = '0';
	while (value) {
		buf[i++] = digits[value % base];
		value /= base;
	}
	if (neg)
		buf[i++] = '-';
	while (i)
		qizo_putc(buf[--i]);
}

void qizo_printf(const char *fmt, ...)
{
	__builtin_va_list args;
	const char *p;

	__builtin_va_start(args, fmt);
	for (p = fmt; *p; p++) {
		if (*p != '%') {
			qizo_putc(*p);
			continue;
		}
		p++;
		switch (*p) {
		case 'c':
			qizo_putc((char)__builtin_va_arg(args, int));
			break;
		case 'd':
		case 'i':
			put_uint((u64)(i64)__builtin_va_arg(args, int), 10, 1);
			break;
		case 'l':
			p++;
			if (*p == 'l') {
				put_uint(__builtin_va_arg(args, u64), 10, 0);
				break;
			}
			if (*p == 'x') {
				put_uint(__builtin_va_arg(args, u64), 16, 0);
				break;
			}
			qizo_putc('%');
			qizo_putc('l');
			qizo_putc(*p);
			break;
		case 'u':
			put_uint(__builtin_va_arg(args, u32), 10, 0);
			break;
		case 'x':
			put_uint(__builtin_va_arg(args, u32), 16, 0);
			break;
		case 'p':
			put_uint(__builtin_va_arg(args, u64), 16, 0);
			break;
		case 's': {
			const char *s = __builtin_va_arg(args, const char *);
			qizo_puts(s ? s : "(null)");
			break;
		}
		case '%':
			qizo_putc('%');
			break;
		default:
			qizo_putc('%');
			qizo_putc(*p);
			break;
		}
	}
	__builtin_va_end(args);
}

void qizo_panic(const char *why)
{
	qizo_puts("\nqizo: panic: ");
	qizo_puts(why);
	qizo_puts("\n");
	qizo_hlt_loop();
}

int qizo_console_vga(void)
{
	return vga_ok;
}

void qizo_console_color(u32 attr)
{
	base_attr = attr & 0x0F;
}
