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
// 2.24 (his ask): External packages (FEATURES_EXTERNAL) aren't in the exe: turning one on downloads it from its
// maker's GitHub releases (extpkg.c / extwin.c), turning it off deletes those files. In the list: an "External"
// column, Enter (or I) = what it does + its commands, Left / Right = on/off, E = which file of the release to
// download (external only).
// /packages works too.
#include "loomy.h"

static const wchar_t *ID[NFEATURES]    = { 0, L"web", L"lp", L"net", L"rb", L"display", L"sysinfo", L"github", L"phone", L"scrcpy" };
static const wchar_t *NAME[NFEATURES]  = { 0, L"Websites", L"Loomy's Path", L"Network", L"Recycle Bin",
                                           L"Display", L"System Info", L"GitHub", L"Remote", L"Android Screen Mirroring" };
// the I view: what it does, in a sentence or three (\n = new line)
static const wchar_t *DESC[NFEATURES] = { 0,
    L"Your favourite websites, opened by a short name. /webconfig adds and removes them; each mode has its own list.",
    L"Loomy's Path: your folders of shortcuts, browsed with the arrow keys.",
    L"Which apps are using the internet right now, and how much.",
    L"Opens the Recycle Bin.",
    L"Switches the screen layout like Win+P: extend, second screen only, PC screen only, duplicate. Also turns the screens off and on.",
    L"Live CPU, GPU, memory and temperatures, with a Flow score that says how smooth the PC feels.",
    L"Opens Loomy's GitHub page (core mode only).",
    L"Pocket Loomy, the Android app: pairing, Bluetooth and remote through Ellune. Opt-in: it listens for your phone.",
    L"Shows your Android phone's screen on the PC, and you control it with the mouse and keyboard. "
    L"It's scrcpy by Genymobile, downloaded from their GitHub releases when you turn it on.\n"
    L"The phone needs USB debugging on (Settings, About phone, tap Build number 7 times, then Developer options) "
    L"and to be plugged in by USB; it asks once to allow this PC.\n"
    L"From Pocket Loomy: \x201C" L"mirror my phone\x201D, \x201C" L"show my phone\x201D, \x201C" L"copy screen\x201D." };

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

// 2.24: external packages go on only once their files are in (extwin.c calls this when it's done)
void feature_set(int f, int on) { if (f > FEAT_CORE && f < NFEATURES) set_on(f, on); }
const wchar_t *feature_name(int f) { return f > FEAT_CORE && f < NFEATURES ? NAME[f] : L"Core"; }
int feature_external(int f) { return f > FEAT_CORE && f < NFEATURES && (FEATURES_EXTERNAL & (1u << f)); }

// turning a feature on or off, from the list or /pkg add|remove. External ones download first / delete after.
static void turn(int f, int on) {
    if (!feature_external(f)) { set_on(f, on); return; }
    if (on) {
        if (ext_busy(f)) { outf(OUT_DIM, L"%s is already downloading.", NAME[f]); return; }
        ext_install(f, 0);                 // turns it on itself when the files are in (WM_PKG)
        return;
    }
    set_on(f, 0);
    ext_remove(f);
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
    int add = s_ieq(w[0], L"add"), rem = s_ieq(w[0], L"remove"), upd = s_ieq(w[0], L"update");
    if ((!add && !rem && !upd) || n != 2) { cmd_usage(L"[add|remove|update <feature>]"); return; }

    int f = feature_find(w[1]);
    if (f < 0) {
        if (cmd_is_core(w[1])) { outf(OUT_ERR, L"%s is part of Loomy's core, so it's always on.", w[1]); return; }
        wchar_t all[160];
        list_ids(all, 160);
        outf(OUT_ERR, L"No feature called '%s'.", w[1]);
        outf(OUT_DIM, L"Features: %s", all);
        return;
    }
    if (upd) {                             // 2.24: an external package's newest release
        if (!feature_external(f)) { outf(OUT_DIM, L"%s is built into Loomy: /update updates it.", NAME[f]); return; }
        if (!feature_on(f)) { outf(OUT_DIM, L"%s is off. /pkg add %s downloads the newest.", NAME[f], ID[f]); return; }
        if (ext_busy(f)) { outf(OUT_DIM, L"%s is already downloading.", NAME[f]); return; }
        ext_install(f, 0);
        return;
    }
    if (add) {
        if (feature_on(f)) { outf(OUT_DIM, L"%s is already on.", NAME[f]); return; }
        if (feature_external(f)) { turn(f, 1); return; }
        set_on(f, 1);
        outf(OUT_OK, L"%s is on.", NAME[f]);
        if (f == FEAT_PHONE) out(OUT_DIM, L"Type /remote to pair your phone.");
    } else {
        if (!feature_on(f)) { outf(OUT_DIM, L"%s is already off.", NAME[f]); return; }
        turn(f, 0);
        if (FEATURES_OPTIN & (1u << f)) outf(OUT_OK, L"%s is off on this PC.", NAME[f]);
        else outf(OUT_OK, L"%s is off on this PC. /pkg add %s brings it back.", NAME[f], ID[f]);
        if (f == FEAT_WEB) out(OUT_DIM, L"Your saved sites are kept.");
        if (f == FEAT_PHONE) out(OUT_DIM, L"Bluetooth listening is off. Your paired phone is kept.");
        if (feature_external(f)) out(OUT_DIM, L"Its downloaded files are deleted.");
    }
}

void pkg_complete(wchar_t **words, int n, const wchar_t *partial) {
    if (n == 1) { complete_add(L"add", partial); complete_add(L"remove", partial); complete_add(L"update", partial); return; }
    if (n != 2) return;
    int add = s_ieq(words[1], L"add"), rem = s_ieq(words[1], L"remove"), upd = s_ieq(words[1], L"update");
    for (int f = FEAT_CORE + 1; f < NFEATURES; f++)
        if ((add && !feature_on(f)) || (rem && feature_on(f)) || (upd && feature_on(f) && feature_external(f))) complete_add(ID[f], partial);
}

// ---------------------------------------------------------------- the toggle list
// I = the info view for the row (what it does + its commands); E = an external package's file prompt.
#define NROWS (NFEATURES - 1)
static int g_active, g_sel, g_listTop, g_rowStep;
static int g_info;                     // 1 = the info view of g_sel's feature
static int g_edit;                     // 1 = typing which file to download
static wchar_t g_text[100];

int pkg_active(void) { return g_active; }

void pkg_open(void) {
    settings_close();
    g_sel = 0;
    g_info = g_edit = 0;
    g_active = 1;
    ui_set_input(L"");
    ui_redraw();
}

void pkg_close(void) {
    if (!g_active) return;
    g_active = 0;
    g_info = g_edit = 0;
    ui_redraw();
}

static void toggle(void) {
    int f = g_sel + 1;
    if (feature_external(f) && ext_busy(f)) return;
    turn(f, !feature_on(f));
    ui_redraw();
}

static void move(int d) {
    g_sel += d;
    if (g_sel < 0) g_sel = 0;
    if (g_sel > NROWS - 1) g_sel = NROWS - 1;
    ui_redraw();
}

static void edit_start(void) {
    int f = g_sel + 1;
    if (!feature_external(f)) return;
    if (!ext_get_file(f, g_text, 100)) g_text[0] = 0;
    g_edit = 1;
    g_info = 0;
    ui_redraw();
}

static void edit_done(int save) {
    int f = g_sel + 1;
    g_edit = 0;
    if (save) {
        wchar_t *t = g_text;
        while (*t == L' ') t++;
        ext_set_file(f, t);
        if (*t) outf(OUT_OK, L"%s: this PC downloads \x201C%s\x201D from now on.", NAME[f], t);
        else outf(OUT_OK, L"%s: Loomy picks the file by itself again.", NAME[f]);
        if (feature_on(f) && !ext_busy(f)) out(OUT_DIM, L"/pkg update gets it now.");
    }
    ui_redraw();
}

int pkg_key(WPARAM vk) {
    if (g_edit) {
        if (vk == VK_RETURN) edit_done(1);
        else if (vk == VK_ESCAPE) edit_done(0);
        else if (vk == VK_BACK) { int n = s_len(g_text); if (n) g_text[n - 1] = 0; ui_redraw(); }
        return 1;
    }
    if (g_info) {                       // Esc / Enter go back to the list (Esc doesn't close the list too)
        if (vk == VK_ESCAPE || vk == VK_RETURN || vk == VK_BACK) { g_info = 0; ui_redraw(); }
        else if (vk == VK_LEFT || vk == VK_RIGHT) toggle();
        else if (vk == VK_UP) { g_info = 0; move(-1); }
        else if (vk == VK_DOWN) { g_info = 0; move(+1); }
        return 1;
    }
    switch (vk) {
    case VK_UP:     move(-1); break;
    case VK_DOWN:   move(+1); break;
    case VK_HOME:   move(-NROWS); break;
    case VK_END:    move(+NROWS); break;
    // 2.23b (his call): Enter opens the info view, Left / Right turn it on and off
    case VK_RETURN: g_info = 1; ui_redraw(); break;
    case VK_SPACE: case VK_LEFT: case VK_RIGHT: toggle(); break;
    case VK_ESCAPE: pkg_close(); break;
    }
    return 1;
}

int pkg_char(WPARAM ch) {
    if (g_edit) {
        if (ch == 22) {                 // Ctrl+V: paste a file name
            wchar_t clip[100];
            if (ui_clipboard_text(clip, 100)) {
                for (int i = 0; clip[i]; i++) if (clip[i] == L'\r' || clip[i] == L'\n' || clip[i] == L'\t') clip[i] = L' ';
                s_cat(g_text, clip, 100);
                ui_redraw();
            }
            return 1;
        }
        if (ch >= 32 && ch != 127) {
            int n = s_len(g_text);
            if (n < 99) { g_text[n] = (wchar_t)ch; g_text[n + 1] = 0; ui_redraw(); }
        }
        return 1;
    }
    if (ch == L'i' || ch == L'I') { g_info = !g_info; ui_redraw(); }
    else if (ch == L'e' || ch == L'E') edit_start();
    return 1;
}

void pkg_click(int x, int y, int dbl) {
    if (g_info || g_edit || !g_rowStep || y < g_listTop) return;
    int i = (y - g_listTop) / g_rowStep;
    if (i >= NROWS) return;
    g_sel = i;
    toggle();                         // one click toggles, like a checkbox
}

void pkg_wheel(int notches) { if (!g_info && !g_edit) move(-notches); }

// an external package's state, for the right-hand column
static const wchar_t *state_of(int f, int *color) {
    int on = feature_on(f);
    *color = on ? C_OK : C_DIM;
    if (!feature_external(f)) return on ? L"on" : L"off";
    if (ext_busy(f)) {
        static wchar_t b[32];
        int p = ext_pct(f);
        *color = g_accent;
        if (p >= 0) { wsprintfW(b, L"downloading %d%%", p); return b; }
        return L"working...";
    }
    return on ? L"on" : L"off";
}

// one wrapped paragraph; returns the y under it
static int para(HDC dc, int x, int y, int right, const wchar_t *t, COLORREF c) {
    wchar_t line[200];
    while (*t) {
        int n = 0, cut = 0;
        for (;;) {
            if (!t[n] || t[n] == L'\n') { cut = n; break; }
            SIZE sz; GetTextExtentPoint32W(dc, t, n + 1, &sz);
            if (sz.cx > right - x) break;
            if (t[n] == L' ') cut = n;
            n++;
        }
        if (!cut) cut = n ? n : 1;
        int k = cut < 199 ? cut : 199;
        for (int i = 0; i < k; i++) line[i] = t[i];
        line[k] = 0;
        ui_text(dc, x, y, line, c);
        y += g_lineH;
        t += cut;
        while (*t == L' ' || *t == L'\n') { int nl = *t == L'\n'; t++; if (nl) break; }
    }
    return y;
}

static void paint_info(HDC dc, RECT a, int y) {
    int f = g_sel + 1;
    int tw = ui_text(dc, a.left, y, NAME[f], C_BRIGHT), col;
    const wchar_t *st = state_of(f, &col);
    tw += S(12) + ui_text(dc, a.left + tw + S(12), y, st, col);
    if (feature_external(f)) ui_text(dc, a.left + tw + S(12), y, L"External", g_accent);
    y += g_lineH + S(8);
    y = para(dc, a.left, y, a.right, DESC[f], C_TEXT) + S(10);
    const ExtPkg *p = ext_pkg(f);
    if (p) {
        wchar_t tag[48], file[100], b[260];
        if (ext_installed(f, tag, 48, file, 100)) wsprintfW(b, L"Installed: %s (%s), from %s", tag, file, p->site);
        else wsprintfW(b, L"Not downloaded yet. From %s", p->site);
        y = para(dc, a.left, y, a.right, b, C_DIM);
        if (ext_get_file(f, file, 100)) wsprintfW(b, L"File for this PC: %s (E to change)", file);
        else wsprintfW(b, L"File: picked automatically for this PC (%S). E to choose one.", ext_arch_name(ext_arch()));
        y = para(dc, a.left, y, a.right, b, C_DIM) + S(10);
    }
    ui_text(dc, a.left, y, L"Commands", g_accent);
    y += g_lineH + S(4);
    int any = 0;
    for (int i = 0;; i++) {
        const wchar_t *type, *alias, *about; int sys;
        if (!cmd_of_feature(f, i, &type, &alias, &about, &sys)) break;
        wchar_t w[64];
        s_cpy(w, sys ? L"/" : L"", 64); s_cat(w, type, 64);
        if (alias) { s_cat(w, sys ? L"  /" : L"  ", 64); s_cat(w, alias, 64); }
        ui_text(dc, a.left + S(12), y, w, C_BRIGHT);
        ui_text(dc, a.left + S(200), y, about, C_DIM);
        y += g_lineH;
        any = 1;
    }
    if (!any) ui_text(dc, a.left + S(12), y, L"(none of its own)", C_DIM);
}

static void paint_edit(HDC dc, RECT a, int y) {
    int f = g_sel + 1;
    wchar_t b[200];
    wsprintfW(b, L"Which file should this PC download for %s?", NAME[f]);
    y = para(dc, a.left, y, a.right, b, C_BRIGHT) + S(6);
    y = para(dc, a.left, y, a.right, L"Its name as on the release page (Ctrl+V pastes). The version number is ignored, so it still fits the next release. Empty = Loomy picks by itself.", C_DIM) + S(10);
    ui_sel_row(dc, a.left, y - S(4), a.right, y + g_lineH + S(4));
    wchar_t shown[110];
    s_cpy(shown, g_text, 104); s_cat(shown, L"_", 110);
    ui_text(dc, a.left + S(10), y, shown, C_BRIGHT);
    y += g_lineH + S(16);
    wchar_t ch[400];
    ext_choices(f, ch, 400);
    if (ch[0]) {
        ui_text(dc, a.left, y, L"On the latest release:", C_DIM);
        y += g_lineH;
        para(dc, a.left + S(12), y, a.right, ch, C_TEXT);
    }
}

void pkg_paint(HDC dc, RECT a) {
    SelectObject(dc, g_font);
    int y = a.top;
    int tw = ui_text(dc, a.left, y, L"Features", g_accent);
    ui_text(dc, a.left + tw + S(12), y, L"on this PC \x00B7 off ones don't load", C_DIM);
    y += g_lineH + S(14);

    if (g_info) paint_info(dc, a, y);
    else if (g_edit) paint_edit(dc, a, y);
    else {
        int rowH = g_lineH + S(10);
        g_rowStep = rowH + S(2);
        g_listTop = y;
        for (int i = 0; i < NROWS; i++) {
            int f = i + 1, sel = i == g_sel, on = feature_on(f), col;
            if (sel) ui_sel_row(dc, a.left, y, a.right, y + rowH);
            int ty = y + (rowH - g_lineH) / 2;
            if (sel) ui_text(dc, a.left + S(10), ty, L"\x203A", g_accent);
            ui_text(dc, a.left + S(28), ty, NAME[f], sel ? C_BRIGHT : on ? C_TEXT : C_DIM);
            if (feature_external(f)) ui_text(dc, a.left + S(300), ty, L"External", g_accent);   // 2.23b: its own column (clear of the longest name)
            const wchar_t *st = state_of(f, &col);
            SIZE sz; GetTextExtentPoint32W(dc, st, s_len(st), &sz);
            ui_text(dc, a.right - S(12) - sz.cx, ty, st, col);
            y += g_rowStep;
        }
        // a line about the selected external package (picking failed, an error...)
        int f = g_sel + 1;
        const wchar_t *note = feature_external(f) ? ext_status(f) : 0;
        if (note && note[0]) para(dc, a.left + S(28), y + S(6), a.right, note, g_accent);
    }

    SelectObject(dc, g_fontSmall);
    const wchar_t *keys = g_edit ? L"Enter save    Esc cancel    Ctrl+V paste"
                        : g_info ? L"\x2190\x2192 on/off    Esc back    \x2191\x2193 next feature"
                        : feature_external(g_sel + 1) ? L"\x2191\x2193 choose    \x2190\x2192 on/off    Enter info    E file    Esc close"
                        : L"\x2191\x2193 choose    \x2190\x2192 on/off    Enter info    Esc close";
    ui_text(dc, a.left, a.bottom - S(16), keys, C_DIM);
    const wchar_t *hint = L"core commands are always on";
    SIZE sz; GetTextExtentPoint32W(dc, hint, s_len(hint), &sz);
    if (!g_edit) ui_text(dc, a.right - sz.cx, a.bottom - S(16), hint, C_DIM);
    SelectObject(dc, g_font);
}
