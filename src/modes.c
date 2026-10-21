// modes.c - Loomy's modes (home / work). The list is hardcoded on purpose: no creating modes.
//   /mode  (or /m)                     the picker panel: Up/Down, Enter switches, Esc closes
//   /m switch|hide|show|main <mode>
//
// Saved in loomy.cfg: which mode is main (Loomy starts in it) and which ones are hidden.
// The current mode only lives in memory, so every fresh start (or /reload) begins in main.
#include "loomy.h"

static const wchar_t *NAMES[NMODES] = { L"home", L"work" };
static const wchar_t *ABOUT[NMODES] = { L"everyday, personal stuff", L"work tools and sites" };

static int g_cur = -1;

int mode_find(const wchar_t *name) {
    for (int i = 0; i < NMODES; i++) if (s_ieq(NAMES[i], name)) return i;
    return -1;
}

const wchar_t *mode_name(int m) { return (m >= 0 && m < NMODES) ? NAMES[m] : L"?"; }

int mode_is_hidden(int m) { return (g_cfg.modeHidden >> m) & 1; }

static int next_visible(int from) {
    for (int k = 1; k <= NMODES; k++) {
        int i = (from + k) % NMODES;
        if (!mode_is_hidden(i)) return i;
    }
    return from;
}

static int visible_count(void) {
    int n = 0;
    for (int i = 0; i < NMODES; i++) if (!mode_is_hidden(i)) n++;
    return n;
}

int mode_current(void) {
    if (g_cur < 0 || g_cur >= NMODES) g_cur = g_cfg.modeMain;
    if (mode_is_hidden(g_cur)) g_cur = next_visible(g_cur);
    return g_cur;
}

static void switch_to(int m) {
    g_cur = m;
    outf(OUT_OK, L"Switched to %s mode.", NAMES[m]);
    ui_redraw();                      // the header shows the mode
}

// ---------------------------------------------------------------- the /mode command
void cmd_mode(wchar_t *args) {
    wchar_t *w[3];
    int n = s_words(args, w, 3);
    if (n == 0) { mode_open(); return; }

    s_lower(w[0]);
    int sub = s_ieq(w[0], L"switch") ? 1 : s_ieq(w[0], L"hide") ? 2 :
              s_ieq(w[0], L"show")   ? 3 : s_ieq(w[0], L"main") ? 4 : 0;
    if (!sub) { cmd_usage(L"[switch|hide|show|main <mode>]"); return; }

    wchar_t shape[80];
    s_cpy(shape, w[0], 80);
    s_cat(shape, L" <", 80);
    for (int i = 0; i < NMODES; i++) { if (i) s_cat(shape, L"|", 80); s_cat(shape, NAMES[i], 80); }
    s_cat(shape, L">", 80);
    if (n != 2) { cmd_usage(shape); return; }

    int m = mode_find(w[1]);
    if (m < 0) { outf(OUT_ERR, L"No mode called '%s'.", w[1]); cmd_usage(shape); return; }
    const wchar_t *name = NAMES[m];
    const wchar_t *typed = cmd_typed();

    if (sub == 1) {                                   // switch
        if (mode_is_hidden(m)) { outf(OUT_ERR, L"%s is hidden. Show it first: %s show %s", name, typed, name); return; }
        if (m == mode_current()) { outf(OUT_DIM, L"Already in %s mode.", name); return; }
        switch_to(m);
    }
    else if (sub == 2) {                              // hide
        if (mode_is_hidden(m)) { outf(OUT_DIM, L"%s is already hidden.", name); return; }
        if (visible_count() <= 1) { outf(OUT_ERR, L"Can't hide %s, at least one mode has to stay visible.", name); return; }
        int wasCurrent = m == mode_current();
        g_cfg.modeHidden |= 1u << m;
        outf(OUT_OK, L"Hid %s.", name);
        if (g_cfg.modeMain == m) {
            g_cfg.modeMain = next_visible(m);
            outf(OUT_DIM, L"It was the main mode, so %s is main now.", NAMES[g_cfg.modeMain]);
        }
        cfg_save();
        if (wasCurrent) switch_to(next_visible(m));
    }
    else if (sub == 3) {                              // show
        if (!mode_is_hidden(m)) { outf(OUT_DIM, L"%s isn't hidden.", name); return; }
        g_cfg.modeHidden &= ~(1u << m);
        cfg_save();
        outf(OUT_OK, L"%s is visible again.", name);
    }
    else {                                            // main
        if (mode_is_hidden(m)) { outf(OUT_ERR, L"%s is hidden. Show it first: %s show %s", name, typed, name); return; }
        if (g_cfg.modeMain == m) { outf(OUT_DIM, L"%s is already the main mode.", name); return; }
        g_cfg.modeMain = m;
        cfg_save();
        outf(OUT_OK, L"%s is the main mode now. Loomy starts in it.", name);
    }
}

// ---------------------------------------------------------------- the picker panel
static int g_active, g_sel, g_list[NMODES], g_nlist, g_listTop, g_rowStep;

int mode_active(void) { return g_active; }

void mode_open(void) {
    settings_close();
    g_nlist = 0;
    for (int i = 0; i < NMODES; i++) if (!mode_is_hidden(i)) g_list[g_nlist++] = i;
    g_sel = 0;
    for (int i = 0; i < g_nlist; i++) if (g_list[i] == mode_current()) g_sel = i;
    g_active = 1;
    ui_set_input(L"");
    ui_redraw();
}

void mode_close(void) {
    if (!g_active) return;
    g_active = 0;
    ui_redraw();
}

static void pick(void) {
    if (!g_nlist) return;
    int m = g_list[g_sel];
    mode_close();
    if (m == mode_current()) outf(OUT_DIM, L"Already in %s mode.", NAMES[m]);
    else switch_to(m);
}

static void move(int d) {
    g_sel += d;
    if (g_sel < 0) g_sel = 0;
    if (g_sel > g_nlist - 1) g_sel = g_nlist - 1;
    ui_redraw();
}

int mode_key(WPARAM vk) {
    switch (vk) {
    case VK_UP:     move(-1); break;
    case VK_DOWN:   move(+1); break;
    case VK_HOME:   move(-NMODES); break;
    case VK_END:    move(+NMODES); break;
    case VK_RETURN: pick(); break;
    case VK_ESCAPE: mode_close(); break;
    }
    return 1;
}

int mode_char(WPARAM ch) {
    for (int k = 1; ch > 32 && k <= g_nlist; k++) {     // type a letter to jump to it
        int i = (g_sel + k) % g_nlist;
        if (s_low(NAMES[g_list[i]][0]) == s_low((wchar_t)ch)) { g_sel = i; ui_redraw(); break; }
    }
    return 1;
}

void mode_click(int x, int y, int dbl) {
    if (!g_rowStep || y < g_listTop) return;
    int i = (y - g_listTop) / g_rowStep;
    if (i >= g_nlist) return;
    g_sel = i;
    ui_redraw();
    if (dbl) pick();
}

void mode_wheel(int notches) { move(-notches); }

void mode_paint(HDC dc, RECT a) {
    SelectObject(dc, g_font);
    int y = a.top;
    ui_text(dc, a.left, y, L"Modes", g_accent);
    y += g_lineH + S(14);

    int rowH = g_lineH + S(10);
    g_rowStep = rowH + S(2);
    g_listTop = y;

    for (int i = 0; i < g_nlist; i++) {
        int m = g_list[i], sel = i == g_sel;
        if (sel) ui_fill(dc, a.left, y, a.right, y + rowH, ui_mix(g_accent, C_BG, 13));
        int ty = y + (rowH - g_lineH) / 2;
        if (sel) ui_text(dc, a.left + S(10), ty, L"\x203A", g_accent);
        ui_text(dc, a.left + S(28), ty, NAMES[m], sel ? C_BRIGHT : C_TEXT);
        ui_text(dc, a.left + S(120), ty, ABOUT[m], C_DIM);

        // tags on the right: "main", "current"
        wchar_t tag[32] = L"";
        if (m == g_cfg.modeMain) s_cat(tag, L"main", 32);
        if (m == mode_current()) { if (tag[0]) s_cat(tag, L" \x00B7 ", 32); s_cat(tag, L"current", 32); }
        if (tag[0]) {
            SIZE sz; GetTextExtentPoint32W(dc, tag, s_len(tag), &sz);
            ui_text(dc, a.right - S(12) - sz.cx, ty, tag, m == mode_current() ? g_accent : C_DIM);
        }
        y += g_rowStep;
    }

    SelectObject(dc, g_fontSmall);
    ui_text(dc, a.left, a.bottom - S(16), L"\x2191\x2193 choose    Enter switch    Esc close", C_DIM);
    int hidden = NMODES - g_nlist;
    if (hidden) {
        wchar_t note[64];
        wsprintfW(note, L"%d hidden \x00B7 /m show <mode>", hidden);
        SIZE sz; GetTextExtentPoint32W(dc, note, s_len(note), &sz);
        ui_text(dc, a.right - sz.cx, a.bottom - S(16), note, C_DIM);
    }
    SelectObject(dc, g_font);
}
