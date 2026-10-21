// util.c - tiny helpers so we don't need the C runtime at all
#include "loomy.h"

// The compiler sometimes calls these on its own (struct = {0}, struct copies),
// so with no C runtime we have to provide them ourselves.
void *memset(void *d, int c, size_t n) {
    unsigned char *p = d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}
void *memcpy(void *d, const void *s, size_t n) {
    unsigned char *a = d; const unsigned char *b = s;
    while (n--) *a++ = *b++;
    return d;
}

int s_len(const wchar_t *s) { int n = 0; while (s[n]) n++; return n; }

void s_cpy(wchar_t *d, const wchar_t *s, int cap) {
    int i = 0;
    while (s[i] && i < cap - 1) { d[i] = s[i]; i++; }
    d[i] = 0;
}

void s_cat(wchar_t *d, const wchar_t *s, int cap) {
    int n = s_len(d);
    if (n < cap) s_cpy(d + n, s, cap - n);
}

wchar_t s_low(wchar_t c) { return (c >= L'A' && c <= L'Z') ? c + 32 : c; }

void s_lower(wchar_t *s) { for (; *s; s++) *s = s_low(*s); }

int s_ieq(const wchar_t *a, const wchar_t *b) {
    while (*a && s_low(*a) == s_low(*b)) { a++; b++; }
    return s_low(*a) == s_low(*b);
}

int s_istarts(const wchar_t *s, const wchar_t *p) {
    while (*p) { if (s_low(*s) != s_low(*p)) return 0; s++; p++; }
    return 1;
}

int s_has(const wchar_t *s, const wchar_t *needle) {
    for (; *s; s++) if (s_istarts(s, needle)) return 1;
    return 0;
}

static int is_space(wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n'; }

wchar_t *s_trim(wchar_t *s) {
    while (is_space(*s)) s++;
    int n = s_len(s);
    while (n > 0 && is_space(s[n - 1])) s[--n] = 0;
    return s;
}

// "go steam stuff" -> s becomes "go", returns "steam stuff"
wchar_t *s_split(wchar_t *s) {
    while (*s && !is_space(*s)) s++;
    if (!*s) return s;
    *s++ = 0;
    while (is_space(*s)) s++;
    return s;
}

// "#A24DF0" or "A24DF0" -> 0xA24DF0
int s_hex(const wchar_t *s, unsigned *out) {
    if (*s == L'#') s++;
    unsigned v = 0; int n = 0;
    for (; *s; s++, n++) {
        wchar_t c = s_low(*s);
        if (c >= L'0' && c <= L'9') v = v * 16 + (c - L'0');
        else if (c >= L'a' && c <= L'f') v = v * 16 + (c - L'a' + 10);
        else return 0;
    }
    if (n != 6) return 0;
    *out = v;
    return 1;
}
