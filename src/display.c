// display.c - the `display` command: switch how your screens are used, like Win+P.
//   display                      picker panel: Up/Down, Enter switches, Esc closes
//   display extend|second|pc|duplicate|off
//
// Uses the same Windows call Win+P does (SetDisplayConfig with a topology), so no admin,
// and Windows remembers each layout's resolution/arrangement on its own.
// "off" isn't a layout: it puts the screens to standby (like their power button), so the
// layout never changes and any key or mouse move wakes them in the same style.
#include "loomy.h"

#define NLAYOUTS 4          // real Win+P layouts
#define ROW_OFF  4          // the extra "Screens off" row
#define NROWS    5

// list order is on purpose: the two you use most come first
static const wchar_t *WORD_[NROWS]  = { L"extend", L"second", L"pc", L"duplicate", L"off" };
static const wchar_t *LABEL[NROWS]  = { L"Extend", L"Second screen only", L"PC screen only", L"Duplicate", L"Screens off" };
static const wchar_t *ABOUT[NROWS]  = { L"both screens, one big desktop", L"just the extra screen",
                                        L"just the main screen", L"same picture on both",
                                        L"any key or mouse wakes them" };
static const UINT32   TOPO[NLAYOUTS]   = { SDC_TOPOLOGY_EXTEND, SDC_TOPOLOGY_EXTERNAL,
                                           SDC_TOPOLOGY_INTERNAL, SDC_TOPOLOGY_CLONE };

static int find_layout(const wchar_t *w) {
    for (int i = 0; i < NROWS; i++) if (s_ieq(WORD_[i], w)) return i;
    return -1;
}

// which layout Windows is on right now, or -1 if it won't say
static int current_layout(void) {
    UINT32 np = 0, nm = 0;
    if (GetDisplayConfigBufferSizes(QDC_DATABASE_CURRENT, &np, &nm) != ERROR_SUCCESS) return -1;
    HANDLE heap = GetProcessHeap();
    DISPLAYCONFIG_PATH_INFO *paths = HeapAlloc(heap, 0, (np ? np : 1) * sizeof(DISPLAYCONFIG_PATH_INFO));
    DISPLAYCONFIG_MODE_INFO *modes = HeapAlloc(heap, 0, (nm ? nm : 1) * sizeof(DISPLAYCONFIG_MODE_INFO));
    int found = -1;
    if (paths && modes) {
        DISPLAYCONFIG_TOPOLOGY_ID topo = 0;
        if (QueryDisplayConfig(QDC_DATABASE_CURRENT, &np, paths, &nm, modes, &topo) == ERROR_SUCCESS)
            for (int i = 0; i < NLAYOUTS; i++) if ((UINT32)topo == TOPO[i]) found = i;
    }
    if (paths) HeapFree(heap, 0, paths);
    if (modes) HeapFree(heap, 0, modes);
    return found;
}

// returns 1 if Windows switched
static int apply(int i) {
    if (i == ROW_OFF) {
        out(OUT_OK, L"Screens off.");
        ui_hide();
        ui_screens_off_soon();   // after a beat, so letting go of Enter doesn't wake them
        return 1;
    }
    if (i == current_layout()) { outf(OUT_DIM, L"Already on %s.", LABEL[i]); return 0; }
    LONG rc = SetDisplayConfig(0, 0, 0, 0, SDC_APPLY | TOPO[i]);
    if (rc != ERROR_SUCCESS) {
        outf(OUT_ERR, L"Windows couldn't switch to %s.", LABEL[i]);
        if (i != 2) out(OUT_DIM, L"Is the second screen plugged in and on?");
        return 0;
    }
    outf(OUT_OK, L"Switched to %s.", LABEL[i]);
    ui_hide();             // screens just moved around: get out of the way
    return 1;
}

// ---------------------------------------------------------------- the command
void cmd_display(wchar_t *args) {
    wchar_t *w[2];
    int n = s_words(args, w, 2);
    if (n == 0) { display_open(); return; }
    int i = find_layout(w[0]);
    if (i < 0) {
        outf(OUT_ERR, L"No display layout called '%s'.", w[0]);
        cmd_usage(L"[extend|second|pc|duplicate|off]");
        return;
    }
    apply(i);
}

void display_complete(const wchar_t *partial) {
    for (int i = 0; i < NROWS; i++) complete_add(WORD_[i], partial);
}

// ---------------------------------------------------------------- the picker panel
static int g_active, g_sel, g_cur = -1, g_listTop, g_rowStep;

int display_active(void) { return g_active; }

void display_open(void) {
    settings_close();
    g_cur = current_layout();
    g_sel = 0;                      // Extend on top, ready for Enter
    g_active = 1;
    ui_set_input(L"");
    ui_redraw();
}

void display_close(void) {
    if (!g_active) return;
    g_active = 0;
    ui_redraw();
}

static void pick(void) {
    int i = g_sel;
    display_close();
    apply(i);
}

static void move(int d) {
    g_sel += d;
    if (g_sel < 0) g_sel = 0;
    if (g_sel > NROWS - 1) g_sel = NROWS - 1;
    ui_redraw();
}

int display_key(WPARAM vk) {
    switch (vk) {
    case VK_UP:     move(-1); break;
    case VK_DOWN:   move(+1); break;
    case VK_HOME:   move(-NROWS); break;
    case VK_END:    move(+NROWS); break;
    case VK_RETURN: pick(); break;
    case VK_ESCAPE: display_close(); break;
    }
    return 1;
}

int display_char(WPARAM ch) {      // e / s / p / d / o jump to a row
    for (int i = 0; ch > 32 && i < NROWS; i++)
        if (WORD_[i][0] == s_low((wchar_t)ch)) { g_sel = i; ui_redraw(); break; }
    return 1;
}

void display_click(int x, int y, int dbl) {
    if (!g_rowStep || y < g_listTop) return;
    int i = (y - g_listTop) / g_rowStep;
    if (i >= NROWS) return;
    g_sel = i;
    ui_redraw();
    if (dbl) pick();
}

void display_wheel(int notches) { move(-notches); }

void display_paint(HDC dc, RECT a) {
    SelectObject(dc, g_font);
    int y = a.top;
    ui_text(dc, a.left, y, L"Display", g_accent);
    y += g_lineH + S(14);

    int rowH = g_lineH + S(10);
    g_rowStep = rowH + S(2);
    g_listTop = y;

    for (int i = 0; i < NROWS; i++) {
        int sel = i == g_sel;
        if (sel) ui_sel_row(dc, a.left, y, a.right, y + rowH);
        int ty = y + (rowH - g_lineH) / 2;
        if (sel) ui_text(dc, a.left + S(10), ty, L"\x203A", g_accent);
        ui_text(dc, a.left + S(28), ty, LABEL[i], sel ? C_BRIGHT : C_TEXT);
        ui_text(dc, a.left + S(220), ty, ABOUT[i], C_DIM);
        if (i == g_cur) {
            SIZE sz; GetTextExtentPoint32W(dc, L"current", 7, &sz);
            ui_text(dc, a.right - S(12) - sz.cx, ty, L"current", g_accent);
        }
        y += g_rowStep;
    }

    SelectObject(dc, g_fontSmall);
    ui_text(dc, a.left, a.bottom - S(16), L"\x2191\x2193 choose    Enter switch    Esc close", C_DIM);
    const wchar_t *hint = L"or: dp extend \x00B7 second \x00B7 pc \x00B7 duplicate \x00B7 off";
    SIZE sz; GetTextExtentPoint32W(dc, hint, s_len(hint), &sz);
    ui_text(dc, a.right - sz.cx, a.bottom - S(16), hint, C_DIM);
    SelectObject(dc, g_font);
}
