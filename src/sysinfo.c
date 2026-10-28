// sysinfo.c - the `sysinfo` / `si` command: what your PC is doing right now.
//   sysinfo                    picker: General, CPU, one row per GPU
//   sysinfo general|cpu        straight to that view
//   sysinfo gpu [n]            straight to GPU n (1 = the first, discrete ones come first)
//
// Picking a view grows Loomy to ~1.5x (as much as fits the screen) and shows live numbers,
// sampled once a second only while it's open. Esc puts everything back to normal size and
// frees the sensors, so it costs nothing while you're not looking.
//
// "Flow" is the custom score: high CPU or GPU load is fine on its own, so it only drops
// for the things you'd actually feel (threads stuck waiting, clocks held back by heat or
// power, RAM full and paging to disk, drivers hogging the CPU, a hot GPU, full VRAM).
#include "loomy.h"

// ---------------------------------------------------------------- Flow (pure: tested on Linux)
typedef struct { int pen; wchar_t why[64]; } Pen;

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void add_pen(Pen *p, int *n, int pen, const wchar_t *why) {
    if (pen <= 0 || *n >= 6) return;
    p[*n].pen = pen; s_cpy(p[*n].why, why, 64); (*n)++;
}

// biggest reasons first, the top two joined: "15 threads waiting for a core · held back to 2.10 GHz"
static void finish(Pen *p, int n, SiFlow *f, const wchar_t *fine) {
    int sum = 0;
    for (int i = 0; i < n; i++) sum += p[i].pen;
    for (int i = 1; i < n; i++) { Pen t = p[i]; int j = i - 1; while (j >= 0 && p[j].pen < t.pen) { p[j + 1] = p[j]; j--; } p[j + 1] = t; }
    f->score = clampi(100 - sum, 0, 100);
    f->level = f->score >= 85 ? FLOW_SMOOTH : f->score >= 60 ? FLOW_STRAINED : FLOW_CHOKING;
    if (!n) { s_cpy(f->why, fine, 112); return; }
    if (f->level == FLOW_SMOOTH) { s_cpy(f->why, L"Mostly fine: ", 112); s_cat(f->why, p[0].why, 112); return; }
    s_cpy(f->why, p[0].why, 112);
    if (n > 1) { s_cat(f->why, L" \x00B7 ", 112); s_cat(f->why, p[1].why, 112); }
    if (f->why[0] >= L'a' && f->why[0] <= L'z') f->why[0] -= 32;
}

void flow_cpu(const SiSnap *s, SiFlow *f) {
    if (!s->ready) { f->score = -1; f->level = FLOW_SMOOTH; s_cpy(f->why, L"Measuring...", 112); return; }
    Pen p[6]; int n = 0; wchar_t w[64];
    int logical = s->logical > 0 ? s->logical : 1;

    // threads ready to run but no free core: the real "it's choking" signal
    int per100 = s->queue10 * 10 / logical;                  // waiting per logical core, x100
    if (per100 > 25) {
        wsprintfW(w, L"%d threads waiting for a core", (s->queue10 + 5) / 10);
        add_pen(p, &n, clampi((per100 - 25) * 40 / 100, 1, 40), w);
    }
    // busy but running under its normal speed = heat or power limit (idle downclocking is fine)
    if (s->cpuUtil >= 50 && s->perfPct > 0 && s->perfPct < 90) {
        int mhz = s->speedMHz > 0 ? s->speedMHz : 0;
        wsprintfW(w, L"held back to %d.%02d GHz (heat or power)", mhz / 1000, (mhz % 1000) / 10);
        add_pen(p, &n, clampi(90 - s->perfPct, 1, 35), w);
    }
    // RAM: nearly full, or already paging back in from disk
    if (s->memTotal) {
        int freePct = (int)((s->memTotal - s->memUsed) * 100 / s->memTotal);
        if (freePct < 10) {
            wsprintfW(w, L"RAM %d%% full", 100 - freePct);
            add_pen(p, &n, clampi((10 - freePct) * 3, 1, 25), w);
        }
    }
    if (s->hardFaults > 300) {
        wsprintfW(w, L"swapping to disk (%d pages/s)", s->hardFaults);
        add_pen(p, &n, clampi(s->hardFaults / 60, 1, 25), w);
    }
    // drivers stealing time (audio crackle / stutter territory)
    if (s->dpc >= 8) {
        wsprintfW(w, L"drivers using %d%% of the CPU", s->dpc);
        add_pen(p, &n, clampi((s->dpc - 8) * 2, 1, 20), w);
    }
    if (s->cpuUtil >= 97) add_pen(p, &n, 6, L"maxed out");

    finish(p, n, f, s->cpuUtil >= 50 ? L"Busy, but keeping up. Nothing waiting." :
                    s->cpuUtil >= 15 ? L"Comfortable. Nothing waiting." : L"Relaxed. Nothing waiting.");
}

void flow_gpu(const SiGpu *g, SiFlow *f) {
    Pen p[6]; int n = 0; wchar_t w[64];
    if (g->tempC > 0) {
        int warn = g->tempWarnC > 0 ? g->tempWarnC : (g->integrated ? 95 : 85);
        if (g->tempC >= warn - 3) {
            wsprintfW(w, L"hot (%d\x00B0" L"C)", g->tempC);
            add_pen(p, &n, clampi((g->tempC - (warn - 3)) * 4, 1, 35), w);
        }
    }
    if (!g->integrated && g->memTotal) {
        int pct = (int)(g->memUsed * 100 / g->memTotal);
        if (pct > 90) {
            wsprintfW(w, L"VRAM %d%% full", pct);
            add_pen(p, &n, clampi((pct - 90) * 3, 1, 30), w);
        }
    }
    int clkPct = g->clkMHz > 0 && g->clkMaxMHz > 0 ? g->clkMHz * 100 / g->clkMaxMHz : -1;
    if (g->util >= 60 && clkPct >= 0 && clkPct < 70) {
        wsprintfW(w, L"clock held at %d MHz while busy", g->clkMHz);
        add_pen(p, &n, clampi(70 - clkPct, 1, 30), w);
    }
    finish(p, n, f, g->util >= 85 ? (clkPct >= 85 ? L"Maxed at full clock. Normal in a game." : L"Working hard, no warning signs.") :
                    g->util >= 20 ? L"Comfortable." : g->util >= 3 ? L"Plenty of headroom." : L"Idle.");
}

// ---------------------------------------------------------------- state
static SiSnap g_snap;
static SiView g_v;
static int g_active, g_expanded, g_view, g_sel, g_listTop, g_rowStep;

int sysinfo_active(void)   { return g_active; }
int sysinfo_expanded(void) { return g_active && g_expanded; }

static int nviews(void) { return 2 + g_snap.ngpu; }

static void expand(int view) {
    g_view = clampi(view, 0, nviews() - 1);
    if (!g_expanded) { g_expanded = 1; sysview_fonts(1); ui_set_big(1); }
    ui_redraw();
}

void sysinfo_open(int view) {
    settings_close(); browser_close(); net_close(); changelog_close(); mode_close(); web_close(); display_close();
    if (!g_active) {
        sysdata_open(&g_snap);
        g_active = 1; g_expanded = 0;
        ui_set_timer_1s(1);
    }
    ui_set_input(L"");
    if (view >= 0) expand(view);
    else { g_sel = 0; ui_redraw(); }
}

void sysinfo_close(void) {
    if (!g_active) return;
    g_active = 0;
    ui_set_timer_1s(0);
    if (g_expanded) { g_expanded = 0; ui_set_big(0); sysview_fonts(0); }
    sysdata_close();
    ui_redraw();
}

void sysinfo_tick(void) {
    if (!g_active) return;
    sysdata_sample(&g_snap);
    if (g_expanded) ui_redraw();
}

// ---------------------------------------------------------------- the command
#define SI_USAGE L"[general|cpu|gpu [n]]"

void cmd_sysinfo(wchar_t *args) {
    wchar_t *w[3];
    int n = s_words(args, w, 3);
    if (n == 0) { sysinfo_open(-1); return; }
    int view = -1, gpu = 1;
    if (s_ieq(w[0], L"general") && n == 1) view = SIV_GENERAL;
    else if (s_ieq(w[0], L"cpu") && n == 1) view = SIV_CPU;
    else if (s_ieq(w[0], L"gpu") && n <= 2) {
        if (n == 2) {
            gpu = 0;
            for (wchar_t *c = w[1]; *c; c++) {
                if (*c < L'0' || *c > L'9' || gpu > 99) { gpu = -1; break; }
                gpu = gpu * 10 + (*c - L'0');
            }
            if (gpu < 0) { outf(OUT_ERR, L"'%s' isn't a GPU number.", w[1]); cmd_usage(SI_USAGE); return; }
        }
        view = SIV_GPU;
    }
    if (view < 0) {
        if (n > 1 || s_ieq(w[0], L"general") || s_ieq(w[0], L"cpu")) cmd_usage(SI_USAGE);
        else { outf(OUT_ERR, L"No sysinfo view called '%s'.", w[0]); cmd_usage(SI_USAGE); }
        return;
    }
    sysinfo_open(-1);                                         // reads the GPU list
    if (view == SIV_GPU) {
        if (g_snap.ngpu == 0) { sysinfo_close(); out(OUT_ERR, L"Windows isn't showing me any GPU."); return; }
        if (gpu < 1 || gpu > g_snap.ngpu) {
            sysinfo_close();
            outf(OUT_ERR, L"There's no GPU %d. This PC has %d.", gpu, g_snap.ngpu);
            return;
        }
        view = SIV_GPU + gpu - 1;
    }
    expand(view);
}

void sysinfo_complete(wchar_t **words, int n, const wchar_t *partial) {
    static const wchar_t *SUBS[] = { L"general", L"cpu", L"gpu" };
    if (n == 1) for (int i = 0; i < 3; i++) complete_add(SUBS[i], partial);
}

// ---------------------------------------------------------------- keys + mouse
static void move(int d) {
    g_sel = clampi(g_sel + d, 0, nviews() - 1);
    ui_redraw();
}

static void switch_view(int d) {
    int n = nviews();
    g_view = (g_view + d + n) % n;
    ui_redraw();
}

int sysinfo_key(WPARAM vk) {
    if (!g_expanded) {
        switch (vk) {
        case VK_UP:     move(-1); break;
        case VK_DOWN:   move(+1); break;
        case VK_HOME:   move(-99); break;
        case VK_END:    move(+99); break;
        case VK_RETURN: expand(g_sel); break;
        case VK_ESCAPE: sysinfo_close(); break;
        }
        return 1;
    }
    switch (vk) {
    case VK_TAB:    switch_view(GetKeyState(VK_SHIFT) < 0 ? -1 : 1); break;
    case VK_RIGHT:  switch_view(+1); break;
    case VK_LEFT:   switch_view(-1); break;
    case VK_BACK:                                             // back to the picker, normal size
        g_expanded = 0; ui_set_big(0); sysview_fonts(0); g_sel = g_view; ui_redraw(); break;
    case VK_ESCAPE: sysinfo_close(); break;
    }
    return 1;
}

// g = general, c = cpu, 1-9 = that GPU (both in the picker and the big view)
int sysinfo_char(WPARAM ch) {
    int v = -1;
    wchar_t c = s_low((wchar_t)ch);
    if (c == L'g') v = SIV_GENERAL;
    else if (c == L'c') v = SIV_CPU;
    else if (c >= L'1' && c <= L'9' && c - L'1' < g_snap.ngpu) v = SIV_GPU + (c - L'1');
    if (v < 0) return 1;
    if (g_expanded) { g_view = v; ui_redraw(); }
    else { g_sel = v; ui_redraw(); }
    return 1;
}

void sysinfo_click(int x, int y, int dbl) {
    if (g_expanded) {
        for (int i = 0; i < g_v.ntabs; i++) {
            RECT r = g_v.tabs[i];
            if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) { g_view = i; ui_redraw(); return; }
        }
        return;
    }
    if (!g_rowStep || y < g_listTop) return;
    int i = (y - g_listTop) / g_rowStep;
    if (i >= nviews()) return;
    g_sel = i;
    ui_redraw();
    if (dbl) expand(i);
}

void sysinfo_wheel(int notches) {
    if (g_expanded) switch_view(-notches);
    else move(-notches);
}

// ---------------------------------------------------------------- the picker (normal size)
void sysinfo_paint(HDC dc, RECT a) {
    if (g_expanded) {
        g_v.view = g_view; g_v.snap = &g_snap;
        sysview_paint(dc, a, &g_v);
        return;
    }
    SelectObject(dc, g_font);
    ui_text(dc, a.left, a.top, L"System info", g_accent);
    int y = a.top + g_lineH + S(14);
    int rowH = g_lineH + S(10);
    g_rowStep = rowH + S(2);
    g_listTop = y;

    for (int i = 0; i < nviews(); i++) {
        int sel = i == g_sel;
        if (sel) ui_fill(dc, a.left, y, a.right, y + rowH, ui_mix(g_accent, C_BG, 13));
        int ty = y + (rowH - g_lineH) / 2;
        if (sel) ui_text(dc, a.left + S(10), ty, L"\x203A", g_accent);
        wchar_t label[16];
        const wchar_t *about;
        if (i == SIV_GENERAL)  { s_cpy(label, L"General", 16); about = L"CPU, GPU, memory, apps at a glance"; }
        else if (i == SIV_CPU) { s_cpy(label, L"CPU", 16);     about = L"per-thread load, speed, waiting"; }
        else { wsprintfW(label, L"GPU %d", i - SIV_GPU + 1); about = g_snap.gpu[i - SIV_GPU].name; }
        ui_text(dc, a.left + S(28), ty, label, sel ? C_BRIGHT : C_TEXT);
        RECT r = { a.left + S(160), ty, a.right - S(12), ty + g_lineH };
        SetTextColor(dc, C_DIM);
        DrawTextW(dc, about, -1, &r, DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        y += g_rowStep;
    }
    if (!g_snap.ngpu) ui_text(dc, a.left + S(28), y + S(4), L"(no GPU found: Windows isn't listing one)", C_DIM);

    SelectObject(dc, g_fontSmall);
    ui_text(dc, a.left, a.bottom - S(16), L"\x2191\x2193 choose    Enter open    Esc close", C_DIM);
    const wchar_t *hint = L"or: si general \x00B7 cpu \x00B7 gpu [n]";
    SIZE sz; GetTextExtentPoint32W(dc, hint, s_len(hint), &sz);
    ui_text(dc, a.right - sz.cx, a.bottom - S(16), hint, C_DIM);
    SelectObject(dc, g_font);
}
