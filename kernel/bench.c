#include "qizo.h"

#define BENCH_BLOCK (16 * 1024)
#define BENCH_ROUNDS 64


static u64 mhz(void)
{
	return qizo_cycles_per_ms() / 1000;
}

static u64 best_of(u64 a, u64 b)
{
	return a < b ? a : b;
}

static const char *speed_label(u64 mib)
{
	if (mib >= 20000)
		return "monster";
	if (mib >= 6000)
		return "brutal";
	if (mib >= 2000)
		return "fast";
	if (mib >= 600)
		return "ok";
	return "slow chip";
}

void qizo_bench(void)
{
	char *a, *b;
	u64 t0, t1;
	u64 bytes, mib;
	u64 r;
	u64 scalar = 0, sse = 0, erms = 0, fill = 0;

	a = (char *)qizo_page_alloc(BENCH_BLOCK / QIZO_PAGE_SIZE);
	b = (char *)qizo_page_alloc(BENCH_BLOCK / QIZO_PAGE_SIZE);
	if (!a || !b) {
		qizo_puts("bench: out of physical frames\r\n");
		if (a)
			qizo_page_free(a, BENCH_BLOCK / QIZO_PAGE_SIZE);
		if (b)
			qizo_page_free(b, BENCH_BLOCK / QIZO_PAGE_SIZE);
		return;
	}
	qizo_fill_byte(b, 0, BENCH_BLOCK);
	qizo_puts("bench: measuring memory ops on a low end chip\r\n");
	qizo_printf("bench: cpu %u MHz  block %u KiB  rounds %u\r\n", (u32)mhz(),
		    BENCH_BLOCK / 1024, BENCH_ROUNDS);
	qizo_sleep(120);

	for (r = 0; r < 3; r++) {
		t0 = qizo_rdtsc();
		for (u64 i = 0; i < BENCH_ROUNDS; i++)
			qizo_memcpy(a, b, BENCH_BLOCK);
		t1 = qizo_rdtsc();
		scalar = r ? best_of(scalar, t1 - t0) : t1 - t0;
	}
	qizo_printf("bench: scalar copy      %u cycles\r\n", (u32)scalar);

	for (r = 0; r < 3; r++) {
		t0 = qizo_rdtsc();
		for (u64 i = 0; i < BENCH_ROUNDS; i++)
			qizo_memcpy_sse2(a, b, BENCH_BLOCK);
		t1 = qizo_rdtsc();
		sse = r ? best_of(sse, t1 - t0) : t1 - t0;
	}
	qizo_printf("bench: sse2 copy        %u cycles\r\n", (u32)sse);

	for (r = 0; r < 3; r++) {
		t0 = qizo_rdtsc();
		for (u64 i = 0; i < BENCH_ROUNDS; i++)
			qizo_copy_byte(a, b, BENCH_BLOCK);
		t1 = qizo_rdtsc();
		erms = r ? best_of(erms, t1 - t0) : t1 - t0;
	}
	qizo_printf("bench: rep movsb copy   %u cycles\r\n", (u32)erms);

	for (r = 0; r < 3; r++) {
		t0 = qizo_rdtsc();
		for (u64 i = 0; i < BENCH_ROUNDS; i++)
			qizo_memset_sse2(a, 0x5A, BENCH_BLOCK);
		t1 = qizo_rdtsc();
		fill = r ? best_of(fill, t1 - t0) : t1 - t0;
	}
	qizo_printf("bench: sse2 fill        %u cycles\r\n", (u32)fill);

	bytes = (u64)BENCH_BLOCK * BENCH_ROUNDS;
	mib = bytes * 1000 / 1024 / 1024 / best_of(sse, erms) * mhz();
	qizo_printf("bench: best copy %u.%u MiB/s  (%s)\r\n", (u32)(mib / 1000),
		    (u32)(mib % 1000 / 100), speed_label(mib / 1000));
	if (qizo_cpu_has(QIZO_FEAT_ERMS))
		qizo_puts("bench: erms path selected (rep movsb tuned)\r\n");
	else
		qizo_puts("bench: no erms, sse2 unrolled path selected\r\n");
	qizo_page_free(a, BENCH_BLOCK / QIZO_PAGE_SIZE);
	qizo_page_free(b, BENCH_BLOCK / QIZO_PAGE_SIZE);
}

u64 qizo_bench_memcpy_cycles(size_t n, int mode)
{
	char *a, *b;
	u64 t0, t1;
	u64 rounds = 1024 * 1024 / (n ? n : 1);
	u64 i;

	if (!rounds)
		rounds = 1;
	a = (char *)qizo_page_alloc(1);
	b = (char *)qizo_page_alloc(1);
	if (!a || !b)
		return 0;
	qizo_fill_byte(b, 0x33, QIZO_PAGE_SIZE);
	t0 = qizo_rdtsc();
	for (i = 0; i < rounds; i++) {
		if (mode == 0)
			qizo_memcpy(a, b, n);
		else if (mode == 1)
			qizo_memcpy_sse2(a, b, n);
		else
			qizo_copy_byte(a, b, n);
	}
	t1 = qizo_rdtsc();
	if (a)
		qizo_page_free(a, 1);
	if (b)
		qizo_page_free(b, 1);
	return (t1 - t0) / rounds;
}
