#include "qizo.h"

void *qizo_memcpy(void *dst, const void *src, size_t n)
{
	u8 *d = dst;
	const u8 *s = src;
	size_t i;

	for (i = 0; i < n; i++)
		d[i] = s[i];
	return dst;
}

void *qizo_memset(void *dst, int value, size_t n)
{
	u8 *d = dst;
	size_t i;

	for (i = 0; i < n; i++)
		d[i] = (u8)value;
	return dst;
}

size_t qizo_strlen(const char *s)
{
	size_t n = 0;

	while (s[n])
		n++;
	return n;
}

int qizo_strcmp(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return (u8)*a - (u8)*b;
}

int qizo_strncmp(const char *a, const char *b, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++) {
		if (a[i] != b[i])
			return (u8)a[i] - (u8)b[i];
		if (!a[i])
			return 0;
	}
	return 0;
}

int qizo_strcasecmp(const char *a, const char *b)
{
	for (;;) {
		int ca = qizo_tolower((u8)*a);
		int cb = qizo_tolower((u8)*b);

		if (ca != cb)
			return ca - cb;
		if (!ca)
			return 0;
		a++;
		b++;
	}
}

char *qizo_strchr(const char *s, int c)
{
	for (; *s; s++) {
		if (*s == (char)c)
			return (char *)s;
	}
	return c ? 0 : (char *)s;
}

int qizo_isdigit(int c)
{
	return c >= '0' && c <= '9';
}

int qizo_tolower(int c)
{
	if (c >= 'A' && c <= 'Z')
		return c + 32;
	return c;
}

void *memcpy(void *dst, const void *src, size_t n)
{
	return qizo_memcpy(dst, src, n);
}

void *memset(void *dst, int value, size_t n)
{
	return qizo_memset(dst, value, n);
}

void *memmove(void *dst, const void *src, size_t n)
{
	u8 *d = dst;
	const u8 *s = src;
	size_t i;

	if (d == s || !n)
		return dst;
	if (d < s) {
		for (i = 0; i < n; i++)
			d[i] = s[i];
	} else {
		for (i = n; i; i--)
			d[i - 1] = s[i - 1];
	}
	return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
	const u8 *x = a;
	const u8 *y = b;
	size_t i;

	for (i = 0; i < n; i++) {
		if (x[i] != y[i])
			return x[i] - y[i];
	}
	return 0;
}

void *__memcpy_chk(void *dst, const void *src, size_t n, size_t size)
{
	if (n > size)
		n = size;
	return qizo_memcpy(dst, src, n);
}

void *__memset_chk(void *dst, int value, size_t n, size_t size)
{
	if (n > size)
		n = size;
	return qizo_memset(dst, value, n);
}
