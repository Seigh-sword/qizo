#include "qizo.h"

#define LINE_MAX 128
#define HIST_MAX 16
#define NCMD 24
#define KEY_UP 0x01
#define KEY_DOWN 0x02

extern void qizo_banner(void);
extern void qizo_cpuinfo(void);
extern void qizo_memstat(void);
extern void qizo_sysinfo(void);
extern void qizo_bench(void);
extern void qizo_reboot(void);
extern void qizo_clear(void);
extern void qizo_console_color(u32 attr);
extern u64 qizo_ticks_to_ms(u64 t);
extern u64 qizo_keyboard_irqs(void);
extern u64 qizo_spurious_irqs(void);
extern u64 qizo_cycles_per_ms(void);
extern u64 qizo_bench_memcpy_cycles(size_t n, int mode);
extern void qizo_sleep(u64 ms);
extern void qizo_cli(void);
extern void qizo_pause(void);

struct qizo_command {
	const char *name;
	const char *what;
	void (*run)(const char *arg);
};

static const struct qizo_command cmds[NCMD];

static char line[LINE_MAX];
static u32 len;
static u32 hist_at;
static char hist[HIST_MAX][LINE_MAX];
static u32 hist_count;
static int keys_mode;

static void cmd_help(const char *arg);

static void cmd_sysmngr(const char *arg)
{
	qizo_sysmngr(arg);
}

static void cmd_mouse(const char *arg)
{
	(void)arg;
	qizo_mouse_report();
}

static u32 hist_first(void)
{
	return hist_count > HIST_MAX ? hist_count - HIST_MAX : 0;
}

static void push_history(void)
{
	u32 i;

	if (!len) {
		len = 0;
		return;
	}
	for (i = hist_first(); i < hist_count; i++) {
		if (!qizo_strcmp(hist[i % HIST_MAX], line)) {
			len = 0;
			return;
		}
	}
	qizo_memcpy(hist[hist_count % HIST_MAX], line, len + 1);
	hist_count++;
	len = 0;
}

static void clear_line(void)
{
	while (len) {
		len--;
		line[len] = 0;
		qizo_puts("\b \b");
	}
}

static void set_line(const char *text)
{
	u32 i = 0;

	while (i < LINE_MAX - 1 && text[i]) {
		line[i] = text[i];
		qizo_putc(text[i]);
		i++;
	}
	line[i] = 0;
	len = i;
}

static void recall(int dir)
{
	u32 first = hist_first();

	if (hist_count == first)
		return;
	if (dir < 0) {
		if (hist_at == first)
			return;
		hist_at--;
	} else if (hist_at + 1 >= hist_count) {
		hist_at = hist_count;
		clear_line();
		return;
	} else {
		hist_at++;
	}
	clear_line();
	if (hist_at >= hist_count)
		return;
	set_line(hist[(hist_at - first) % HIST_MAX]);
}

static void erase_word(void)
{
	while (len && line[len - 1] == ' ') {
		len--;
		line[len] = 0;
		qizo_puts("\b \b");
	}
	while (len && line[len - 1] != ' ') {
		len--;
		line[len] = 0;
		qizo_puts("\b \b");
	}
}

static int name_prefix(const char *name, const char *text, u32 n)
{
	u32 i;

	for (i = 0; i < n; i++) {
		if (name[i] != text[i])
			return 0;
	}
	return name[i] != 0;
}

static void complete(void)
{
	char best[LINE_MAX];
	int matches = 0;
	u32 i, n;

	for (i = 0; i < NCMD; i++) {
		if (!name_prefix(cmds[i].name, line, len))
			continue;
		if (!matches) {
			qizo_memcpy(best, cmds[i].name, qizo_strlen(cmds[i].name) + 1);
		} else {
			n = 0;
			while (best[n] && best[n] == cmds[i].name[n])
				n++;
			best[n] = 0;
		}
		matches++;
	}
	if (!matches)
		return;
	n = qizo_strlen(best);
	while (len < n) {
		line[len] = best[len];
		qizo_putc(best[len]);
		len++;
	}
	if (matches == 1) {
		line[len] = ' ';
		qizo_putc(' ');
		len++;
	}
}

static void edit_line(int key)
{
	if (key == '\b') {
		if (len) {
			len--;
			line[len] = 0;
			qizo_puts("\b \b");
		}
		return;
	}
	if (key == 0x15) {
		clear_line();
		return;
	}
	if (key == 0x17) {
		erase_word();
		return;
	}
	if (key == ' ' || (key >= '!' && key <= '/') || (key >= ':' && key <= '@') ||
	    (key >= 'a' && key <= 'z') || (key >= 'A' && key <= 'Z') ||
	    (key >= '[' && key <= '`') || (key >= '{' && key <= '~')) {
		if (len < LINE_MAX - 1) {
			line[len++] = (char)key;
			line[len] = 0;
			qizo_putc((char)key);
		}
	}
}

static void prompt(void)
{
	qizo_console_color(0x0A);
	qizo_puts("qizo");
	qizo_console_color(0x07);
	qizo_puts("> ");
}

static void cmd_help(const char *arg)
{
	u32 i;

	if (arg && *arg) {
		for (i = 0; i < NCMD; i++) {
			if (!qizo_strcmp(cmds[i].name, arg)) {
				qizo_printf("  %-10s %s\r\n", cmds[i].name, cmds[i].what);
				return;
			}
		}
		qizo_puts("help: no such command, try help alone\r\n");
		return;
	}
	qizo_puts("  command     what it does\r\n");
	for (i = 0; i < NCMD; i++)
		qizo_printf("  %-10s %s\r\n", cmds[i].name, cmds[i].what);
	qizo_puts("  ctrl-u      erase the line, ctrl-w erase the last word\r\n");
	qizo_puts("  ctrl-l      clear the screen, ctrl-c cancel the line\r\n");
	qizo_puts("  up down     walk the history, tab completes a command\r\n");
}

static void cmd_info(const char *arg)
{
	(void)arg;
	qizo_sysinfo();
}

static void cmd_cpu(const char *arg)
{
	(void)arg;
	qizo_cpuinfo();
}

static void cmd_mem(const char *arg)
{
	(void)arg;
	qizo_memstat();
}

static void cmd_bench(const char *arg)
{
	(void)arg;
	qizo_bench();
}

static void cmd_clear(const char *arg)
{
	(void)arg;
	qizo_clear();
}

static void cmd_reboot(const char *arg)
{
	(void)arg;
	qizo_reboot();
}

static void cmd_echo(const char *arg)
{
	qizo_puts(arg);
	qizo_puts("\r\n");
}

static void cmd_ticks(const char *arg)
{
	u64 t = qizo_ticks();
	u64 ms = qizo_ticks_to_ms(t);

	(void)arg;
	qizo_printf("ticks   : %u, %u ms, kbd irq %u, spurious %u\r\n", (u32)t, (u32)ms,
		    (u32)qizo_keyboard_irqs(), (u32)qizo_spurious_irqs());
}

static void cmd_uptime(const char *arg)
{
	u64 ms = qizo_ticks_to_ms(qizo_ticks());

	(void)arg;
	qizo_printf("uptime  : %u:%02u:%02u.%03u at %u Mcycles per ms\r\n",
		    (u32)(ms / 3600000), (u32)(ms / 60000) % 60, (u32)(ms / 1000) % 60,
		    (u32)(ms % 1000), (u32)qizo_cycles_per_ms());
}

static u8 cmos_read(u8 reg)
{
	qizo_outb(0x70, reg & 0x7F);
	return qizo_inb(0x71);
}

static u8 bcd(u8 v)
{
	return (u8)((v >> 4) * 10 + (v & 0x0F));
}

static void cmd_rtc(const char *arg)
{
	u8 mode, sec, min, hour, day, mon, yr;
	u32 tries = 0;
	int pm;

	(void)arg;
	mode = cmos_read(0x0B);
	do {
		sec = cmos_read(0);
		min = cmos_read(2);
		hour = cmos_read(4);
		day = cmos_read(7);
		mon = cmos_read(8);
		yr = cmos_read(9);
		if (sec == cmos_read(0))
			break;
	} while (++tries < 100);
	pm = !(mode & 0x04) && (hour & 0x80);
	hour &= 0x7F;
	if (!(mode & 0x02)) {
		sec = bcd(sec);
		min = bcd(min);
		hour = bcd(hour);
		day = bcd(day);
		mon = bcd(mon);
		yr = bcd(yr);
	}
	qizo_printf("rtc     : 20%u-%02u-%02u %02u:%02u:%02u %s\r\n", yr, mon, day,
		    (mode & 0x04) ? hour : (pm ? hour + 12 : hour), min, sec,
		    (mode & 0x02) ? "binary" : "bcd");
}

static void cmd_color(const char *arg)
{
	u32 v = 0;

	while (*arg >= '0' && *arg <= '9')
		v = v * 10 + (u32)(*arg++ - '0');
	if (v > 15) {
		qizo_puts("color   : pick 0..15, 7 is light grey on black, 14 yellow\r\n");
		return;
	}
	qizo_console_color(v);
	qizo_puts("qizo: colour set, the prompt goes back to grey\r\n");
	qizo_console_color(0x07);
}

static void cmd_cpuid(const char *arg)
{
	u32 out[4];
	u32 leaf = 0;

	while (*arg >= '0' && *arg <= '9')
		leaf = leaf * 10 + (u32)(*arg++ - '0');
	qizo_cpuid(out, leaf, 0);
	qizo_printf("cpuid %u: eax %x ebx %x ecx %x edx %x\r\n", leaf, out[0], out[1], out[2],
		    out[3]);
	if (leaf >= 0x80000002 && leaf <= 0x80000004) {
		const char *p = (const char *)out;
		u32 i;

		for (i = 0; i < 16 && p[i]; i++)
			qizo_putc(p[i]);
		qizo_puts("\r\n");
	}
}

static void cmd_keys(const char *arg)
{
	(void)arg;
	keys_mode = !keys_mode;
	qizo_printf("keys    : %s, press any key to see its code\r\n",
		    keys_mode ? "on" : "off");
}

static void cmd_hist(const char *arg)
{
	u32 first = hist_first();
	u32 i;

	(void)arg;
	for (i = first; i < hist_count; i++)
		qizo_printf("  %2u  %s\r\n", i - first + 1, hist[i % HIST_MAX]);
	if (first == hist_count)
		qizo_puts("hist    : nothing remembered yet\r\n");
}

static void cmd_wait(const char *arg)
{
	u64 ms = 0;

	while (*arg >= '0' && *arg <= '9')
		ms = ms * 10 + (u64)(*arg++ - '0');
	if (!ms)
		ms = 1000;
	if (ms > 60000)
		ms = 60000;
	qizo_sleep(ms);
	qizo_printf("wait    : %u ms, ticks now %u\r\n", (u32)ms, (u32)qizo_ticks());
}

static void cmd_halt(const char *arg)
{
	(void)arg;
	qizo_puts("qizo: halting cpu 0, the timer stays armed\r\n");
	qizo_cli();
	qizo_hlt_loop();
}

static void cmd_fs(const char *arg)
{
	(void)arg;
	qizo_puts("fs      : there is no filesystem yet. the disk carries the loader, the\r\n");
	qizo_puts("          kernel blob and a FAT16 volume the build tools use to place the\r\n");
	qizo_puts("          image; the kernel reads that blob directly.\r\n");
}

static void microbench(const char *arg)
{
	u64 c0 = qizo_bench_memcpy_cycles(16, 0);
	u64 c1 = qizo_bench_memcpy_cycles(16, 1);
	u64 c2 = qizo_bench_memcpy_cycles(16, 2);
	u64 c3 = qizo_bench_memcpy_cycles(256, 0);
	u64 c4 = qizo_bench_memcpy_cycles(256, 1);

	(void)arg;
	qizo_printf("memcpy 16B   scalar %u  sse2 %u  rep %u\r\n", (u32)c0, (u32)c1, (u32)c2);
	qizo_printf("memcpy 256B  scalar %u  sse2 %u\r\n", (u32)c3, (u32)c4);
}

static void speed(const char *arg)
{
	u64 t0, t1, i, acc = 0;
	volatile char buf[64];

	(void)arg;
	qizo_puts("speed   : 100000 byte loops\r\n");
	t0 = qizo_rdtsc();
	for (i = 0; i < 100000; i++) {
		buf[i & 63] = (char)i;
		acc += (u8)buf[i & 63];
	}
	t1 = qizo_rdtsc();
	qizo_printf("  store loop %u cycles, %u per op, checksum %u\r\n", (u32)(t1 - t0),
		    (u32)((t1 - t0) / 100000), (u32)acc);
}

static void about(const char *arg)
{
	(void)arg;
	qizo_puts("qizo    : an operating system written from scratch\r\n");
	qizo_puts("          code and assets generated by Arena.ai Agent Mode\r\n");
	qizo_puts("          maintainers: Seigh-sword (arunkumar), suripewepedi (surya)\r\n");
	qizo_puts("          license: BSD-3-Clause\r\n");
}

static void banner_cmd(const char *arg)
{
	(void)arg;
	qizo_banner();
}

static const struct qizo_command cmds[NCMD] = {
	{ "help", "this list, or help <command>", cmd_help },
	{ "sysmngr", "memory, vram, volumes, drivers and versions", cmd_sysmngr },
	{ "mouse", "ps2 mouse state and counters", cmd_mouse },
	{ "info", "kernel and hardware summary", cmd_info },
	{ "cpu", "cpu identity, features, clock", cmd_cpu },
	{ "cpuid", "raw cpuid leaf, decimal number", cmd_cpuid },
	{ "mem", "frame allocator state", cmd_mem },
	{ "bench", "memory bandwidth benchmark", cmd_bench },
	{ "micro", "small copy microbenchmarks", microbench },
	{ "speed", "raw op timing", speed },
	{ "ticks", "tick and interrupt counters", cmd_ticks },
	{ "uptime", "time since boot", cmd_uptime },
	{ "rtc", "cmos clock", cmd_rtc },
	{ "keys", "toggle key code echo", cmd_keys },
	{ "hist", "command history", cmd_hist },
	{ "color", "text colour, 0..15", cmd_color },
	{ "banner", "draw the boot banner again", banner_cmd },
	{ "echo", "print the rest of the line", cmd_echo },
	{ "wait", "sleep for n milliseconds", cmd_wait },
	{ "clear", "wipe the screen", cmd_clear },
	{ "fs", "why there is no ls", cmd_fs },
	{ "about", "project and license", about },
	{ "reboot", "reset the machine", cmd_reboot },
	{ "halt", "stop this cpu", cmd_halt },
};

_Static_assert(sizeof(cmds) / sizeof(cmds[0]) == NCMD, "command table size");

static int word_eq(const char *a, const char *b)
{
	while (*a && *b) {
		if (*a != *b)
			return 0;
		a++;
		b++;
	}
	return *a == *b;
}

void qizo_shell(void)
{
	hist_at = hist_count;
	qizo_puts("qizo: type help, or tab to complete, up and down for history\r\n");
	prompt();
	for (;;) {
		int c = qizo_kbd_get();

		if (c < 0) {
			qizo_pause();
			continue;
		}
		if (keys_mode && c != '\n') {
			qizo_printf("key     : %x\r\n", (u32)c);
			if (c == KEY_UP || c == KEY_DOWN)
				continue;
		}
		if (c == KEY_UP) {
			recall(-1);
			continue;
		}
		if (c == KEY_DOWN) {
			recall(1);
			continue;
		}
		if (c == '\t') {
			complete();
			continue;
		}
		if (c == 0x0C) {
			qizo_clear();
			prompt();
			continue;
		}
		if (c == 0x03) {
			qizo_puts("^C\r\n");
			clear_line();
			prompt();
			continue;
		}
		if (c == '\n') {
			char *cmd;
			char *arg;
			u32 i;

			qizo_puts("\r\n");
			line[len] = 0;
			cmd = line;
			while (*cmd == ' ')
				cmd++;
			arg = cmd;
			while (*arg && *arg != ' ')
				arg++;
			if (*arg) {
				*arg = 0;
				arg++;
				while (*arg == ' ')
					arg++;
			}
			if (!*cmd) {
				prompt();
				continue;
			}
			push_history();
			hist_at = hist_count;
			if (word_eq(cmd, "?"))
				cmd = (char *)"help";
			for (i = 0; i < NCMD; i++) {
				if (word_eq(cmd, cmds[i].name)) {
					cmds[i].run(arg);
					break;
				}
			}
			if (i == NCMD) {
				qizo_puts("qizo: unknown command '");
				qizo_puts(cmd);
				qizo_puts("' (help, or tab)\r\n");
			}
			len = 0;
			prompt();
			continue;
		}
		edit_line(c);
	}
}
