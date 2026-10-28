// render.c - draws the sysinfo views into bitmaps with fake data, so the layout can be eyeballed
// without a real Windows box (cross-compile with mingw, run under Wine). Not part of the build.
//   x86_64-w64-mingw32-gcc -municode -I../../src render.c ../../src/sysinfo.c ../../src/sysview.c ../../src/util.c -lgdi32 -o render.exe
#include "loomy.h"

COLORREF g_accent = RGB(0xA8, 0x55, 0xF7);
HFONT g_font, g_fontSmall; int g_lineH = 20;
int S(int px) { return px; }
void ui_fill(HDC dc, int l, int t, int r, int b, COLORREF c) { RECT rc = { l, t, r, b }; SetBkColor(dc, c); ExtTextOutW(dc, 0, 0, ETO_OPAQUE, &rc, 0, 0, 0); }
int ui_text(HDC dc, int x, int y, const wchar_t *t, COLORREF c) { SetTextColor(dc, c); TextOutW(dc, x, y, t, s_len(t)); SIZE sz; GetTextExtentPoint32W(dc, t, s_len(t), &sz); return sz.cx; }
COLORREF ui_mix(COLORREF a, COLORREF b, int p) { return RGB((GetRValue(a)*p+GetRValue(b)*(100-p))/100,(GetGValue(a)*p+GetGValue(b)*(100-p))/100,(GetBValue(a)*p+GetBValue(b)*(100-p))/100); }
void ui_redraw(void) {} void ui_set_input(const wchar_t *t) {} void ui_set_timer_1s(int on) {} void ui_set_big(int on) {}
void settings_close(void){} void browser_close(void){} void net_close(void){} void changelog_close(void){} void mode_close(void){} void web_close(void){} void display_close(void){}
void out(int k, const wchar_t *t) {} void outf(int k, const wchar_t *f, ...) {} void cmd_usage(const wchar_t *s) {} void complete_add(const wchar_t *a, const wchar_t *b) {}

static int g_ngpu = 2, g_choke;
int sysdata_open(SiSnap *s) {
    memset(s, 0, sizeof(*s));
    s_cpy(s->cpuName, L"Intel Core i7-11800H", 64);
    s->cores = 8; s->logical = 16; s->nthread = 16; s->baseMHz = 2304;
    s->ngpu = g_ngpu;
    s_cpy(s->gpu[0].name, L"NVIDIA GeForce RTX 3050 Ti Laptop GPU", 80);
    s->gpu[0].memTotal = 4ull << 30; s->gpu[0].tempWarnC = 87;
    if (g_ngpu > 1) { s_cpy(s->gpu[1].name, L"Intel UHD Graphics", 80); s->gpu[1].integrated = 1; s->gpu[1].memTotal = 8ull << 30; s->gpu[1].tempWarnC = -1; }
    return 1;
}
static int wave(int i, int base, int amp, int k) { int x = (i * 37 + k * 11) % 100; return base + (x < 50 ? x : 100 - x) * amp / 50 - amp / 2; }
void sysdata_sample(SiSnap *s) {
    s->ready = 1;
    s->cpuUtil = g_choke ? 98 : 64; s->perfPct = g_choke ? 93 : 186; s->speedMHz = s->baseMHz * s->perfPct / 100;
    s->queue10 = g_choke ? 190 : 11; s->dpc = 2; s->procs = 231; s->threads = 3104; s->handles = 101233; s->uptimeMs = 2 * 3600000ull + 14 * 60000 + 7000;
    for (int i = 0; i < 16; i++) s->thread[i] = (unsigned char)wave(i, g_choke ? 96 : 60, 50, 3) > 100 ? 100 : wave(i, g_choke ? 96 : 60, 50, 3);
    for (int i = 0; i < SI_HIST; i++) { s->cpuHist[i] = wave(i, 60, 30, 1); s->memHist[i] = 72 + (i % 7); s->gpu[0].hist[i] = wave(i, 88, 20, 2); s->gpu[1].hist[i] = wave(i, 12, 10, 5); }
    s->memTotal = 16ull << 30; s->memUsed = g_choke ? (15ull << 30) + (500ull << 20) : 11ull << 30; s->commit = 14ull << 30; s->commitLimit = 31ull << 30; s->hardFaults = g_choke ? 1400 : 12;
    s->diskActive = 7; s->diskRead = 12ull << 20; s->diskWrite = 3ull << 20; s->netDown = 520ull << 10; s->netUp = 90ull << 10;
    SiGpu *g = &s->gpu[0];
    g->util = 96; g->memUsed = (3ull << 30) + (300ull << 20); g->tempC = g_choke ? 86 : 71; g->clkMHz = 1695; g->clkMaxMHz = 1785;
    g->neng = 4; s_cpy(g->eng[0].name, L"3D", 24); g->eng[0].pct = 96; s_cpy(g->eng[1].name, L"Copy", 24); g->eng[1].pct = 8;
    s_cpy(g->eng[2].name, L"Video decode", 24); g->eng[2].pct = 3; s_cpy(g->eng[3].name, L"Compute 0", 24); g->eng[3].pct = 1;
    g = &s->gpu[1]; g->util = 11; g->memUsed = 900ull << 20; g->tempC = -1; g->clkMHz = -1; g->clkMaxMHz = -1;
    g->neng = 2; s_cpy(g->eng[0].name, L"3D", 24); g->eng[0].pct = 11; s_cpy(g->eng[1].name, L"Video processing", 24); g->eng[1].pct = 2;
    const wchar_t *n[5] = { L"Cyberpunk2077", L"Discord", L"chrome", L"audiodg", L"dwm" }; int c[5] = { 418, 32, 21, 10, 9 }; ULONGLONG m[5] = { 5300ull << 20, 340ull << 20, 1400ull << 20, 28ull << 20, 96ull << 20 };
    s->ntop = 5; for (int i = 0; i < 5; i++) { s_cpy(s->top[i].name, n[i], 40); s->top[i].cpu10 = c[i]; s->top[i].mem = m[i]; }
    s->selfCpu10 = 1; s->selfMem = 3200ull << 10;
}
void sysdata_close(void) {}

static HFONT mk(int px, int w) { return CreateFontW(-px, 0, 0, 0, w, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FIXED_PITCH, L"Consolas"); }

static void shot(const char *file, int W, int H, int view) {
    HDC sdc = GetDC(0), dc = CreateCompatibleDC(sdc);
    BITMAPINFO bi = { { sizeof(BITMAPINFOHEADER), W, -H, 1, 32, BI_RGB } };
    void *bits; HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, 0, 0);
    SelectObject(dc, bmp); SetBkMode(dc, TRANSPARENT);
    ui_fill(dc, 0, 0, W, H, C_BG);
    SelectObject(dc, mk(19, FW_BOLD)); int tw = ui_text(dc, 22, 14, L"Loomy", g_accent);
    SelectObject(dc, g_fontSmall); ui_text(dc, 22 + tw + 10, 17, L"v2.13 \x00B7 home", C_DIM);
    for (int r = 0; r < 4; r++) for (int x = (r & 1) * 3; x < W * 3 / 4 - r * W / 8; x += 6) ui_fill(dc, x, 46 + r * 6, x + 3, 46 + r * 6 + 3, ui_mix(g_accent, C_BG, 45));
    int inputTop = H - 52;
    RECT area = { 22, 84, W - 22, inputTop - 12 };
    sysinfo_open(view); sysinfo_tick();
    SelectObject(dc, g_font);
    IntersectClipRect(dc, area.left, area.top, area.right, area.bottom);
    sysinfo_paint(dc, area);
    SelectClipRgn(dc, 0);
    ui_fill(dc, 0, inputTop, W, H, C_INPUTBG); ui_fill(dc, 0, inputTop, W, inputTop + 1, C_RULE);
    SelectObject(dc, g_font); ui_text(dc, 22, inputTop + 16, L"\x203A", g_accent);
    sysinfo_close();
    BITMAPFILEHEADER fh = { 0x4D42, sizeof(fh) + sizeof(BITMAPINFOHEADER) + W * H * 4, 0, 0, sizeof(fh) + sizeof(BITMAPINFOHEADER) };
    HANDLE f = CreateFileA(file, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0); DWORD wr;
    WriteFile(f, &fh, sizeof(fh), &wr, 0); WriteFile(f, &bi.bmiHeader, sizeof(BITMAPINFOHEADER), &wr, 0); WriteFile(f, bits, W * H * 4, &wr, 0); CloseHandle(f);
}

int wmain(void) {
    g_font = mk(15, FW_NORMAL); g_fontSmall = mk(12, FW_NORMAL);
    shot("picker.bmp", 720, 480, -1);
    shot("general2.bmp", 1080, 720, SIV_GENERAL);
    shot("cpu.bmp", 1080, 720, SIV_CPU);
    shot("gpu1.bmp", 1080, 720, SIV_GPU);
    shot("small.bmp", 1080, 640, SIV_GENERAL);
    g_choke = 1; shot("choking.bmp", 1080, 720, SIV_GENERAL);
    g_choke = 0; g_ngpu = 1; shot("general1.bmp", 1080, 720, SIV_GENERAL);
    return 0;
}
