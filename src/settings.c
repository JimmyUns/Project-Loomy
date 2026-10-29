// settings.c - the config screen (type: /config, or /config general|custom)
// Two sections, Tab switches. Up/Down picks a row, Enter (or Left/Right) changes it,
// Esc goes back. Everything saves instantly.
#include "loomy.h"

enum { ROW_HOTKEY, ROW_NAME, ROW_STARTUP, ROW_POSITION,                     // General
       ROW_ACCENT, ROW_ANIM, ROW_DOTS, ROW_LINES, ROW_SELECT, ROW_TEXT, ROW_FADE, ROW_FADESPD,   // Customization
       NROWS };
enum { EDIT_NONE, EDIT_NAME, EDIT_ACCENT };

#define NSECTIONS 2
static const wchar_t *SEC_NAME[NSECTIONS] = { L"General", L"Customization" };
static const int SEC_FIRST[NSECTIONS + 1] = { ROW_HOTKEY, ROW_ACCENT, NROWS };

static int  g_active, g_sec, g_sel, g_editing, g_capturing;   // g_sel is a ROW_ value
static RECT g_rows[NROWS], g_tabs[NSECTIONS], g_slider;
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
    } else if (row == ROW_ANIM) {
        g_cfg.anim = (g_cfg.anim + 1) % 3;          // auto -> on -> off -> auto
        cfg_save();
        anim_config_changed();
    } else if (row >= ROW_DOTS && row <= ROW_FADE) {
        int *v = row == ROW_DOTS ? &g_cfg.animDots : row == ROW_LINES ? &g_cfg.animLines :
                 row == ROW_SELECT ? &g_cfg.animSelect : row == ROW_TEXT ? &g_cfg.animText : &g_cfg.fadeIn;
        *v = !*v;
        cfg_save();
        anim_config_changed();
        if (row == ROW_FADE && *v) msg(OUT_DIM, L"You'll see it the next time Loomy opens.");
    } else if (row == ROW_FADESPD) {
        msg(OUT_DIM, L"Use \x2190 \x2192 (or click the bar) to change it.");
    }
    ui_redraw();
}

static void set_fade_ms(int ms) {
    ms = (ms + FADE_STEP / 2) / FADE_STEP * FADE_STEP;
    if (ms < FADE_MIN) ms = FADE_MIN;
    if (ms > FADE_MAX) ms = FADE_MAX;
    if (ms == g_cfg.fadeMs) return;
    g_cfg.fadeMs = ms;
    cfg_save();
    ui_redraw();
}

static void set_section(int sec) {
    g_sec = (sec + NSECTIONS) % NSECTIONS;
    g_sel = SEC_FIRST[g_sec];
    g_msg[0] = 0;
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

void settings_open(int section) {
    browser_close();
    g_active = 1; g_editing = EDIT_NONE; g_capturing = 0;
    set_section(section);
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

    int first = SEC_FIRST[g_sec], n = SEC_FIRST[g_sec + 1] - first;
    switch (vk) {
    case VK_TAB:    set_section(g_sec + (GetKeyState(VK_SHIFT) < 0 ? -1 : 1)); return 1;
    case VK_UP:     g_sel = first + (g_sel - first + n - 1) % n; g_msg[0] = 0; ui_redraw(); return 1;
    case VK_DOWN:   g_sel = first + (g_sel - first + 1) % n;     g_msg[0] = 0; ui_redraw(); return 1;
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
        } else if (g_sel == ROW_ANIM) {
            g_cfg.anim = (g_cfg.anim + (d > 0 ? 1 : 2)) % 3;
            cfg_save();
            anim_config_changed();
            ui_redraw();
        } else if (g_sel == ROW_FADESPD) {
            set_fade_ms(g_cfg.fadeMs + d * FADE_STEP);
        } else if (g_sel == ROW_STARTUP || g_sel == ROW_POSITION || (g_sel >= ROW_DOTS && g_sel <= ROW_FADE))
            activate(g_sel);
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
    for (int s = 0; s < NSECTIONS; s++)
        if (PtInRect(&g_tabs[s], pt)) { set_section(s); return; }
    if (g_sec == 1 && PtInRect(&g_slider, pt)) {          // click on the fade-speed bar
        g_sel = ROW_FADESPD;
        set_fade_ms(FADE_MIN + (x - g_slider.left) * (FADE_MAX - FADE_MIN) / (g_slider.right - g_slider.left));
        ui_redraw();
        return;
    }
    for (int i = SEC_FIRST[g_sec]; i < SEC_FIRST[g_sec + 1]; i++)
        if (PtInRect(&g_rows[i], pt)) { g_sel = i; activate(i); return; }
}

// ---------------------------------------------------------------- drawing
static void onoff(HDC dc, int x, int y, int on, int dim) {
    ui_text(dc, x, y, on ? L"on" : L"off", dim ? C_DIM : on ? C_OK : C_DIM);
}

void settings_paint(HDC dc, RECT a) {
    static const wchar_t *labels[NROWS] = {
        L"Hotkey", L"Your name", L"Start with Windows", L"Remember position",
        L"Accent color", L"Animations", L"Dot wave", L"Line glow", L"List glide", L"Text fade-in", L"Fade in on open",
        L"Fade speed" };
    static const wchar_t *about[NROWS] = {
        0, 0, 0, L"open where I was last hidden",
        0, 0, L"a slow wave drifts through the dots", L"a glow sweeps the lines when I open",
        L"the highlight slides between rows", L"new lines fade in", L"I fade in instead of popping up", 0 };
    int rowH = g_lineH + S(14);
    int y = a.top;
    int valueX = a.left + S(240);

    // section tabs
    SelectObject(dc, g_font);
    int x = a.left;
    for (int s = 0; s < NSECTIONS; s++) {
        int w = ui_text(dc, x, y, SEC_NAME[s], s == g_sec ? g_accent : C_DIM);
        RECT t = { x - S(6), y - S(4), x + w + S(6), y + g_lineH + S(6) };
        g_tabs[s] = t;
        if (s == g_sec) ui_fill(dc, x, y + g_lineH + S(3), x + w, y + g_lineH + S(5), g_accent);
        x += w + S(28);
    }
    y += g_lineH + S(16);

    int animsOff = g_cfg.anim == ANIM_OFF;
    for (int i = SEC_FIRST[g_sec]; i < SEC_FIRST[g_sec + 1]; i++) {
        RECT r = { a.left, y, a.right, y + rowH };
        g_rows[i] = r;
        int sel = i == g_sel;
        if (sel) ui_sel_row(dc, r.left, r.top, r.right, r.bottom);
        int ty = y + (rowH - g_lineH) / 2;
        int sub = i >= ROW_DOTS && i <= ROW_FADESPD;
        if (sel) ui_text(dc, a.left + S(10), ty, L"\x203A", g_accent);
        int off = (sub && animsOff) || (i == ROW_FADESPD && !g_cfg.fadeIn);
        ui_text(dc, a.left + (i == ROW_FADESPD ? S(60) : sub ? S(44) : S(28)), ty, labels[i],
                sel ? C_BRIGHT : off ? C_DIM : C_TEXT);

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
        case ROW_STARTUP:  onoff(dc, valueX, ty, startup_get(), 0); break;
        case ROW_POSITION: onoff(dc, valueX, ty, g_cfg.rememberPos, 0); break;
        case ROW_ANIM: {
            const wchar_t *m = g_cfg.anim == ANIM_OFF ? L"off" : g_cfg.anim == ANIM_ON ? L"on" : L"auto";
            int w = ui_text(dc, valueX, ty, m, g_cfg.anim == ANIM_OFF ? C_DIM : C_OK);
            if (sel) ui_text(dc, valueX + w + S(14), ty,
                             g_cfg.anim == ANIM_AUTO ? L"skipped when the PC is busy" :
                             g_cfg.anim == ANIM_ON   ? L"always" : L"none at all", C_DIM);
            break;
        }
        case ROW_DOTS:   onoff(dc, valueX, ty, g_cfg.animDots, animsOff); break;
        case ROW_LINES:  onoff(dc, valueX, ty, g_cfg.animLines, animsOff); break;
        case ROW_SELECT: onoff(dc, valueX, ty, g_cfg.animSelect, animsOff); break;
        case ROW_TEXT:   onoff(dc, valueX, ty, g_cfg.animText, animsOff); break;
        case ROW_FADE:   onoff(dc, valueX, ty, g_cfg.fadeIn, animsOff); break;
        case ROW_FADESPD: {                      // a little slider: track, fill, knob, value
            int tw = S(150), th = S(4), tx = valueX, my = ty + g_lineH / 2;
            int fill = (g_cfg.fadeMs - FADE_MIN) * tw / (FADE_MAX - FADE_MIN);
            COLORREF on = off ? C_DIM : g_accent;
            ui_fill(dc, tx, my - th / 2, tx + tw, my + th / 2 + 1, ui_mix(C_DIM, C_BG, 35));
            ui_fill(dc, tx, my - th / 2, tx + fill, my + th / 2 + 1, on);
            ui_fill(dc, tx + fill - S(4), my - S(6), tx + fill + S(4), my + S(6), off ? C_DIM : C_BRIGHT);
            RECT sr = { tx - S(6), y, tx + tw + S(6), y + rowH };
            g_slider = sr;
            wsprintfW(v, L"%d ms", g_cfg.fadeMs);
            int w = ui_text(dc, tx + tw + S(16), ty, v, off ? C_DIM : C_BRIGHT);
            if (sel) ui_text(dc, tx + tw + S(30) + w, ty, L"\x2190 faster  \x2192 slower", C_DIM);
            break;
        }
        }
        if (sel && about[i]) ui_text(dc, valueX + S(50), ty, about[i], C_DIM);
        y += rowH + S(2);
    }

    if (g_msg[0]) {
        COLORREF c = g_msgKind == OUT_ERR ? C_ERR : g_msgKind == OUT_OK ? C_OK : C_DIM;
        ui_text(dc, a.left, y + S(8), g_msg, c);
    }

    SelectObject(dc, g_fontSmall);
    const wchar_t *hint = g_capturing ? L"Esc to cancel" :
                          g_editing   ? L"Enter to save    Esc to cancel" :
                                        L"Tab section    \x2191\x2193 choose    Enter change    Esc back";
    ui_text(dc, a.left, a.bottom - S(16), hint, C_DIM);
    SelectObject(dc, g_font);
}
