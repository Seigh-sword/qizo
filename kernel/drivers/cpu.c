#include "qizo.h"

static char brand_string[96];
static char vendor_string[16];
static u64 feat_bits;
static u32 cpu_family;
static u32 cpu_model;
static u32 cpu_stepping;

static void trim_ascii(char *s, size_t n)
{
	size_t i;
	size_t len = 0;

	for (i = 0; i < n; i++) {
		if (s[i] < 32 || s[i] > 126)
			s[i] = ' ';
	}
	for (i = 0; i < n; i++) {
		if (s[i])
			len = i + 1;
	}
	while (len && s[len - 1] == ' ')
		len--;
	s[len] = 0;
}

void qizo_cpu_init(struct qizo_bootinfo *info)
{
	u32 regs[4];
	u32 max_leaf;
	size_t i;
	u64 cr4;

	brand_string[0] = 0;
	vendor_string[0] = 0;
	qizo_cpuid(regs, 0, 0);
	max_leaf = regs[0];
	{
		char tag[13];

		qizo_memcpy(tag, regs + 1, 12);
		tag[12] = 0;
		for (i = 0; i < 12; i++)
			vendor_string[i] = tag[i];
		vendor_string[12] = 0;
	}
	if (max_leaf >= 7) {
		u32 ext_regs[4];

		qizo_cpuid(ext_regs, 7, 0);
	}
	if (info && info->cpuid_max >= 0x80000004u) {
		qizo_cpuid((u32 *)brand_string, 0x80000002, 0);
		qizo_cpuid((u32 *)brand_string + 4, 0x80000003, 0);
		qizo_cpuid((u32 *)brand_string + 8, 0x80000004, 0);
		trim_ascii(brand_string, sizeof(brand_string) - 1);
	}
	qizo_cpuid(regs, 1, 0);
	cpu_stepping = regs[0] & 0xF;
	cpu_model = (regs[0] >> 4) & 0xF;
	cpu_family = (regs[0] >> 8) & 0xF;
	if (cpu_family == 6 || cpu_family == 15)
		cpu_model += (regs[0] >> 16) & 0xF;
	if (cpu_family == 15)
		cpu_family += (regs[0] >> 20) & 0xFF;
	feat_bits = 0;
	if (regs[3] & (1u << 26))
		feat_bits |= QIZO_FEAT_SSE2;
	if (regs[2] & (1u << 0))
		feat_bits |= QIZO_FEAT_SSE3;
	if (regs[2] & (1u << 9))
		feat_bits |= QIZO_FEAT_SSSE3;
	if (regs[2] & (1u << 19))
		feat_bits |= QIZO_FEAT_SSE41;
	if (regs[2] & (1u << 20))
		feat_bits |= QIZO_FEAT_SSE42;
	if (regs[2] & (1u << 23))
		feat_bits |= QIZO_FEAT_POPCNT;
	if (regs[2] & (1u << 27))
		feat_bits |= QIZO_FEAT_AVX;
	qizo_cpuid(regs, 0x80000001, 0);
	if (regs[3] & (1u << 0))
		feat_bits |= QIZO_FEAT_LAHF;
	if (regs[3] & (1u << 29))
		feat_bits |= QIZO_FEAT_1GB;
	if (max_leaf >= 7) {
		qizo_cpuid(regs, 7, 0);
		if (regs[1] & (1u << 5))
			feat_bits |= QIZO_FEAT_AVX2;
		if (regs[1] & (1u << 8))
			feat_bits |= QIZO_FEAT_BMI2;
		if (regs[1] & (1u << 16))
			feat_bits |= QIZO_FEAT_AVX512F;
		if (regs[3] & (1u << 2))
			feat_bits |= QIZO_FEAT_FSGSBASE;
		if (regs[3] & (1u << 4))
			feat_bits |= QIZO_FEAT_ERMS;
	}
	cr4 = qizo_read_cr4();
	if (cr4 & (1ULL << 20))
		feat_bits |= QIZO_FEAT_SMEP;
	if (cr4 & (1ULL << 21))
		feat_bits |= QIZO_FEAT_SMAP;
}

const char *qizo_cpu_brand(void)
{
	if (brand_string[0])
		return brand_string;
	return "x86-64 compatible processor";
}

const char *qizo_cpu_vendor(void)
{
	if (vendor_string[0])
		return vendor_string;
	return "unknown";
}

u64 qizo_cpu_features(void)
{
	return feat_bits;
}

int qizo_cpu_has(u64 feature)
{
	return (feat_bits & feature) != 0;
}

u32 qizo_cpu_family(void)
{
	return cpu_family;
}

u32 qizo_cpu_model(void)
{
	return cpu_model;
}

u32 qizo_cpu_stepping(void)
{
	return cpu_stepping;
}

