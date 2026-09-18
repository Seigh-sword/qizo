#include "qizo.h"

#define KBD_QUEUE 256

static const char set1_noshift[128] = {
	0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
	'\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
	0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
	0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*',
	0, ' ', 0
};

static const char set1_shift[128] = {
	0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
	'\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
	0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
	0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*',
	0, ' ', 0
};

static const char set2_noshift[128] = {
	0, 0, '1', '2', '3', '4', '5', '7', '8', '9', '0', '-', '=', 0, '\t',
	'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', 0, '\n',
	0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
	0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*',
	0, ' ', 0
};

static const char set2_shift[128] = {
	0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 0, '\t',
	'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', 0, '\n',
	0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
	0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*',
	0, ' ', 0
};

static u8 queue[KBD_QUEUE];
static u32 head, tail;
static int shift_left, shift_right, ctrl_key, caps, set2, pending_release, ext;

static int kbd_wait_in(void)
{
	u32 spin = 2000000;

	while (spin--) {
		if (qizo_inb(0x64) & 1)
			return 1;
	}
	return 0;
}

static int kbd_wait_out(void)
{
	u32 spin = 2000000;

	while (spin--) {
		if (!(qizo_inb(0x64) & 2))
			return 1;
	}
	return 0;
}

static void kbd_cmd(u8 value)
{
	if (!kbd_wait_out())
		return;
	qizo_outb(0x60, value);
	kbd_wait_in();
	qizo_inb(0x60);
}

static void push_key(int c)
{
	u32 next = (head + 1) % KBD_QUEUE;

	if (next == tail)
		return;
	queue[head] = (u8)c;
	head = next;
}

void qizo_kbd_init(void)
{
	u8 cmd;

	head = tail = 0;
	shift_left = shift_right = ctrl_key = caps = ext = pending_release = 0;
	set2 = 1;
	if (!kbd_wait_out())
		return;
	qizo_outb(0x64, 0x60);
	if (!kbd_wait_in())
		return;
	cmd = qizo_inb(0x60);
	cmd |= 0x02;
	if (!kbd_wait_out())
		return;
	qizo_outb(0x64, 0x60);
	if (!kbd_wait_out())
		return;
	qizo_outb(0x60, cmd);
	kbd_cmd(0xF0);
	kbd_cmd(0);
	kbd_cmd(0xF4);
	head = tail = 0;
}

int qizo_kbd_ready(void)
{
	return head != tail;
}

int qizo_kbd_get(void)
{
	int c;

	if (head == tail)
		return -1;
	c = queue[tail];
	tail = (tail + 1) % KBD_QUEUE;
	return c;
}

static void push_scancode(u8 scan)
{
	int shift = shift_left || shift_right || caps;
	int up = pending_release;
	const char *map;

	pending_release = 0;
	if (scan == 0x12 || scan == 0x59 || scan == 0xF2 || scan == 0xF5) {
		if (scan == 0x12)
			shift_left = !up;
		if (scan == 0x59)
			shift_right = !up;
		return;
	}
	if (scan == 0x14) {
		ctrl_key = !up;
		return;
	}
	if (scan == 0x58) {
		if (!up)
			caps = !caps;
		return;
	}
	if (up)
		return;
	map = shift ? set2_shift : set2_noshift;
	if (scan >= 0x60)
		return;
	if (map[scan]) {
		if (ctrl_key) {
			char c = map[scan];

			if (c >= 'a' && c <= 'z')
				push_key(c - 'a' + 1);
			return;
		}
		push_key(map[scan]);
	}
}

static void handle_set2(u8 code)
{
	if (code == 0xE0 || code == 0xE1) {
		ext = 1;
		return;
	}
	if (code == 0xF0) {
		pending_release = 1;
		return;
	}
	if (code == 0xFA || code == 0xEE || code == 0xFC || code == 0xFE || code == 0xFF)
		return;
	if (code == 0xAA) {
		pending_release = 0;
		return;
	}
	if (ext) {
		ext = 0;
		if (!pending_release) {
			if (code == 0x5D)
				push_key('\n');
			else if (code == 0x75)
				push_key(0x01);
			else if (code == 0x72)
				push_key(0x02);
			else if (code == 0x69)
				push_key(0x06);
			else if (code == 0x70)
				push_key(0x05);
			else if (code == 0x71)
				push_key(0x02);
			else if (code == 0x7A)
				push_key(0x01);
		}
		pending_release = 0;
		return;
	}
	push_scancode(code);
}

static void handle_set1(u8 code)
{
	int up = code & 0x80;
	u8 scan = code & 0x7F;
	int shift = shift_left || shift_right || caps;
	const char *map;

	if (scan == 0x2A) {
		shift_left = !up;
		return;
	}
	if (scan == 0x36) {
		shift_right = !up;
		return;
	}
	if (scan == 0x1D) {
		ctrl_key = !up;
		return;
	}
	if (scan == 0x3A) {
		if (!up)
			caps = !caps;
		return;
	}
	if (up)
		return;
	map = shift ? set1_shift : set1_noshift;
	if (scan >= 128)
		return;
	if (map[scan]) {
		if (ctrl_key) {
			char c = map[scan];

			if (c >= 'a' && c <= 'z')
				push_key(c - 'a' + 1);
			return;
		}
		push_key(map[scan]);
	}
}

void qizo_kbd_interrupt(u8 code)
{
	if (set2)
		handle_set2(code);
	else
		handle_set1(code);
}
