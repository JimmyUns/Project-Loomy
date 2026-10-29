// anim.c - Loomy's small animations, all optional (/config > Customization):
//   dot wave   a slow wave drifts through the halftone strip (the only one that keeps going)
//   line glow  a glow sweeps along the purple lines once when Loomy opens
//   list glide the selection highlight slides between rows instead of jumping
//   text fade  new output lines fade in
//   fade in    the window fades in on open (off by default)
//
// Cost: nothing while Loomy is hidden (no timer). While it's open, the dot wave repaints only
// its thin strip ~30 times a second; the rest are short one-shots (60 fps, then stop).
// "auto" skips everything if the PC looks busy when Loomy opens: RAM >= 90 %, battery saver,
// or CPU >= 80 % measured over the first ~50 ms (Loomy opens plain and upgrades after that).
#include "loomy.h"

#define FAST_MS    16      // one-shot effects
#define SLOW_MS    33      // dot wave only
#define CHECK_MS   50      // CPU sample window for "auto"
#define GLOW_MS    750
#define GLIDE_MS   110
#define TEXT_MS    180
#define BUSY_CPU   80
#define BUSY_RAM   90

static HWND      g_w;
static int       g_on;                 // effects allowed this time Loomy is open
static int       g_pending;            // "auto": waiting on the CPU sample
static int       g_timer;              // current timer interval, 0 = stopped
static int       g_layered;            // window is mid fade-in
static ULONGLONG g_shown, g_glow0, g_fade0, g_text0;
static ULONGLONG g_idle0, g_total0;

static ULONGLONG now_ms(void) { return GetTickCount64(); }

// 0..1024, eased out (fast start, soft landing); 1024 once dur has passed
static int ease(ULONGLONG start, int dur) {
    ULONGLONG t = now_ms() - start;
    if (t >= (ULONGLONG)dur) return 1024;
    int i = 1024 - (int)(t * 1024 / dur);
    return 1024 - (int)((long long)i * i / 1024 * i / 1024);
}

// 0..1024, slow start and slow end (smoothstep): reads as a fade, not a blink
static int smooth(ULONGLONG start, int dur) {
    ULONGLONG t = now_ms() - start;
    if (t >= (ULONGLONG)dur) return 1024;
    int x = (int)(t * 1024 / dur);
    return (int)((long long)x * x / 1024 * (3072 - 2 * x) / 1024);
}

static ULONGLONG ft(FILETIME f) { return ((ULONGLONG)f.dwHighDateTime << 32) | f.dwLowDateTime; }
static void cpu_snap(ULONGLONG *idle, ULONGLONG *total) {
    FILETIME i, k, u;
    GetSystemTimes(&i, &k, &u);
    *idle = ft(i); *total = ft(k) + ft(u);      // kernel time includes idle time
}

static int pc_strained(void) {                  // instant checks
    MEMORYSTATUSEX m = { sizeof(m) };
    if (GlobalMemoryStatusEx(&m) && m.dwMemoryLoad >= BUSY_RAM) return 1;
    SYSTEM_POWER_STATUS p;
    if (GetSystemPowerStatus(&p) && p.SystemStatusFlag == 1) return 1;   // battery saver on
    return 0;
}

static void set_timer(int ms) {
    if (ms == g_timer) return;
    if (g_timer) KillTimer(g_w, TIMER_ANIM);
    g_timer = ms;
    if (ms) SetTimer(g_w, TIMER_ANIM, ms, 0);
}

void anim_kick(void) { if (g_w && (g_on || g_pending || g_layered)) set_timer(FAST_MS); }

static void end_fade(void) {
    if (!g_layered) return;
    g_layered = 0;
    SetWindowLongW(g_w, GWL_EXSTYLE, GetWindowLongW(g_w, GWL_EXSTYLE) & ~WS_EX_LAYERED);
}

static void start_effects(void) {
    g_on = 1;
    if (g_cfg.animLines) g_glow0 = now_ms();
    anim_kick();
    InvalidateRect(g_w, 0, FALSE);
}

// ---------------------------------------------------------------- open / close
// Called just before Loomy's window is shown.
void anim_show(HWND w) {
    g_w = w;
    g_on = g_pending = 0;
    g_glow0 = g_text0 = 0;
    g_shown = now_ms();
    if (g_cfg.anim == ANIM_OFF) return;
    int strained = g_cfg.anim == ANIM_AUTO && pc_strained();
    if (strained) return;

    if (g_cfg.fadeIn) {                         // start see-through, ramp up in anim_tick
        SetWindowLongW(w, GWL_EXSTYLE, GetWindowLongW(w, GWL_EXSTYLE) | WS_EX_LAYERED);
        SetLayeredWindowAttributes(w, 0, 0, LWA_ALPHA);
        g_layered = 1;
        g_fade0 = 0;                            // the clock starts on the first tick, after the first paint
    }
    if (g_cfg.anim == ANIM_ON) start_effects();
    else { g_pending = 1; cpu_snap(&g_idle0, &g_total0); }
    set_timer(FAST_MS);
}

void anim_hide(void) {
    set_timer(0);
    end_fade();
    g_on = g_pending = 0;
    g_glow0 = 0;
    ui_glow_border(0);
}

// ---------------------------------------------------------------- frames
int anim_dots_on(void) { return g_on && g_cfg.animDots; }

// how far the dot wave's crest is from x: 0..1024 (1024 = right on it), or -1 when off
int anim_wave(int x, int wavelen) {
    if (!anim_dots_on() || wavelen <= 0) return -1;
    int phase = (int)(((ULONGLONG)x * 1024 / wavelen + 2048 * 64 - (now_ms() * 1024 / 4200)) % 1024);
    int tri = phase < 512 ? phase * 2 : (1024 - phase) * 2;          // 0..1024..0
    int s = (int)((long long)tri * tri / 1024 * (3072 - 2 * tri) / 1024);   // smoothstep
    return s * s / 1024;                                               // narrower crest
}

// line glow progress 0..1024, or -1 when not glowing
int anim_glow(void) {
    if (!g_on || !g_cfg.animLines || !g_glow0) return -1;
    ULONGLONG t = now_ms() - g_glow0;
    if (t >= GLOW_MS) return -1;
    return (int)(t * 1024 / GLOW_MS);
}

// fade a fresh output line in from the background
COLORREF anim_text(COLORREF c, ULONGLONG born) {
    if (!g_on || !g_cfg.animText || !born) return c;
    ULONGLONG age = now_ms() - born;
    if (born < g_shown || age >= TEXT_MS) return c;     // lines from before Loomy opened just show
    if (!g_text0 || now_ms() - g_text0 > TEXT_MS) { g_text0 = now_ms(); anim_kick(); }
    return ui_mix(c, C_BG, ease(born, TEXT_MS) * 100 / 1024);
}

void anim_tick(void) {
    ULONGLONG t = now_ms();
    if (g_pending && t - g_shown >= CHECK_MS) {
        g_pending = 0;
        ULONGLONG idle, total;
        cpu_snap(&idle, &total);
        ULONGLONG dt = total - g_total0, di = idle - g_idle0;
        int cpu = dt ? (int)((dt - di) * 100 / dt) : 0;
        if (cpu < BUSY_CPU) start_effects();
    }
    int busy = g_pending;

    if (g_layered) {
        if (!g_fade0) g_fade0 = t;              // timers come after WM_PAINT, so the window has its content now
        int a = smooth(g_fade0, g_cfg.fadeMs);    // length set in /config (Fade speed)
        SetLayeredWindowAttributes(g_w, 0, (BYTE)(a * 255 / 1024), LWA_ALPHA);
        if (a >= 1024) end_fade(); else busy = 1;
    }

    int g = anim_glow();
    if (g >= 0) {
        int k = g < 300 ? g * 1024 / 300 : (1024 - g) * 1024 / 724;       // quick rise, slow settle
        ui_glow_border(k);
    } else if (g_glow0) { g_glow0 = 0; ui_glow_border(0); InvalidateRect(g_w, 0, FALSE); }

    int paint = g >= 0 || anim_glide_active() || (g_text0 && t - g_text0 <= TEXT_MS + FAST_MS);
    if (paint) InvalidateRect(g_w, 0, FALSE);
    if (paint || busy) set_timer(FAST_MS);
    else if (anim_dots_on()) { ui_invalidate_strip(); set_timer(SLOW_MS); }
    else set_timer(0);
}

// /config changed something while Loomy is open
void anim_config_changed(void) {
    if (!g_w) return;
    if (g_cfg.anim == ANIM_OFF) { g_on = g_pending = 0; g_glow0 = 0; ui_glow_border(0); set_timer(0); }
    else if (!g_on && !g_pending) { g_on = 1; set_timer(FAST_MS); }
    else set_timer(FAST_MS);
    InvalidateRect(g_w, 0, FALSE);
}

// ---------------------------------------------------------------- list glide
// Panels call ui_sel_row() for their selected row instead of filling it themselves.
// ui.c calls anim_paint_begin() before the panel draws, which paints the highlight at its
// gliding position underneath the rows; the panel then only records where it should be.
static int g_selValid, g_selScreen, g_curScreen, g_selSeen, g_drewPre;
static int g_selFrom, g_selTo, g_selL, g_selR, g_selH;
static ULONGLONG g_sel0;

static int glide_on(void) { return g_on && g_cfg.animSelect; }
static int sel_y(void) { return g_selFrom + (g_selTo - g_selFrom) * ease(g_sel0, GLIDE_MS) / 1024; }
int anim_glide_active(void) { return glide_on() && g_selValid && now_ms() - g_sel0 < GLIDE_MS; }

void anim_paint_begin(HDC dc, int screen) {
    g_curScreen = screen;
    g_selSeen = g_drewPre = 0;
    if (glide_on() && g_selValid && screen == g_selScreen) {
        int y = sel_y();
        ui_fill(dc, g_selL, y, g_selR, y + g_selH, ui_mix(g_accent, C_BG, 13));
        g_drewPre = 1;
    }
}

void anim_paint_end(void) { if (!g_selSeen) g_selValid = 0; }

void ui_sel_row(HDC dc, int l, int t, int r, int b) {
    COLORREF c = ui_mix(g_accent, C_BG, 13);
    g_selSeen = 1;
    if (!glide_on()) { ui_fill(dc, l, t, r, b, c); g_selValid = 0; return; }
    if (!g_selValid || g_selScreen != g_curScreen || b - t != g_selH) {
        g_selValid = 1; g_selScreen = g_curScreen;
        g_selFrom = g_selTo = t; g_sel0 = 0;
    } else if (t != g_selTo) {                  // moved: glide from wherever it is now
        g_selFrom = sel_y(); g_selTo = t; g_sel0 = now_ms();
        anim_kick();
    }
    g_selL = l; g_selR = r; g_selH = b - t;
    if (!g_drewPre) ui_fill(dc, l, t, r, b, c);
}
