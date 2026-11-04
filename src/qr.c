// qr.c - QR codes for /remote's pairing (2.22): nayuki's QR Code generator (MIT licence, its header in
// src/qr/qrcodegen.c), compiled here so it fits Loomy's build with no C runtime: asserts off, and the few
// C functions it uses defined below (memset / memcpy come from util.c).
#define NDEBUG
#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static size_t qr_strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
static void *qr_memmove(void *d, const void *s, size_t n) {
    unsigned char *a = (unsigned char *)d;
    const unsigned char *b = (const unsigned char *)s;
    if (a < b) for (size_t i = 0; i < n; i++) a[i] = b[i];
    else for (size_t i = n; i > 0; i--) a[i - 1] = b[i - 1];
    return d;
}
static char *qr_strchr(const char *s, int c) { for (; *s; s++) if (*s == (char)c) return (char *)s; return c ? 0 : (char *)s; }
static int qr_abs(int x) { return x < 0 ? -x : x; }
static long qr_labs(long x) { return x < 0 ? -x : x; }
#define strlen qr_strlen
#define memmove qr_memmove
#define strchr qr_strchr
#define abs qr_abs
#define labs qr_labs
#include "qr/qrcodegen.c"
