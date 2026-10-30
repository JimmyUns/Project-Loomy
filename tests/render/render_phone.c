// render_phone.c - draws the /phone screen in its states into bitmaps (cross-compile with mingw,
// run under Wine) so the layout can be eyeballed. Fake radio and clock; real phone.c + protocol.
//   x86_64-w64-mingw32-gcc -DLOOMY_TESTS -I../../src render_phone.c ../../src/phone.c ../../src/pocket.c
//     ../../src/monocypher.c ../../src/monocypher-ed25519.c ../../src/util.c -lgdi32 -o render_phone.exe
#include "loomy.h"
#include "pocket.h"
#include <stdio.h>

COLORREF g_accent = RGB(0xA8, 0x55, 0xF7);
HFONT g_font, g_fontSmall; int g_lineH = 20;
Config g_cfg;
int S(int px) { return px; }
void ui_fill(HDC dc, int l, int t, int r, int b, COLORREF c) { RECT rc = { l, t, r, b }; SetBkColor(dc, c); ExtTextOutW(dc, 0, 0, ETO_OPAQUE, &rc, 0, 0, 0); }
int ui_text(HDC dc, int x, int y, const wchar_t *t, COLORREF c) { SetTextColor(dc, c); TextOutW(dc, x, y, t, s_len(t)); SIZE sz; GetTextExtentPoint32W(dc, t, s_len(t), &sz); return sz.cx; }
COLORREF ui_mix(COLORREF a, COLORREF b, int p) { return RGB((GetRValue(a)*p+GetRValue(b)*(100-p))/100,(GetGValue(a)*p+GetGValue(b)*(100-p))/100,(GetBValue(a)*p+GetBValue(b)*(100-p))/100); }
void ui_redraw(void) {} void ui_set_input(const wchar_t *t) {} void ui_show(void) {} void ui_show_if_panel(void) {} void ui_hide(void) {}
void ui_balloon(const wchar_t *a, const wchar_t *b) {} void ui_set_timer(int i, int m) {} HWND ui_hwnd(void) { return 0; }
void settings_close(void) {} void cfg_save(void) {} const wchar_t *cfg_dir(void) { return L"."; }
int feature_on(int f) { return 1; }
void out(int k, const wchar_t *t) {} void outf(int k, const wchar_t *f, ...) {} void cmd_usage(const wchar_t *s) {}
void complete_add(const wchar_t *a, const wchar_t *b) {} void cmd_run(wchar_t *l) {} int cmd_phone_ok(const wchar_t *w) { return 0; }
static int listening;
int bt_listen(wchar_t *e, int c) { listening = 1; return 1; } void bt_unlisten(void) { listening = 0; } int bt_listening(void) { return listening; }
static unsigned char toPhone[4096]; static int toPhoneN;
int bt_send(const unsigned char *p, int n) { memcpy(toPhone + toPhoneN, p, n); toPhoneN += n; return 1; }
void bt_drop(void) {} void bt_event(WPARAM s, LPARAM e) {} void bt_shutdown(void) {}
static unsigned long long rs = 88172645463325252ull;
int os_random(void *p, int n) { for (int i = 0; i < n; i++) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; ((unsigned char *)p)[i] = (unsigned char)rs; } return 1; }
int os_protect(const unsigned char *in, int n, unsigned char *out, int cap, int un) { if (n > cap) return 0; memcpy(out, in, n); return n; }
static ULONGLONG clk = 13400000000ull;
ULONGLONG os_now(void) { return clk; }
void os_fmt_time(ULONGLONG s, wchar_t *b, int cap, int day) { if (day) s_cpy(b, L"Fri 6 Nov, 21:04", cap); else s_cpy(b, L"21:04", cap); }
int os_pc_name(char *u, int cap) { memcpy(u, "JIMMY-PC", 8); return 8; }
int phone_code_for_tests(char *out);

static HFONT mk(int px, int w) { return CreateFontW(-px, 0, 0, 0, w, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FIXED_PITCH, L"Consolas"); }

static void shot(const char *file) {
    int W = 720, H = 480;
    HDC sdc = GetDC(0), dc = CreateCompatibleDC(sdc);
    BITMAPINFO bi = { { sizeof(BITMAPINFOHEADER), W, -H, 1, 32, BI_RGB } };
    void *bits; HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, 0, 0);
    SelectObject(dc, bmp); SetBkMode(dc, TRANSPARENT);
    ui_fill(dc, 0, 0, W, H, C_BG);
    SelectObject(dc, mk(19, FW_BOLD)); int tw = ui_text(dc, 22, 14, L"Loomy", g_accent);
    SelectObject(dc, g_fontSmall); ui_text(dc, 22 + tw + 10, 17, L"v2.17 \x00B7 home", C_DIM);
    for (int r = 0; r < 4; r++) for (int x = (r & 1) * 3; x < W * 3 / 4 - r * W / 8; x += 6) ui_fill(dc, x, 46 + r * 6, x + 3, 46 + r * 6 + 3, ui_mix(g_accent, C_BG, 45));
    RECT area = { 22, 84, W - 22, H - 8 - 12 };
    phone_paint(dc, area);
    BITMAPFILEHEADER fh = { 0x4D42, sizeof(fh) + sizeof(BITMAPINFOHEADER) + W * H * 4, 0, 0, sizeof(fh) + sizeof(BITMAPINFOHEADER) };
    FILE *o = fopen(file, "wb"); fwrite(&fh, sizeof fh, 1, o); fwrite(&bi.bmiHeader, sizeof(BITMAPINFOHEADER), 1, o); fwrite(bits, W * H * 4, 1, o); fclose(o);
}

static void pair(const char *code, int finish) {
    PkConn ph; pk_init_phone(&ph, (const uint8_t *)code, 0, 0, 0, 0, (const uint8_t *)"Pixel 8", 7);
    toPhoneN = 0; phone_on_accept(L"AA:BB:CC:DD:EE:01");
    for (int r = 0; r < 6; r++) {
        int used, off = 0, n = toPhoneN; unsigned char b[4096]; memcpy(b, toPhone, n); toPhoneN = 0;
        while (off < n) { pk_feed(&ph, b + off, n - off, &used); off += used; }
        if (ph.outlen) { unsigned char o[1100]; int k = ph.outlen; memcpy(o, ph.out, k); ph.outlen = 0; phone_on_data(o, finish || r < 1 ? k : 0); }
    }
    if (finish) phone_on_closed();
}

int main(void) {
    g_font = mk(15, FW_NORMAL); g_fontSmall = mk(12, FW_NORMAL);
    phone_open();
    clk += 8; shot("phone_code.bmp");
    char code[9]; phone_code_for_tests(code);
    pair(code, 1);
    phone_tick(); shot("phone_trusted.bmp");
    phone_char(L'f'); shot("phone_forget.bmp"); phone_char(L'x');
    unsigned char junk[] = { 0x02, 0x01 };
    phone_on_accept(L"66:66:66:66:66:66"); phone_on_data(junk, 2);
    shot("phone_locked.bmp");
    return 0;
}
