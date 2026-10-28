// settings.c - the config screen (type: config)
// Up/Down picks a row, Enter changes it, Esc goes back. Everything saves instantly.
#include "loomy.h"

enum { ROW_HOTKEY, ROW_NAME, ROW_ACCENT, ROW_STARTUP, ROW_POSITION, NROWS };
enum { EDIT_NONE, EDIT_NAME, EDIT_ACCENT };

static int  g_active, g_sel, g_editing, g_capturing;
static RECT g_rows[NROWS];
static wchar_t g_msg[160];
static int  g_msgKind;

static const COLORREF PRESETS[] = {
    RGB(0xA8, 0x55, 0xF7), RGB(0x81, 0x8C, 0xF8), RGB(0x60, 0xA5, 0xFA), RGB(0x22, 0xD3, 0xEE),
    RGB(0x34, 0xD3, 0x99), RGB(0xF4, 0x72, 0xB6), RGB(0xFB, 0x92, 0x3C), RGB(0xF8, 0x71, 0x71),
};
#define NPRESETS (int)(sizeof(PRESETS) / sizeof(PRESETS[0]))

// ---------------------------------------------------------------- start with Windows
#define RUN_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"

int startup_get(void) {
    HKEY k; int on = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_READ, &k) == ERROR_SUCCESS) {
        on = RegQueryValueExW(k, L"Loomy", 0, 0, 0, 0) == ERROR_SUCCESS;
        RegCloseKey(k);
    }
    return on;
}

void startup_set(int on) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    if (on) {
        wchar_t exe[MAX_PATH], val[MAX_PATH + 16] = L"\"";
        GetModuleFileNameW(0, exe, MAX_PATH);
        s_cat(val, exe, MAX_PATH + 16);
        s_cat(val, L"\" --silent", MAX_PATH + 16);
        RegSetValueExW(k, L"Loomy", 0, REG_SZ, (BYTE *)val, (s_len(val) + 1) * sizeof(wchar_t));
    } else {
        RegDeleteValueW(k, L"Loomy");
    }
    RegCloseKey(k);
}

// ---------------------------------------------------------------- helpers
static void msg(int kind, const wchar_t *t) { g_msgKind = kind; s_cpy(g_msg, t, 160); }

static void hex_of(COLORREF c, wchar_t *buf) {
    wsprintfW(buf, L"#%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
}

static void set_accent(COLORREF c) {
    g_cfg.accent = c;
    cfg_save();
    ui_apply_colors();
}

static void stop_capture(void) {
    if (!g_capturing) return;
    g_capturing = 0;
    ui_register_hotkey();          // put the current hotkey back
}

// turn a key press into text like "ctrl+alt+k"
static int describe_key(WPARAM vk, wchar_t *buf) {
    buf[0] = 0;
    int ctrl = GetKeyState(VK_CONTROL) < 0, alt = GetKeyState(VK_MENU) < 0;
    int shift = GetKeyState(VK_SHIFT) < 0, win = GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0;

    wchar_t key[16] = L"";
    if (vk >= 'A' && vk <= 'Z') { key[0] = (wchar_t)(vk + 32); key[1] = 0; }
    else if (vk >= '0' && vk <= '9') { key[0] = (wchar_t)vk; key[1] = 0; }
    else if (vk == VK_SPACE) s_cpy(key, L"space", 16);
    else if (vk == VK_OEM_3) s_cpy(key, L"`", 16);
    else if (vk >= VK_F1 && vk <= VK_F24) wsprintfW(key, L"f%d", (int)(vk - VK_F1 + 1));
    else {
        UINT ch = MapVirtualKeyW((UINT)vk, MAPVK_VK_TO_CHAR) & 0x7FFF;
        if (ch > 32 && ch != L'+') { key[0] = s_low((wchar_t)ch); key[1] = 0; }
    }
    if (!key[0]) return 0;
    int isF = vk >= VK_F1 && vk <= VK_F24;
    if (!ctrl && !alt && !win && !isF) return 0;   // plain letters would hijack typing everywhere

    if (ctrl)  s_cat(buf, L"ctrl+", 64);
    if (alt)   s_cat(buf, L"alt+", 64);
    if (shift) s_cat(buf, L"shift+", 64);
    if (win)   s_cat(buf, L"win+", 64);
    s_cat(buf, key, 64);
    return 1;
}

static void activate(int row) {
    g_msg[0] = 0;
    if (row == ROW_HOTKEY) {
        ui_unregister_hotkey();     // so pressing the old one doesn't hide the window
        g_capturing = 1;
    } else if (row == ROW_NAME) {
        g_editing = EDIT_NAME;
        ui_set_input(g_cfg.name);
    } else if (row == ROW_ACCENT) {
        wchar_t h[16]; hex_of(g_cfg.accent, h);
        g_editing = EDIT_ACCENT;
        ui_set_input(h);
    } else if (row == ROW_STARTUP) {
        startup_set(!startup_get());
        msg(OUT_OK, startup_get() ? L"I'll start quietly with Windows." : L"I won't start with Windows.");
    } else if (row == ROW_POSITION) {
        g_cfg.rememberPos = !g_cfg.rememberPos;
        cfg_save();
        msg(OUT_OK, g_cfg.rememberPos ? L"I'll come back where you last hid me. /resetwindow re-centers."
                                      : L"I'll always open in the middle of the screen.");
    }
    ui_redraw();
}

static void commit_edit(void) {
    wchar_t buf[128];
    ui_get_input(buf, 128);
    wchar_t *v = s_trim(buf);
    if (g_editing == EDIT_NAME) {
        s_cpy(g_cfg.name, v, 64);
        cfg_save();
        msg(OUT_OK, L"Saved.");
    } else if (g_editing == EDIT_ACCENT) {
        unsigned hex;
        if (!s_hex(v, &hex)) { msg(OUT_ERR, L"That's not a color. Use a hex code like #A855F7."); ui_redraw(); return; }
        set_accent(RGB((hex >> 16) & 255, (hex >> 8) & 255, hex & 255));
        msg(OUT_OK, L"Saved.");
    }
    g_editing = EDIT_NONE;
    ui_set_input(L"");
    ui_redraw();
}

// ---------------------------------------------------------------- open / close
int settings_active(void) { return g_active; }
int settings_wants_input(void) { return g_active && g_editing; }

void settings_open(void) {
    browser_close();
    g_active = 1; g_sel = 0; g_editing = EDIT_NONE; g_capturing = 0; g_msg[0] = 0;
    ui_set_input(L"");
    ui_redraw();
}

void settings_close(void) {
    if (!g_active) return;
    stop_capture();
    g_active = 0; g_editing = EDIT_NONE;
    ui_set_input(L"");
    ui_redraw();
}

// ---------------------------------------------------------------- input
int settings_key(WPARAM vk) {
    if (g_capturing) {
        if (vk == VK_CONTROL || vk == VK_MENU || vk == VK_SHIFT || vk == VK_LWIN || vk == VK_RWIN ||
            vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_LMENU || vk == VK_RMENU ||
            vk == VK_LSHIFT || vk == VK_RSHIFT) return 1;
        if (vk == VK_ESCAPE) { stop_capture(); msg(OUT_DIM, L"Kept the old hotkey."); ui_redraw(); return 1; }

        wchar_t spec[64];
        if (!describe_key(vk, spec)) {
            msg(OUT_ERR, L"Use ctrl, alt or win with a key (or an F key).");
            ui_redraw(); return 1;
        }
        wchar_t old[64]; s_cpy(old, g_cfg.hotkey, 64);
        s_cpy(g_cfg.hotkey, spec, 64);
        g_capturing = 0;
        if (ui_register_hotkey()) {
            cfg_save();
            wchar_t t[128]; wsprintfW(t, L"Hotkey is now %s.", spec);
            msg(OUT_OK, t);
        } else {
            s_cpy(g_cfg.hotkey, old, 64);
            ui_register_hotkey();
            wchar_t t[128]; wsprintfW(t, L"%s is taken by another app. Kept the old one.", spec);
            msg(OUT_ERR, t);
        }
        ui_redraw();
        return 1;
    }

    if (g_editing) {
        if (vk == VK_RETURN) { commit_edit(); return 1; }
        if (vk == VK_ESCAPE) { g_editing = EDIT_NONE; ui_set_input(L""); g_msg[0] = 0; ui_redraw(); return 1; }
        if (vk == VK_UP || vk == VK_DOWN || vk == VK_TAB) return 1;
        return 0;   // normal typing goes to the input box
    }

    switch (vk) {
    case VK_UP:     g_sel = (g_sel + NROWS - 1) % NROWS; g_msg[0] = 0; ui_redraw(); return 1;
    case VK_DOWN:   g_sel = (g_sel + 1) % NROWS;         g_msg[0] = 0; ui_redraw(); return 1;
    case VK_RETURN: case VK_SPACE: activate(g_sel); return 1;
    case VK_LEFT: case VK_RIGHT: {
        int d = vk == VK_RIGHT ? 1 : -1;
        if (g_sel == ROW_ACCENT) {
            int i = 0;
            while (i < NPRESETS && PRESETS[i] != g_cfg.accent) i++;
            i = (i == NPRESETS) ? 0 : (i + d + NPRESETS) % NPRESETS;
            set_accent(PRESETS[i]);
            g_msg[0] = 0;
            ui_redraw();
        } else if (g_sel == ROW_STARTUP || g_sel == ROW_POSITION) activate(g_sel);
        return 1;
    }
    case VK_ESCAPE: settings_close(); return 1;
    }
    return 1;   // ignore everything else while browsing
}

int settings_char(WPARAM ch) {
    if (g_capturing) return 1;
    if (g_editing) return ch == L'\r' || ch == 0x1B || ch == L'\t';
    return 1;
}

void settings_click(int x, int y) {
    if (g_capturing || g_editing) return;
    POINT pt = { x, y };
    for (int i = 0; i < NROWS; i++)
        if (PtInRect(&g_rows[i], pt)) { g_sel = i; activate(i); return; }
}

// ---------------------------------------------------------------- drawing
void settings_paint(HDC dc, RECT a) {
    static const wchar_t *labels[NROWS] = { L"Hotkey", L"Your name", L"Accent color", L"Start with Windows", L"Remember position" };
    int rowH = g_lineH + S(16);
    int y = a.top;
    int valueX = a.left + S(240);

    SelectObject(dc, g_font);
    ui_text(dc, a.left, y, L"Settings", g_accent);
    y += g_lineH + S(14);

    for (int i = 0; i < NROWS; i++) {
        RECT r = { a.left, y, a.right, y + rowH };
        g_rows[i] = r;
        int sel = i == g_sel;
        if (sel) ui_fill(dc, r.left, r.top, r.right, r.bottom, ui_mix(g_accent, C_BG, 13));
        int ty = y + (rowH - g_lineH) / 2;
        if (sel) ui_text(dc, a.left + S(10), ty, L"\x203A", g_accent);
        ui_text(dc, a.left + S(28), ty, labels[i], sel ? C_BRIGHT : C_TEXT);

        wchar_t v[96];
        switch (i) {
        case ROW_HOTKEY:
            if (g_capturing) ui_text(dc, valueX, ty, L"press your new shortcut...", g_accent);
            else ui_text(dc, valueX, ty, g_cfg.hotkey, C_BRIGHT);
            break;
        case ROW_NAME:
            if (g_editing == EDIT_NAME) ui_text(dc, valueX, ty, L"type it below, Enter to save", g_accent);
            else if (g_cfg.name[0]) ui_text(dc, valueX, ty, g_cfg.name, C_BRIGHT);
            else ui_text(dc, valueX, ty, L"not set", C_DIM);
            break;
        case ROW_ACCENT: {
            int sw = g_lineH - S(4);
            ui_fill(dc, valueX, ty + S(2), valueX + sw, ty + S(2) + sw, g_cfg.accent);
            if (g_editing == EDIT_ACCENT) ui_text(dc, valueX + sw + S(10), ty, L"type a hex below, Enter to save", g_accent);
            else {
                hex_of(g_cfg.accent, v);
                int w = ui_text(dc, valueX + sw + S(10), ty, v, C_BRIGHT);
                if (sel) ui_text(dc, valueX + sw + S(22) + w, ty, L"\x2190 \x2192 presets", C_DIM);
            }
            break;
        }
        case ROW_STARTUP:
            if (startup_get()) ui_text(dc, valueX, ty, L"on", C_OK);
            else ui_text(dc, valueX, ty, L"off", C_DIM);
            break;
        case ROW_POSITION:
            if (g_cfg.rememberPos) ui_text(dc, valueX, ty, L"on", C_OK);
            else ui_text(dc, valueX, ty, L"off", C_DIM);
            if (sel) ui_text(dc, valueX + S(50), ty, L"open where I was last hidden", C_DIM);
            break;
        }
        y += rowH + S(4);
    }

    if (g_msg[0]) {
        COLORREF c = g_msgKind == OUT_ERR ? C_ERR : g_msgKind == OUT_OK ? C_OK : C_DIM;
        ui_text(dc, a.left, y + S(10), g_msg, c);
    }

    SelectObject(dc, g_fontSmall);
    const wchar_t *hint = g_capturing ? L"Esc to cancel" :
                          g_editing   ? L"Enter to save    Esc to cancel" :
                                        L"\x2191\x2193 choose    Enter change    Esc back";
    ui_text(dc, a.left, a.bottom - S(16), hint, C_DIM);
    SelectObject(dc, g_font);
}
