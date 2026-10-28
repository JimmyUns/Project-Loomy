// sysview.c - draws the big sysinfo view (the data and keys live in sysinfo.c / sysdata.c).
//
// Look: a clinical test-lab console. Numbered sections, corner brackets, condensed caps
// labels, a segmented Flow meter. Orange = CPU, blue = GPU, the accent stays Loomy's.
// Everything is laid out from the area it gets, so it adapts to however big the window
// could grow on this screen and to however many GPUs there are.
//
// The extra fonts (Bahnschrift ships with Windows 10+, Segoe UI as the fallback) only
// exist while the view is open.
#include "loomy.h"

#define C_CARD   RGB(0x17, 0x0C, 0x22)
#define C_GRAPH  RGB(0x12, 0x08, 0x1A)
#define C_GRID   RGB(0x22, 0x14, 0x2E)
#define C_LABEL  RGB(0xA3, 0x93, 0xB8)   // small caps labels: a touch lighter than C_DIM so 11px stays readable
#define C_CPU    RGB(0xFF, 0x9A, 0x3C)
#define C_GPU    RGB(0x4F, 0xB3, 0xFF)
#define C_WARN   RGB(0xFF, 0xC4, 0x6B)

static HFONT g_fBig, g_fMid, g_fSec, g_fHead, g_fLabel;

// ---------------------------------------------------------------- fonts
static HFONT sv_font(int px, int weight, const wchar_t *face) {
    const wchar_t *faces[3] = { face, L"Bahnschrift", L"Segoe UI" };
    for (int i = 0; i < 3; i++) {
        HFONT f = CreateFontW(-S(px), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, faces[i]);
        HDC dc = GetDC(0);
        HGDIOBJ old = SelectObject(dc, f);
        wchar_t got[64]; GetTextFaceW(dc, 64, got);
        SelectObject(dc, old); ReleaseDC(0, dc);
        if (s_ieq(got, faces[i]) || i == 2) return f;
        DeleteObject(f);
    }
    return 0;
}

void sysview_fonts(int on) {
    HFONT *all[] = { &g_fBig, &g_fMid, &g_fSec, &g_fHead, &g_fLabel };
    for (int i = 0; i < 5; i++) { if (*all[i]) DeleteObject(*all[i]); *all[i] = 0; }
    if (!on) return;
    g_fBig   = sv_font(46, FW_SEMIBOLD, L"Bahnschrift SemiBold SemiConden");
    g_fMid   = sv_font(28, FW_SEMIBOLD, L"Bahnschrift SemiBold SemiConden");
    g_fSec   = sv_font(22, FW_BOLD,     L"Bahnschrift");
    g_fHead  = sv_font(15, FW_SEMIBOLD, L"Bahnschrift SemiBold");
    g_fLabel = sv_font(12, FW_NORMAL,   L"Bahnschrift SemiCondensed");
}

// ---------------------------------------------------------------- text helpers
static int text_w(HDC dc, const wchar_t *t) { SIZE sz; GetTextExtentPoint32W(dc, t, s_len(t), &sz); return sz.cx; }
static int text_h(HDC dc) { TEXTMETRICW tm; GetTextMetricsW(dc, &tm); return tm.tmHeight; }

static int text_r(HDC dc, int right, int y, const wchar_t *t, COLORREF c) {
    int w = text_w(dc, t);
    ui_text(dc, right - w, y, t, c);
    return w;
}

static void text_clip(HDC dc, int l, int y, int r, const wchar_t *t, COLORREF c) {
    if (r <= l) return;
    RECT rc = { l, y, r, y + S(40) };
    SetTextColor(dc, c);
    DrawTextW(dc, t, -1, &rc, DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
}

// spaced-out caps in a given font: "UTILIZATION"
static int caps(HDC dc, HFONT f, int x, int y, const wchar_t *t, COLORREF c) {
    wchar_t up[96]; int i = 0;
    for (; t[i] && i < 95; i++) up[i] = (t[i] >= L'a' && t[i] <= L'z') ? t[i] - 32 : t[i];
    up[i] = 0;
    SelectObject(dc, f);
    SetTextCharacterExtra(dc, S(2));
    int w = ui_text(dc, x, y, up, c);
    SetTextCharacterExtra(dc, 0);
    return w;
}
static int label(HDC dc, int x, int y, const wchar_t *t, COLORREF c) { return caps(dc, g_fLabel, x, y, t, c); }

// ---------------------------------------------------------------- number formatting
static void f_gb(wchar_t *b, unsigned __int64 bytes) {           // "11.9" (GB, no unit)
    unsigned __int64 t = (bytes * 10 + (1ull << 29)) >> 30;
    wsprintfW(b, L"%d.%d", (int)(t / 10), (int)(t % 10));
}
static void f_size(wchar_t *b, unsigned __int64 bytes) {         // "512 MB" / "3.1 GB"
    if (bytes < (1ull << 30)) { wsprintfW(b, L"%d MB", (int)((bytes + (1 << 19)) >> 20)); return; }
    f_gb(b, bytes); s_cat(b, L" GB", 24);
}
static void f_rate(wchar_t *b, unsigned __int64 bps) {           // "240 KB/s" / "7.9 MB/s"
    if (bps < 1024ull * 1024) { wsprintfW(b, L"%d KB/s", (int)(bps / 1024)); return; }
    unsigned __int64 t = bps * 10 / (1024ull * 1024);
    wsprintfW(b, L"%d.%d MB/s", (int)(t / 10), (int)(t % 10));
}
static void f_ghz(wchar_t *b, int mhz) {
    if (mhz <= 0) { s_cpy(b, L"n/a", 16); return; }
    wsprintfW(b, L"%d.%02d GHz", mhz / 1000, (mhz % 1000) / 10);
}
static void f_int(wchar_t *b, int n) {                           // 3104 -> "3,104"
    wchar_t raw[16]; wsprintfW(raw, L"%d", n);
    int len = s_len(raw), o = 0;
    for (int i = 0; i < len; i++) {
        b[o++] = raw[i];
        int left = len - 1 - i;
        if (left > 0 && left % 3 == 0 && raw[i] != L'-') b[o++] = L',';
    }
    b[o] = 0;
}
static void f_uptime(wchar_t *b, unsigned __int64 ms) {
    unsigned __int64 s = ms / 1000;
    int d = (int)(s / 86400), h = (int)(s / 3600 % 24), m = (int)(s / 60 % 60), x = (int)(s % 60);
    if (d) wsprintfW(b, L"%dd %d:%02d:%02d", d, h, m, x);
    else   wsprintfW(b, L"%d:%02d:%02d", h, m, x);
}
static void f_pct10(wchar_t *b, int v10) { wsprintfW(b, L"%d.%d%%", v10 / 10, v10 % 10); }

// ---------------------------------------------------------------- building blocks
static COLORREF level_color(int level) { return level == FLOW_SMOOTH ? C_OK : level == FLOW_STRAINED ? C_WARN : C_ERR; }
static const wchar_t *level_word(int level) { return level == FLOW_SMOOTH ? L"smooth" : level == FLOW_STRAINED ? L"strained" : L"choking"; }

static void frame(HDC dc, int l, int t, int r, int b, COLORREF c) {
    ui_fill(dc, l, t, r, t + 1, c); ui_fill(dc, l, b - 1, r, b, c);
    ui_fill(dc, l, t, l + 1, b, c); ui_fill(dc, r - 1, t, r, b, c);
}

// a section: card, border, accent corner brackets, "02  GPU 1   name" header. Returns the inside.
static RECT card(HDC dc, RECT r, int num, const wchar_t *title, const wchar_t *sub, int subRight) {
    ui_fill(dc, r.left, r.top, r.right, r.bottom, C_CARD);
    frame(dc, r.left, r.top, r.right, r.bottom, C_RULE);
    int L = S(10), T = S(2);
    ui_fill(dc, r.left, r.top, r.left + L, r.top + T, g_accent);        ui_fill(dc, r.left, r.top, r.left + T, r.top + L, g_accent);
    ui_fill(dc, r.right - L, r.top, r.right, r.top + T, g_accent);      ui_fill(dc, r.right - T, r.top, r.right, r.top + L, g_accent);
    ui_fill(dc, r.left, r.bottom - T, r.left + L, r.bottom, g_accent);  ui_fill(dc, r.left, r.bottom - L, r.left + T, r.bottom, g_accent);
    ui_fill(dc, r.right - L, r.bottom - T, r.right, r.bottom, g_accent); ui_fill(dc, r.right - T, r.bottom - L, r.right, r.bottom, g_accent);

    int x = r.left + S(14), y = r.top + S(9);
    wchar_t n[8]; wsprintfW(n, L"%02d", num);
    SelectObject(dc, g_fSec);
    x += ui_text(dc, x, y, n, g_accent) + S(10);
    x += caps(dc, g_fHead, x, y + S(6), title, C_BRIGHT) + S(12);
    if (sub && *sub) {
        SelectObject(dc, g_fontSmall);
        text_clip(dc, x, y + S(8), (subRight ? subRight : r.right) - S(14), sub, C_LABEL);
    }
    SelectObject(dc, g_font);
    RECT in = { r.left + S(14), r.top + S(44), r.right - S(14), r.bottom - S(12) };
    return in;
}

static void graph(HDC dc, RECT r, const unsigned char *h, COLORREF col) {
    if (r.bottom - r.top < S(12) || r.right - r.left < S(30)) return;
    ui_fill(dc, r.left, r.top, r.right, r.bottom, C_GRAPH);
    int W = r.right - r.left, H = r.bottom - r.top;
    for (int i = 1; i < 4; i++)  ui_fill(dc, r.left, r.top + H * i / 4, r.right, r.top + H * i / 4 + 1, C_GRID);
    for (int i = 1; i < 10; i++) ui_fill(dc, r.left + W * i / 10, r.top, r.left + W * i / 10 + 1, r.bottom, C_GRID);
    frame(dc, r.left, r.top, r.right, r.bottom, C_RULE);

    POINT pt[SI_HIST + 2];
    int iw = W - 2, ih = H - 3;
    for (int i = 0; i < SI_HIST; i++) {
        pt[i].x = r.left + 1 + i * iw / (SI_HIST - 1);
        pt[i].y = r.bottom - 2 - h[i] * ih * 96 / 10000;
    }
    pt[SI_HIST].x = r.right - 1;   pt[SI_HIST].y = r.bottom - 1;
    pt[SI_HIST + 1].x = r.left + 1; pt[SI_HIST + 1].y = r.bottom - 1;

    HBRUSH br = CreateSolidBrush(ui_mix(col, C_GRAPH, 20));
    HGDIOBJ ob = SelectObject(dc, br), op = SelectObject(dc, GetStockObject(NULL_PEN));
    Polygon(dc, pt, SI_HIST + 2);
    HPEN pen = CreatePen(PS_SOLID, (S(10) + 5) / 6, col);
    SelectObject(dc, pen);
    Polyline(dc, pt, SI_HIST);
    SelectObject(dc, op); SelectObject(dc, ob);
    DeleteObject(pen); DeleteObject(br);
}

static void hbar(HDC dc, int l, int y, int r, int h, int pct, COLORREF col) {
    ui_fill(dc, l, y, r, y + h, C_RULE);
    if (pct > 0) ui_fill(dc, l, y, l + (r - l) * (pct > 100 ? 100 : pct) / 100, y + h, col);
}

// big "42%" with a small % sign; returns its width
static int big_pct(HDC dc, HFONT f, int x, int y, int pct, COLORREF col) {
    wchar_t n[8]; wsprintfW(n, L"%d", pct);
    SelectObject(dc, f);
    int bh = text_h(dc);
    int w = ui_text(dc, x, y, n, col);
    SelectObject(dc, g_fHead);
    int sh = text_h(dc);
    w += ui_text(dc, x + w + S(2), y + bh - sh - S(bh > S(40) ? 6 : 3), L"%", col) + S(2);
    SelectObject(dc, g_font);
    return w;
}

// LABEL over a value
static void stat(HDC dc, int x, int y, const wchar_t *lab, const wchar_t *val, COLORREF c) {
    label(dc, x, y, lab, C_LABEL);
    SelectObject(dc, g_font);
    ui_text(dc, x, y + S(15), val, c);
}

#define STAT_H S(36)
#define FLOW_H S(30)

// "FLOW  91  smooth   Busy, but keeping up."  over a 24-step meter
static void flow_row(HDC dc, RECT r, const SiFlow *f) {
    int x = r.left, y = r.top;
    x += label(dc, x, y + S(3), L"Flow", C_LABEL) + S(10);
    if (f->score < 0) {
        SelectObject(dc, g_fontSmall);
        ui_text(dc, x, y + S(2), f->why, C_LABEL);
    } else {
        COLORREF c = level_color(f->level);
        wchar_t n[8]; wsprintfW(n, L"%d", f->score);
        SelectObject(dc, g_fHead);
        x += ui_text(dc, x, y, n, c) + S(8);
        SelectObject(dc, g_fontSmall);
        x += ui_text(dc, x, y + S(2), level_word(f->level), c) + S(12);
        text_clip(dc, x, y + S(2), r.right, f->why, C_LABEL);
    }
    int segs = 24, gap = S(3), by = r.bottom - S(7);
    int w = (r.right - r.left - gap * (segs - 1)) / segs;
    int lit = f->score < 0 ? 0 : (f->score * segs + 50) / 100;
    COLORREF c = f->score < 0 ? C_RULE : level_color(f->level);
    for (int i = 0; i < segs; i++) {
        int sx = r.left + i * (w + gap);
        ui_fill(dc, sx, by, i == segs - 1 ? r.right : sx + w, r.bottom, i < lit ? c : C_RULE);
    }
    SelectObject(dc, g_font);
}

// ---------------------------------------------------------------- the GPU bits shared by cards
static void gpu_mem_str(const SiGpu *g, wchar_t *b) {
    wchar_t u[16], t[16];
    f_gb(u, g->memUsed); f_gb(t, g->memTotal);
    wsprintfW(b, L"%s / %s GB", u, t);
}
static void gpu_temp_str(const SiGpu *g, wchar_t *b) {
    if (g->tempC > 0) wsprintfW(b, L"%d\x00B0" L"C", g->tempC); else s_cpy(b, L"n/a", 16);
}
static void gpu_clk_str(const SiGpu *g, wchar_t *b) {
    wchar_t n[16];
    if (g->clkMHz > 0) { f_int(n, g->clkMHz); wsprintfW(b, L"%s MHz", n); } else s_cpy(b, L"n/a", 16);
}
static COLORREF gpu_temp_color(const SiGpu *g) {
    int warn = g->tempWarnC > 0 ? g->tempWarnC : (g->integrated ? 95 : 85);
    return g->tempC >= warn - 3 ? C_ERR : C_BRIGHT;
}

static void gpu_flow(const SiSnap *s, const SiGpu *g, SiFlow *f) {
    if (!s->ready) { f->score = -1; f->level = FLOW_SMOOTH; s_cpy(f->why, L"Measuring...", 112); return; }
    flow_gpu(g, f);
}

// ---------------------------------------------------------------- general view cards
static void cpu_card(HDC dc, RECT r, const SiSnap *s, const SiFlow *fc) {
    wchar_t sub[128];
    wsprintfW(sub, L"%s \x00B7 %d cores \x00B7 %d threads", s->cpuName, s->cores, s->logical);
    RECT c = card(dc, r, 1, L"CPU", sub, 0);
    int blockBot = c.bottom - FLOW_H - STAT_H - S(20);
    SelectObject(dc, g_fBig);
    int bh = text_h(dc);
    int by = c.top + (blockBot - c.top - bh - S(16)) / 2;
    big_pct(dc, g_fBig, c.left, by, s->cpuUtil, C_CPU);
    label(dc, c.left, by + bh + S(2), L"Utilization", C_LABEL);
    RECT gr = { c.left + S(130), c.top, c.right, blockBot };
    graph(dc, gr, s->cpuHist, C_CPU);

    int y = blockBot + S(10), cw = (c.right - c.left) / 4;
    wchar_t v[32];
    f_ghz(v, s->speedMHz);       stat(dc, c.left,          y, L"Speed", v, C_BRIGHT);
    f_int(v, s->procs);          stat(dc, c.left + cw,     y, L"Processes", v, C_BRIGHT);
    f_int(v, s->threads);        stat(dc, c.left + cw * 2, y, L"Threads", v, C_BRIGHT);
    f_uptime(v, s->uptimeMs);    stat(dc, c.left + cw * 3, y, L"Up time", v, C_BRIGHT);
    RECT fr = { c.left, c.bottom - FLOW_H, c.right, c.bottom };
    flow_row(dc, fr, fc);
}

static void gpu_card_full(HDC dc, RECT r, const SiSnap *s, int i, const SiFlow *fg) {
    const SiGpu *g = &s->gpu[i];
    RECT c = card(dc, r, 2 + i, s->ngpu > 1 ? L"GPU" : L"GPU", g->name, 0);
    int blockBot = c.bottom - FLOW_H - STAT_H - S(20);
    SelectObject(dc, g_fBig);
    int bh = text_h(dc);
    int by = c.top + (blockBot - c.top - bh - S(16)) / 2;
    big_pct(dc, g_fBig, c.left, by, g->util, C_GPU);
    label(dc, c.left, by + bh + S(2), L"Utilization", C_LABEL);
    RECT gr = { c.left + S(130), c.top, c.right, blockBot };
    graph(dc, gr, g->hist, C_GPU);

    int y = blockBot + S(10), cw = (c.right - c.left) / 4;
    wchar_t v[40];
    gpu_clk_str(g, v);  stat(dc, c.left, y, L"Clock", v, C_BRIGHT);
    gpu_mem_str(g, v);  stat(dc, c.left + cw, y, g->integrated ? L"Shared mem" : L"VRAM", v, C_BRIGHT);
    gpu_temp_str(g, v); stat(dc, c.left + cw * 2, y, L"Temp", v, g->tempC > 0 ? gpu_temp_color(g) : C_DIM);
    stat(dc, c.left + cw * 3, y, L"Busiest", g->neng ? g->eng[0].name : L"n/a", C_BRIGHT);
    RECT fr = { c.left, c.bottom - FLOW_H, c.right, c.bottom };
    flow_row(dc, fr, fg);
}

// smaller GPU card for when there are 2+ GPUs (laptops: integrated + discrete)
static void gpu_card_compact(HDC dc, RECT r, const SiSnap *s, int i, const SiFlow *fg) {
    const SiGpu *g = &s->gpu[i];
    wchar_t title[16]; wsprintfW(title, L"GPU %d", i + 1);
    wchar_t sub[100]; s_cpy(sub, g->name, 100);
    if (g->integrated) s_cat(sub, L" \x00B7 integrated", 100);
    RECT c = card(dc, r, 2 + i, title, sub, 0);
    c.top -= S(6);                                  // a little tighter than the big cards
    int fh = FLOW_H;
    int bodyBot = c.bottom - fh - S(6);
    int bodyH = bodyBot - c.top;
    if (bodyH < S(30)) { RECT fr = { c.left, c.bottom - fh, c.right, c.bottom }; flow_row(dc, fr, fg); return; }

    SelectObject(dc, g_fMid);
    int mh = text_h(dc);
    big_pct(dc, g_fMid, c.left, c.top + (bodyH - mh) / 2 - S(2), g->util, C_GPU);

    int statsW = S(200);
    RECT gr = { c.left + S(84), c.top, c.right - statsW - S(12), bodyBot };
    graph(dc, gr, g->hist, C_GPU);

    int sx = c.right - statsW, lh = S(17), vx = sx + S(72);
    int sy = c.top + (bodyH - lh * 2) / 2;
    wchar_t v[40], t[16], k[16];
    label(dc, sx, sy + S(1), g->integrated ? L"Shared" : L"VRAM", C_LABEL);
    gpu_mem_str(g, v);
    SelectObject(dc, g_fontSmall); ui_text(dc, vx, sy, v, C_BRIGHT);
    label(dc, sx, sy + lh + S(1), L"Temp", C_LABEL);
    gpu_temp_str(g, t); gpu_clk_str(g, k);
    SelectObject(dc, g_fontSmall);
    int tw = ui_text(dc, vx, sy + lh, t, g->tempC > 0 ? gpu_temp_color(g) : C_DIM);
    if (g->clkMHz > 0) {                            // the clock too, when the driver reports it
        ui_text(dc, vx + tw, sy + lh, L"  \x00B7  ", C_DIM);
        ui_text(dc, vx + tw + text_w(dc, L"  \x00B7  "), sy + lh, k, C_TEXT);
    }

    RECT fr = { c.left, c.bottom - fh, c.right, c.bottom };
    flow_row(dc, fr, fg);
}

static void mem_card(HDC dc, RECT r, const SiSnap *s, int num) {
    wchar_t sub[24]; f_size(sub, s->memTotal);
    RECT c = card(dc, r, num, L"Memory", sub, 0);
    wchar_t used[16], tot[16], line[48];
    f_gb(used, s->memUsed); f_gb(tot, s->memTotal);
    int pct = s->memTotal ? (int)(s->memUsed * 100 / s->memTotal) : 0;
    SelectObject(dc, g_fMid);
    int mh = text_h(dc);
    int w = ui_text(dc, c.left, c.top - S(4), used, C_BRIGHT);
    SelectObject(dc, g_fontSmall);
    wsprintfW(line, L" / %s GB used", tot);
    ui_text(dc, c.left + w + S(4), c.top - S(4) + mh - S(18), line, C_LABEL);
    wsprintfW(line, L"%d%%", pct);
    SelectObject(dc, g_font);
    text_r(dc, c.right, c.top + S(4), line, pct >= 90 ? C_ERR : C_BRIGHT);

    int lastY = c.bottom - S(16);
    RECT gr = { c.left, c.top + mh + S(2), c.right, lastY - S(8) };
    graph(dc, gr, s->memHist, g_accent);
    wchar_t cm[16], cl[16]; f_gb(cm, s->commit); f_gb(cl, s->commitLimit);
    if (s->hardFaults >= 0) wsprintfW(line, L"committed %s / %s GB \x00B7 paging %d/s", cm, cl, s->hardFaults);
    else                    wsprintfW(line, L"committed %s / %s GB", cm, cl);
    SelectObject(dc, g_fontSmall);
    text_clip(dc, c.left, lastY, c.right, line, C_LABEL);
    SelectObject(dc, g_font);
}

static void io_card(HDC dc, RECT r, const SiSnap *s, int num) {
    RECT c = card(dc, r, num, L"Disk \x00B7 Net", 0, 0);
    wchar_t v[48], a[24], b[24];
    label(dc, c.left, c.top, L"Disk active", C_LABEL);
    if (s->diskActive >= 0) wsprintfW(v, L"%d%%", s->diskActive); else s_cpy(v, L"n/a", 48);
    SelectObject(dc, g_font);
    text_r(dc, c.right, c.top - S(2), v, s->diskActive >= 90 ? C_ERR : C_BRIGHT);
    hbar(dc, c.left, c.top + S(22), c.right, S(6), s->diskActive, s->diskActive >= 90 ? C_ERR : g_accent);
    f_rate(a, s->diskRead); f_rate(b, s->diskWrite);
    wsprintfW(v, L"read %s \x00B7 write %s", a, b);
    SelectObject(dc, g_fontSmall);
    text_clip(dc, c.left, c.top + S(34), c.right, v, C_LABEL);

    int y = c.bottom - STAT_H, cw = (c.right - c.left) / 2;
    f_rate(a, s->netDown); stat(dc, c.left, y, L"\x2193 Down", a, C_BRIGHT);
    f_rate(b, s->netUp);   stat(dc, c.left + cw, y, L"\x2191 Up", b, C_BRIGHT);
}

static void top_card(HDC dc, RECT r, const SiSnap *s, int num) {
    RECT c = card(dc, r, num, L"Top apps", L"by CPU", 0);
    int memW = S(76), cpuW = S(62);
    int rowH = g_lineH + S(3);
    int rows = (c.bottom - c.top + S(4)) / rowH;
    SelectObject(dc, g_font);
    if (!s->ntop) { ui_text(dc, c.left, c.top, s->ready ? L"Nothing running?" : L"Measuring...", C_DIM); return; }
    for (int i = 0; i < s->ntop && i < rows; i++) {
        int y = c.top - S(2) + i * rowH;
        const SiProc *p = &s->top[i];
        text_clip(dc, c.left, y, c.right - memW - cpuW - S(8), p->name, C_BRIGHT);
        wchar_t v[24];
        f_pct10(v, p->cpu10); text_r(dc, c.right - memW, y, v, C_CPU);
        f_size(v, p->mem);    text_r(dc, c.right, y, v, C_LABEL);
    }
}

// ---------------------------------------------------------------- the full CPU / GPU views
static void cpu_view(HDC dc, RECT r, const SiSnap *s, const SiFlow *fc) {
    wchar_t sub[160], base[16];
    f_ghz(base, s->baseMHz);
    wsprintfW(sub, L"%s \x00B7 %d cores \x00B7 %d threads \x00B7 base %s", s->cpuName, s->cores, s->logical, base);
    SelectObject(dc, g_fMid);
    int pw = text_w(dc, L"100%") + S(20);
    RECT c = card(dc, r, 1, L"CPU", sub, r.right - pw);
    big_pct(dc, g_fMid, r.right - pw, r.top + S(8), s->cpuUtil, C_CPU);

    int H = c.bottom - c.top;
    int barsH = H * 22 / 100; if (barsH < S(36)) barsH = S(36); if (barsH > S(96)) barsH = S(96);
    int statsY = c.bottom - FLOW_H - STAT_H - S(12);
    int barsTop = statsY - S(14) - barsH;
    int labY = barsTop - S(20);
    RECT gr = { c.left, c.top, c.right, labY - S(10) };
    graph(dc, gr, s->cpuHist, C_CPU);

    label(dc, c.left, labY, L"Per thread", C_LABEL);
    int n = s->nthread > 0 ? s->nthread : 1;
    int busiest = 0;
    for (int i = 0; i < n; i++) if (s->thread[i] > busiest) busiest = s->thread[i];
    wchar_t v[48];
    wsprintfW(v, L"busiest %d%%", busiest);
    SelectObject(dc, g_fontSmall);
    text_r(dc, c.right, labY, v, C_LABEL);
    int gap = n > 32 ? S(2) : n > 16 ? S(3) : S(5);
    int bw = (c.right - c.left - gap * (n - 1)) / n; if (bw < 1) bw = 1;
    for (int i = 0; i < n; i++) {
        int x = c.left + i * (bw + gap);
        ui_fill(dc, x, barsTop, x + bw, barsTop + barsH, C_GRAPH);
        frame(dc, x, barsTop, x + bw, barsTop + barsH, C_RULE);
        int fh = (barsH - 2) * s->thread[i] / 100;
        if (fh > 0) ui_fill(dc, x + 1, barsTop + barsH - 1 - fh, x + bw - 1, barsTop + barsH - 1, s->thread[i] >= 90 ? C_ERR : C_CPU);
    }

    int cw = (c.right - c.left) / 6;
    f_ghz(v, s->speedMHz);                                stat(dc, c.left, statsY, L"Speed", v, C_BRIGHT);
    wsprintfW(v, L"%d.%d", s->queue10 / 10, s->queue10 % 10);
    stat(dc, c.left + cw, statsY, L"Waiting", v, s->queue10 > s->logical * 5 ? C_WARN : C_BRIGHT);
    if (s->dpc >= 0) wsprintfW(v, L"%d%%", s->dpc); else s_cpy(v, L"n/a", 48);
    stat(dc, c.left + cw * 2, statsY, L"Drivers", v, s->dpc >= 8 ? C_WARN : C_BRIGHT);
    f_int(v, s->procs);                                   stat(dc, c.left + cw * 3, statsY, L"Processes", v, C_BRIGHT);
    f_int(v, s->threads);                                 stat(dc, c.left + cw * 4, statsY, L"Threads", v, C_BRIGHT);
    f_uptime(v, s->uptimeMs);                             stat(dc, c.left + cw * 5, statsY, L"Up time", v, C_BRIGHT);
    RECT fr = { c.left, c.bottom - FLOW_H, c.right, c.bottom };
    flow_row(dc, fr, fc);
}

static void gpu_view(HDC dc, RECT r, const SiSnap *s, int i, const SiFlow *fg) {
    const SiGpu *g = &s->gpu[i];
    wchar_t title[16], sub[160], tot[24];
    wsprintfW(title, L"GPU %d", i + 1);
    f_size(tot, g->memTotal);
    wsprintfW(sub, L"%s \x00B7 %s \x00B7 %s %s", g->name, g->integrated ? L"integrated" : L"dedicated",
              tot, g->integrated ? L"shared" : L"VRAM");
    SelectObject(dc, g_fMid);
    int pw = text_w(dc, L"100%") + S(20);
    RECT c = card(dc, r, 2 + i, title, sub, r.right - pw);
    big_pct(dc, g_fMid, r.right - pw, r.top + S(8), g->util, C_GPU);

    int rowH = S(20);
    int nrows = g->neng ? g->neng : 1;
    int statsY = c.bottom - FLOW_H - STAT_H - S(12);
    int engTop = statsY - S(14) - nrows * rowH;
    int labY = engTop - S(20);
    RECT gr = { c.left, c.top, c.right, labY - S(10) };
    graph(dc, gr, g->hist, C_GPU);

    label(dc, c.left, labY, L"Engines", C_LABEL);
    SelectObject(dc, g_font);
    if (!g->neng) ui_text(dc, c.left, engTop, L"No engine counters (needs Windows 10 1709+ and a WDDM 2 driver).", C_DIM);
    for (int e = 0; e < g->neng; e++) {
        int y = engTop + e * rowH;
        wchar_t v[8]; wsprintfW(v, L"%d%%", g->eng[e].pct);
        SelectObject(dc, g_fontSmall);
        text_clip(dc, c.left, y + S(1), c.left + S(130), g->eng[e].name, C_TEXT);
        text_r(dc, c.right, y + S(1), v, C_BRIGHT);
        hbar(dc, c.left + S(140), y + S(6), c.right - S(52), S(6), g->eng[e].pct, C_GPU);
    }

    int cw = (c.right - c.left) / 4;
    wchar_t v[48], m[16];
    gpu_clk_str(g, v);
    if (g->clkMHz > 0 && g->clkMaxMHz > 0) { f_int(m, g->clkMaxMHz); s_cat(v, L" / ", 48); s_cat(v, m, 48); }
    stat(dc, c.left, statsY, L"Clock", v, C_BRIGHT);
    gpu_mem_str(g, v); stat(dc, c.left + cw, statsY, g->integrated ? L"Shared mem" : L"VRAM", v, C_BRIGHT);
    gpu_temp_str(g, v);
    if (g->tempC > 0 && g->tempWarnC > 0) { wsprintfW(m, L"  (warn %d)", g->tempWarnC); s_cat(v, m, 48); }
    stat(dc, c.left + cw * 2, statsY, L"Temp", v, g->tempC > 0 ? gpu_temp_color(g) : C_DIM);
    stat(dc, c.left + cw * 3, statsY, L"Busiest", g->neng ? g->eng[0].name : L"n/a", C_BRIGHT);
    RECT fr = { c.left, c.bottom - FLOW_H, c.right, c.bottom };
    flow_row(dc, fr, fg);
}

// ---------------------------------------------------------------- the whole view
void sysview_paint(HDC dc, RECT a, SiView *v) {
    const SiSnap *s = v->snap;
    SiFlow fc, fg[SI_MAXGPU];
    flow_cpu(s, &fc);
    int worst = fc.level;
    for (int i = 0; i < s->ngpu; i++) { gpu_flow(s, &s->gpu[i], &fg[i]); if (fg[i].score >= 0 && fg[i].level > worst) worst = fg[i].level; }

    // tabs: GENERAL  CPU  GPU 1  GPU 2 ...
    int x = a.left, y = a.top;
    v->ntabs = 2 + s->ngpu;
    for (int i = 0; i < v->ntabs; i++) {
        wchar_t t[16];
        if (i == SIV_GENERAL) s_cpy(t, L"General", 16);
        else if (i == SIV_CPU) s_cpy(t, L"CPU", 16);
        else wsprintfW(t, L"GPU %d", i - SIV_GPU + 1);
        int on = i == v->view;
        int w = caps(dc, g_fHead, x, y, t, on ? C_BRIGHT : C_LABEL);
        if (on) ui_fill(dc, x, y + S(25), x + w, y + S(27), g_accent);
        RECT tr = { x - S(6), y - S(6), x + w + S(6), y + S(30) };
        v->tabs[i] = tr;
        x += w + S(26);
    }
    ui_fill(dc, a.left, y + S(27), a.right, y + S(28), C_RULE);
    {
        const wchar_t *word = !s->ready ? L"calibrating" : worst == FLOW_SMOOTH ? L"nominal" : worst == FLOW_STRAINED ? L"strained" : L"critical";
        COLORREF wc = !s->ready ? C_LABEL : level_color(worst);
        wchar_t up[16]; int i = 0;
        for (; word[i]; i++) up[i] = word[i] - 32;
        up[i] = 0;
        int ww = caps(dc, g_fLabel, 0, -S(100), up, wc);           // measure (drawn off-screen, clipped)
        caps(dc, g_fLabel, a.right - ww, y + S(4), up, wc);
        int lw = caps(dc, g_fLabel, 0, -S(100), L"Status: ", C_LABEL);
        caps(dc, g_fLabel, a.right - ww - lw, y + S(4), L"Status: ", C_LABEL);
    }

    // footer
    int fy = a.bottom - S(16);
    SelectObject(dc, g_fontSmall);
    ui_text(dc, a.left, fy, L"Tab switch view    Backspace back    Esc close", C_DIM);
    {
        wchar_t me[96], cpu[16], mem[24];
        f_pct10(cpu, s->selfCpu10); f_size(mem, s->selfMem);
        wsprintfW(me, L"sampling every 1 s \x00B7 Loomy itself: %s CPU, %s", cpu, mem);
        text_r(dc, a.right, fy, me, C_DIM);
    }
    SelectObject(dc, g_font);

    RECT ct = { a.left, a.top + S(40), a.right, fy - S(12) };
    int gap = S(12);

    if (v->view == SIV_CPU) { cpu_view(dc, ct, s, &fc); return; }
    if (v->view >= SIV_GPU && v->view - SIV_GPU < s->ngpu) { gpu_view(dc, ct, s, v->view - SIV_GPU, &fg[v->view - SIV_GPU]); return; }

    // general: CPU | GPU(s) on top, memory | disk+net | top apps below
    int H = ct.bottom - ct.top;
    int row1 = H * 58 / 100;
    int mid = (ct.left + ct.right) / 2;
    RECT rc = { ct.left, ct.top, mid - gap / 2, ct.top + row1 };
    cpu_card(dc, rc, s, &fc);
    RECT rg = { mid + gap / 2, ct.top, ct.right, ct.top + row1 };
    if (s->ngpu == 0) {
        RECT c = card(dc, rg, 2, L"GPU", 0, 0);
        SelectObject(dc, g_font);
        ui_text(dc, c.left, c.top, L"Windows isn't listing a GPU here.", C_DIM);
    } else if (s->ngpu == 1) {
        gpu_card_full(dc, rg, s, 0, &fg[0]);
    } else {
        int n = s->ngpu, h = (row1 - gap * (n - 1)) / n;
        for (int i = 0; i < n; i++) {
            RECT r = { rg.left, rg.top + i * (h + gap), rg.right, i == n - 1 ? rg.bottom : rg.top + i * (h + gap) + h };
            gpu_card_compact(dc, r, s, i, &fg[i]);
        }
    }

    int top2 = ct.top + row1 + gap, W = ct.right - ct.left - gap * 2;
    int w1 = W * 100 / 335, w2 = W * 100 / 335;
    int num = 2 + (s->ngpu ? s->ngpu : 1);
    RECT rm = { ct.left, top2, ct.left + w1, ct.bottom };
    RECT ri = { rm.right + gap, top2, rm.right + gap + w2, ct.bottom };
    RECT rt = { ri.right + gap, top2, ct.right, ct.bottom };
    mem_card(dc, rm, s, num);
    io_card(dc, ri, s, num + 1);
    top_card(dc, rt, s, num + 2);
}
