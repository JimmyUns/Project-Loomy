// features.c - optional features you can turn on/off per PC (/pkg), like VS Code extensions
// but built in: nothing is downloaded. An off feature's commands act like they don't exist
// (unknown, not in help, no Tab), and its code never runs, so it costs ~nothing.
//   /pkg                       toggle list: Up/Down, Enter/Space/Left/Right toggles, Esc closes
//   /pkg add <feature>         turn one on
//   /pkg remove <feature>      turn one off
// Saved in loomy.cfg as the OFF list (features_off = ...), so new features arrive switched on.
// Opt-in features (phone: it listens for Bluetooth connections) are the other way round: off
// until you add them, saved as the ON list (features_on = phone).
// The core (help, modes, /config, /update..., /pkg, hide...) can't be turned off.
#include "loomy.h"

static const wchar_t *ID[NFEATURES]    = { 0, L"web", L"lp", L"net", L"rb", L"display", L"sysinfo", L"github", L"phone" };
static const wchar_t *NAME[NFEATURES]  = { 0, L"Websites", L"Loomy's Path", L"Network", L"Recycle Bin",
                                           L"Display", L"System Info", L"GitHub", L"Phone" };
static const wchar_t *ABOUT[NFEATURES] = { 0, L"web + /webconfig, saved sites", L"lp, browse your folders",
                                           L"net, what's using the internet", L"rb, open the recycle bin",
                                           L"display / dp, screen layouts", L"sysinfo / si, live CPU and GPU",
                                           L"github / gh (core mode)", L"/phone, Pocket Loomy remote" };

int feature_on(int f) {
    if (f <= FEAT_CORE || f >= NFEATURES) return 1;
    if (FEATURES_OPTIN & (1u << f)) return (g_cfg.featuresOn & (1u << f)) != 0;
    return !(g_cfg.featuresOff & (1u << f));
}
const wchar_t *feature_id(int f) { return f > FEAT_CORE && f < NFEATURES ? ID[f] : L"core"; }

int feature_find(const wchar_t *word) {
    for (int f = FEAT_CORE + 1; f < NFEATURES; f++) if (s_ieq(ID[f], word)) return f;
    return -1;
}

int features_off_count(void) {           // opt-in features being off is normal, so they don't count
    int n = 0;
    for (int f = FEAT_CORE + 1; f < NFEATURES; f++) if (!feature_on(f) && !(FEATURES_OPTIN & (1u << f))) n++;
    return n;
}

static void set_on(int f, int on) {
    if (FEATURES_OPTIN & (1u << f)) {
        if (on) g_cfg.featuresOn |= 1u << f;
        else    g_cfg.featuresOn &= ~(1u << f);
    } else {
        if (on) g_cfg.featuresOff &= ~(1u << f);
        else    g_cfg.featuresOff |= 1u << f;
    }
    cfg_save();
    if (f == FEAT_PHONE) phone_feature_changed();
}

// ---------------------------------------------------------------- the command
static void list_ids(wchar_t *buf, int cap) {
    buf[0] = 0;
    for (int f = FEAT_CORE + 1; f < NFEATURES; f++) {
        if (buf[0]) s_cat(buf, L", ", cap);
        s_cat(buf, ID[f], cap);
    }
}

void cmd_pkg(wchar_t *args) {
    wchar_t *w[3];
    int n = s_words(args, w, 3);
    if (n == 0) { pkg_open(); return; }
    int add = s_ieq(w[0], L"add"), rem = s_ieq(w[0], L"remove");
    if ((!add && !rem) || n != 2) { cmd_usage(L"[add|remove <feature>]"); return; }

    int f = feature_find(w[1]);
    if (f < 0) {
        if (cmd_is_core(w[1])) { outf(OUT_ERR, L"%s is part of Loomy's core, so it's always on.", w[1]); return; }
        wchar_t all[160];
        list_ids(all, 160);
        outf(OUT_ERR, L"No feature called '%s'.", w[1]);
        outf(OUT_DIM, L"Features: %s", all);
        return;
    }
    if (add) {
        if (feature_on(f)) { outf(OUT_DIM, L"%s is already on.", NAME[f]); return; }
        set_on(f, 1);
        outf(OUT_OK, L"%s is on.", NAME[f]);
        if (f == FEAT_PHONE) out(OUT_DIM, L"Type /phone to pair your phone.");
    } else {
        if (!feature_on(f)) { outf(OUT_DIM, L"%s is already off.", NAME[f]); return; }
        set_on(f, 0);
        if (FEATURES_OPTIN & (1u << f)) outf(OUT_OK, L"%s is off on this PC.", NAME[f]);
        else outf(OUT_OK, L"%s is off on this PC. /pkg add %s brings it back.", NAME[f], ID[f]);
        if (f == FEAT_WEB) out(OUT_DIM, L"Your saved sites are kept.");
        if (f == FEAT_PHONE) out(OUT_DIM, L"Bluetooth listening is off. Your paired phone is kept.");
    }
}

void pkg_complete(wchar_t **words, int n, const wchar_t *partial) {
    if (n == 1) { complete_add(L"add", partial); complete_add(L"remove", partial); return; }
    if (n != 2) return;
    int add = s_ieq(words[1], L"add"), rem = s_ieq(words[1], L"remove");
    for (int f = FEAT_CORE + 1; f < NFEATURES; f++)
        if ((add && !feature_on(f)) || (rem && feature_on(f))) complete_add(ID[f], partial);
}

// ---------------------------------------------------------------- the toggle list
#define NROWS (NFEATURES - 1)
static int g_active, g_sel, g_listTop, g_rowStep;

int pkg_active(void) { return g_active; }

void pkg_open(void) {
    settings_close();
    g_sel = 0;
    g_active = 1;
    ui_set_input(L"");
    ui_redraw();
}

void pkg_close(void) {
    if (!g_active) return;
    g_active = 0;
    ui_redraw();
}

static void toggle(void) {
    int f = g_sel + 1;
    set_on(f, !feature_on(f));
    ui_redraw();
}

static void move(int d) {
    g_sel += d;
    if (g_sel < 0) g_sel = 0;
    if (g_sel > NROWS - 1) g_sel = NROWS - 1;
    ui_redraw();
}

int pkg_key(WPARAM vk) {
    switch (vk) {
    case VK_UP:     move(-1); break;
    case VK_DOWN:   move(+1); break;
    case VK_HOME:   move(-NROWS); break;
    case VK_END:    move(+NROWS); break;
    case VK_RETURN: case VK_SPACE: case VK_LEFT: case VK_RIGHT: toggle(); break;
    case VK_ESCAPE: pkg_close(); break;
    }
    return 1;
}

int pkg_char(WPARAM ch) { return 1; }

void pkg_click(int x, int y, int dbl) {
    if (!g_rowStep || y < g_listTop) return;
    int i = (y - g_listTop) / g_rowStep;
    if (i >= NROWS) return;
    g_sel = i;
    toggle();                         // one click toggles, like a checkbox
}

void pkg_wheel(int notches) { move(-notches); }

void pkg_paint(HDC dc, RECT a) {
    SelectObject(dc, g_font);
    int y = a.top;
    int tw = ui_text(dc, a.left, y, L"Features", g_accent);
    ui_text(dc, a.left + tw + S(12), y, L"on this PC \x00B7 off ones don't load", C_DIM);
    y += g_lineH + S(14);

    int rowH = g_lineH + S(10);
    g_rowStep = rowH + S(2);
    g_listTop = y;
    for (int i = 0; i < NROWS; i++) {
        int f = i + 1, sel = i == g_sel, on = feature_on(f);
        if (sel) ui_sel_row(dc, a.left, y, a.right, y + rowH);
        int ty = y + (rowH - g_lineH) / 2;
        if (sel) ui_text(dc, a.left + S(10), ty, L"\x203A", g_accent);
        ui_text(dc, a.left + S(28), ty, NAME[f], sel ? C_BRIGHT : on ? C_TEXT : C_DIM);
        ui_text(dc, a.left + S(200), ty, ABOUT[f], C_DIM);
        const wchar_t *st = on ? L"on" : L"off";
        SIZE sz; GetTextExtentPoint32W(dc, st, s_len(st), &sz);
        ui_text(dc, a.right - S(12) - sz.cx, ty, st, on ? C_OK : C_DIM);
        y += g_rowStep;
    }

    SelectObject(dc, g_fontSmall);
    ui_text(dc, a.left, a.bottom - S(16), L"\x2191\x2193 choose    Enter toggle    Esc close", C_DIM);
    const wchar_t *hint = L"core commands are always on";
    SIZE sz; GetTextExtentPoint32W(dc, hint, s_len(hint), &sz);
    ui_text(dc, a.right - sz.cx, a.bottom - S(16), hint, C_DIM);
    SelectObject(dc, g_font);
}
