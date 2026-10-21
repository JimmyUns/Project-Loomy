// ui.c - the window, drawing, hotkey, tray icon, and the program's entry point
#include "loomy.h"
#include <shellapi.h>
#include <dwmapi.h>

#define CLASS_NAME   L"LoomyWnd"
#define WM_TRAY      (WM_APP + 1)
#define WM_LOOMY_SHOW (WM_APP + 2)
#define HOTKEY_ID    1
#define TIMER_CLOCK  1
#define TIMER_NET    2

enum { MENU_OPEN = 1, MENU_SETTINGS, MENU_RELOAD, MENU_SHUTDOWN };

// ---------------------------------------------------------------- state
static HINSTANCE g_inst;
static HWND      g_wnd, g_edit;
static WNDPROC   g_editProc;
static HANDLE    g_mutex;
static HICON     g_icon, g_iconSmall;
HFONT            g_font, g_fontSmall;
static HFONT     g_fontTitle;
int              g_lineH;
static int       g_dpi = 96;
static int       g_hotkeyOk, g_dwmBorder, g_fresh;
static UINT      g_taskbarMsg;
static NOTIFYICONDATAW g_nid;

COLORREF         g_accent;
static COLORREF  g_accentDim;
static HBRUSH    g_brInput;

int S(int px) { return MulDiv(px, g_dpi, 96); }

#define WIN_W 720
#define WIN_H 480

// ---------------------------------------------------------------- output buffer
#define MAX_LINES 300
#define LINE_LEN  300
typedef struct { int kind; wchar_t text[LINE_LEN]; } Line;
static Line g_lines[MAX_LINES];
static int  g_first, g_count, g_scroll;

static Line *line_at(int i) { return &g_lines[(g_first + i) % MAX_LINES]; }

static void push_line(int kind, const wchar_t *t, int len) {
    Line *l;
    if (g_count < MAX_LINES) l = line_at(g_count++);
    else { l = line_at(0); g_first = (g_first + 1) % MAX_LINES; }
    if (len > LINE_LEN - 1) len = LINE_LEN - 1;
    for (int i = 0; i < len; i++) l->text[i] = t[i];
    l->text[len] = 0;
    l->kind = kind;
}

void out(int kind, const wchar_t *text) {
    const wchar_t *start = text;
    for (const wchar_t *p = text;; p++) {
        if (*p == L'\n' || !*p) {
            push_line(kind, start, (int)(p - start));
            if (!*p) break;
            start = p + 1;
        }
    }
    g_scroll = 0;
    if (g_wnd) InvalidateRect(g_wnd, 0, FALSE);
}

void outf(int kind, const wchar_t *fmt, ...) {
    wchar_t buf[1025];
    va_list ap; va_start(ap, fmt);
    wvsprintfW(buf, fmt, ap);
    va_end(ap);
    out(kind, buf);
}

void ui_set_timer_1s(int on) {
    if (on) SetTimer(g_wnd, TIMER_NET, 1000, 0);
    else    KillTimer(g_wnd, TIMER_NET);
}

void ui_clear(void) { g_count = 0; g_first = 0; g_scroll = 0; InvalidateRect(g_wnd, 0, FALSE); }

// ---------------------------------------------------------------- colors & fonts
COLORREF ui_mix(COLORREF a, COLORREF b, int pctA) {
    return RGB((GetRValue(a) * pctA + GetRValue(b) * (100 - pctA)) / 100,
               (GetGValue(a) * pctA + GetGValue(b) * (100 - pctA)) / 100,
               (GetBValue(a) * pctA + GetBValue(b) * (100 - pctA)) / 100);
}

static HFONT make_font(int px, int weight) {
    static const wchar_t *faces[] = { L"Cascadia Mono", L"Consolas" };
    for (int i = 0; i < 2; i++) {
        HFONT f = CreateFontW(-S(px), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH, faces[i]);
        HDC dc = GetDC(0);
        HGDIOBJ old = SelectObject(dc, f);
        wchar_t got[64]; GetTextFaceW(dc, 64, got);
        SelectObject(dc, old); ReleaseDC(0, dc);
        if (s_ieq(got, faces[i]) || i == 1) return f;
        DeleteObject(f);
    }
    return 0;
}

void ui_apply_colors(void) {
    g_accent = g_cfg.accent;
    g_accentDim = ui_mix(g_accent, C_BG, 45);
    if (g_wnd) {
        COLORREF border = ui_mix(g_accent, C_BG, 55);
        g_dwmBorder = SUCCEEDED(DwmSetWindowAttribute(g_wnd, 34 /*BORDER_COLOR*/, &border, sizeof(border)));
    }
}

// ---------------------------------------------------------------- hotkey
static int parse_hotkey(const wchar_t *spec, UINT *mods, UINT *vk) {
    wchar_t buf[64]; s_cpy(buf, spec, 64);
    *mods = MOD_NOREPEAT; *vk = 0;
    wchar_t *p = buf;
    while (*p) {
        wchar_t *tok = p;
        while (*p && *p != L'+') p++;
        if (*p) *p++ = 0;
        tok = s_trim(tok);
        if      (s_ieq(tok, L"ctrl") || s_ieq(tok, L"control")) *mods |= MOD_CONTROL;
        else if (s_ieq(tok, L"alt"))   *mods |= MOD_ALT;
        else if (s_ieq(tok, L"shift")) *mods |= MOD_SHIFT;
        else if (s_ieq(tok, L"win"))   *mods |= MOD_WIN;
        else if (s_ieq(tok, L"space")) *vk = VK_SPACE;
        else if (s_ieq(tok, L"`") || s_ieq(tok, L"backtick") || s_ieq(tok, L"tilde")) *vk = VK_OEM_3;
        else if ((tok[0] == L'f' || tok[0] == L'F') && tok[1] >= L'1' && tok[1] <= L'9') {
            int n = tok[1] - L'0';
            if (tok[2] >= L'0' && tok[2] <= L'9') n = n * 10 + (tok[2] - L'0');
            if (n >= 1 && n <= 24) *vk = VK_F1 + n - 1;
        }
        else if (tok[0] && !tok[1]) {
            wchar_t c = s_low(tok[0]);
            if (c >= L'a' && c <= L'z') *vk = 'A' + (c - L'a');
            else if (c >= L'0' && c <= L'9') *vk = c;
            else { SHORT r = VkKeyScanW(c); if (r != -1) *vk = r & 0xFF; }
        }
    }
    return *vk != 0;
}

static void register_hotkey(int retries) {
    UINT mods, vk;
    UnregisterHotKey(g_wnd, HOTKEY_ID);
    g_hotkeyOk = 0;
    if (!parse_hotkey(g_cfg.hotkey, &mods, &vk)) return;
    for (int i = 0; i <= retries; i++) {
        if (RegisterHotKey(g_wnd, HOTKEY_ID, mods, vk)) { g_hotkeyOk = 1; return; }
        Sleep(150);
    }
}

int  ui_hotkey_ok(void) { return g_hotkeyOk; }
int  ui_register_hotkey(void) { register_hotkey(0); InvalidateRect(g_wnd, 0, FALSE); return g_hotkeyOk; }
void ui_unregister_hotkey(void) { UnregisterHotKey(g_wnd, HOTKEY_ID); }
void ui_redraw(void) { InvalidateRect(g_wnd, 0, FALSE); }

static int screen_active(void) {
    return settings_active() || browser_active() || net_active() || changelog_active() || mode_active() || web_active();
}
static void reset_history_pos(void);

// ---------------------------------------------------------------- show / hide
static void layout(void) {
    HDC dc = GetDC(g_wnd);
    HGDIOBJ old = SelectObject(dc, g_font);
    TEXTMETRICW tm; GetTextMetricsW(dc, &tm);
    SelectObject(dc, old); ReleaseDC(g_wnd, dc);
    g_lineH = tm.tmHeight;

    int inputTop = S(WIN_H) - S(52);
    int x = S(22) + S(20);
    MoveWindow(g_edit, x, inputTop + (S(52) - g_lineH) / 2, S(WIN_W) - x - S(22), g_lineH, TRUE);
}

static void show(void) {
    if (cfg_reload_if_changed()) { register_hotkey(0); ui_apply_colors(); }
    if (g_fresh) {                 // it was hidden last time: come back with a clean screen
        g_fresh = 0;
        ui_clear();
        ui_set_input(L"");
        reset_history_pos();
    }

    POINT pt; GetCursorPos(&pt);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY), &mi);
    RECT wa = mi.rcWork;
    int w = S(WIN_W), h = S(WIN_H);
    int x = wa.left + (wa.right - wa.left - w) / 2;
    int y = wa.top + (wa.bottom - wa.top - h) * 2 / 9;

    SetWindowPos(g_wnd, HWND_TOPMOST, x, y, w, h, SWP_SHOWWINDOW);
    SetForegroundWindow(g_wnd);
    SetFocus(g_edit);
    SetTimer(g_wnd, TIMER_CLOCK, 15000, 0);
    InvalidateRect(g_wnd, 0, FALSE);
}

void ui_hide(void) {
    settings_close();
    browser_close();
    net_close();
    changelog_close();
    mode_close();
    web_close();
    g_fresh = 1;
    KillTimer(g_wnd, TIMER_CLOCK);
    ShowWindow(g_wnd, SW_HIDE);
}

static void toggle(void) {
    if (!IsWindowVisible(g_wnd)) { show(); return; }
    // visible: if it's not focused, try to focus it. If that's not possible (or it
    // already was focused), the press means "go away".
    if (GetForegroundWindow() != g_wnd) {
        SetForegroundWindow(g_wnd);
        if (GetForegroundWindow() == g_wnd) { SetFocus(g_edit); return; }
    }
    ui_hide();
}

// ---------------------------------------------------------------- tray
static void tray_add(void) {
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_wnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = g_iconSmall;
    s_cpy(g_nid.szTip, L"Loomy", 128);
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

static void tray_balloon(const wchar_t *title, const wchar_t *msg) {
    g_nid.uFlags = NIF_INFO;
    s_cpy(g_nid.szInfoTitle, title, 64);
    s_cpy(g_nid.szInfo, msg, 256);
    g_nid.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
}

static void tray_menu(void) {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, MENU_OPEN, L"Open Loomy");
    AppendMenuW(m, MF_STRING, MENU_SETTINGS, L"Settings");
    AppendMenuW(m, MF_SEPARATOR, 0, 0);
    AppendMenuW(m, MF_STRING, MENU_RELOAD, L"Reload");
    AppendMenuW(m, MF_STRING, MENU_SHUTDOWN, L"Shutdown");
    POINT pt; GetCursorPos(&pt);
    SetForegroundWindow(g_wnd);
    int id = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_wnd, 0);
    DestroyMenu(m);
    if (id == MENU_OPEN) show();
    else if (id == MENU_SETTINGS) { show(); settings_open(); }
    else if (id == MENU_RELOAD) ui_restart();
    else if (id == MENU_SHUTDOWN) ui_quit();
}

// ---------------------------------------------------------------- shutdown / restart
static void cleanup(void) {
    UnregisterHotKey(g_wnd, HOTKEY_ID);
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    if (g_mutex) { CloseHandle(g_mutex); g_mutex = 0; }
}

void ui_quit(void) { cleanup(); ExitProcess(0); }

void ui_restart(void) {
    wchar_t exe[MAX_PATH], cmd[MAX_PATH + 32] = L"\"";
    GetModuleFileNameW(0, exe, MAX_PATH);
    s_cat(cmd, exe, MAX_PATH + 32);
    s_cat(cmd, L"\" --restarted", MAX_PATH + 32);
    cleanup();
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    if (CreateProcessW(0, cmd, 0, 0, FALSE, 0, 0, 0, &si, &pi)) {
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    }
    ExitProcess(0);
}

// ---------------------------------------------------------------- painting
void ui_fill(HDC dc, int l, int t, int r, int b, COLORREF c) {
    RECT rc = { l, t, r, b };
    SetBkColor(dc, c);
    ExtTextOutW(dc, 0, 0, ETO_OPAQUE, &rc, 0, 0, 0);   // cheap solid fill, no brush needed
}

int ui_text(HDC dc, int x, int y, const wchar_t *t, COLORREF c) {
    SetTextColor(dc, c);
    TextOutW(dc, x, y, t, s_len(t));
    SIZE sz; GetTextExtentPoint32W(dc, t, s_len(t), &sz);
    return sz.cx;
}

// halftone dots that fade out, like the dither in the old Loomy logo
static void halftone(HDC dc, int y, int w) {
    int step = S(6), rows = 4, fadeEnd = w * 3 / 4;
    for (int r = 0; r < rows; r++) {
        int off = (r & 1) ? step / 2 : 0;
        int rowEnd = fadeEnd - r * fadeEnd / 6;          // each row fades out a bit sooner
        for (int x = off; x < rowEnd; x += step) {
            int size = (S(4) * (rowEnd - x) + rowEnd - 1) / rowEnd;
            ui_fill(dc, x, y + r * step, x + size, y + r * step + size, g_accentDim);
        }
    }
}

static void paint(HWND w) {
    PAINTSTRUCT ps;
    HDC wdc = BeginPaint(w, &ps);
    RECT rc; GetClientRect(w, &rc);
    int W = rc.right, H = rc.bottom;

    HDC dc = CreateCompatibleDC(wdc);
    HBITMAP bmp = CreateCompatibleBitmap(wdc, W, H);
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    SetBkMode(dc, TRANSPARENT);

    ui_fill(dc, 0, 0, W, H, C_BG);
    SetBkMode(dc, TRANSPARENT);

    int px = S(22);

    // header
    SelectObject(dc, g_fontTitle);
    int tw = ui_text(dc, px, S(14), L"Loomy", g_accent);
    SelectObject(dc, g_fontSmall);
    int vw = ui_text(dc, px + tw + S(10), S(17), L"v" LOOMY_VERSION L" \x00B7 ", C_DIM);
    ui_text(dc, px + tw + S(10) + vw, S(17), mode_name(mode_current()), g_accent);   // current mode

    SYSTEMTIME t; GetLocalTime(&t);
    wchar_t right[96];
    if (g_hotkeyOk) wsprintfW(right, L"%02d:%02d", t.wHour, t.wMinute);
    else            wsprintfW(right, L"hotkey taken \x00B7 %02d:%02d", t.wHour, t.wMinute);
    SIZE sz; GetTextExtentPoint32W(dc, right, s_len(right), &sz);
    ui_text(dc, W - px - sz.cx, S(17), right, g_hotkeyOk ? C_DIM : C_ERR);

    halftone(dc, S(46), W);

    // output lines, newest at the bottom
    int inputTop = H - S(52);
    RECT area = { px, S(84), W - px, inputTop - S(12) };
    SelectObject(dc, g_font);
    IntersectClipRect(dc, area.left, area.top, area.right, area.bottom);
    if (settings_active()) settings_paint(dc, area);
    else if (browser_active()) browser_paint(dc, area);
    else if (net_active()) net_paint(dc, area);
    else if (changelog_active()) changelog_paint(dc, area);
    else if (mode_active()) mode_paint(dc, area);
    else if (web_active()) web_paint(dc, area);
    int y = screen_active() ? area.top : area.bottom;
    int indent = S(20);
    for (int k = g_scroll; k < g_count && y > area.top; k++) {
        Line *l = line_at(g_count - 1 - k);
        int echo = l->kind == OUT_ECHO;
        RECT r = { area.left + (echo ? indent : 0), 0, area.right, 0 };
        UINT fmt = DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL | DT_EXPANDTABS;
        DrawTextW(dc, l->text, -1, &r, fmt | DT_CALCRECT);
        int h = r.bottom - r.top;
        if (h < g_lineH) h = g_lineH;
        y -= h;
        r.top = y; r.bottom = y + h; r.right = area.right;
        COLORREF c = l->kind == OUT_DIM ? C_DIM : l->kind == OUT_ACCENT ? g_accent :
                     l->kind == OUT_OK ? C_OK : l->kind == OUT_ERR ? C_ERR :
                     echo ? C_BRIGHT : C_TEXT;
        SetTextColor(dc, c);
        DrawTextW(dc, l->text, -1, &r, fmt);
        if (echo) {
            ui_text(dc, area.left, y, L"\x203A", g_accent);
            y -= S(12);   // breathing room between command blocks
        }
    }
    SelectClipRgn(dc, 0);

    if (g_scroll > 0 && !screen_active()) {
        SelectObject(dc, g_fontSmall);
        const wchar_t *more = L"\x2193 newer below";
        GetTextExtentPoint32W(dc, more, s_len(more), &sz);
        ui_text(dc, W - px - sz.cx, inputTop - S(12) - sz.cy, more, C_DIM);
    }

    // input bar
    ui_fill(dc, 0, inputTop, W, H, C_INPUTBG);
    ui_fill(dc, 0, inputTop, W, inputTop + 1, C_RULE);
    SelectObject(dc, g_font);
    ui_text(dc, px, inputTop + (S(52) - g_lineH) / 2, L"\x203A", g_accent);

    // fallback border for Windows 10 (Windows 11 draws a rounded one for us)
    if (!g_dwmBorder) {
        COLORREF b = ui_mix(g_accent, C_BG, 55);
        ui_fill(dc, 0, 0, W, 1, b); ui_fill(dc, 0, H - 1, W, H, b);
        ui_fill(dc, 0, 0, 1, H, b); ui_fill(dc, W - 1, 0, W, H, b);
    }

    BitBlt(wdc, 0, 0, W, H, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(w, &ps);
}

// ---------------------------------------------------------------- input box
#define HIST 32
static wchar_t g_hist[HIST][LINE_LEN];
static int g_hcount, g_hpos;

void ui_get_input(wchar_t *buf, int cap) { GetWindowTextW(g_edit, buf, cap); }

void ui_set_input(const wchar_t *t) {
    SetWindowTextW(g_edit, t);
    int n = s_len(t);
    SendMessageW(g_edit, EM_SETSEL, n, n);
}

static void submit(void) {
    wchar_t line[LINE_LEN];
    GetWindowTextW(g_edit, line, LINE_LEN);
    SetWindowTextW(g_edit, L"");
    wchar_t *t = s_trim(line);
    if (*t && (g_hcount == 0 || !s_ieq(g_hist[(g_hcount - 1) % HIST], t))) {
        s_cpy(g_hist[g_hcount % HIST], t, LINE_LEN);
        g_hcount++;
    }
    g_hpos = g_hcount;
    cmd_run(t);
}

static void reset_history_pos(void) { g_hpos = g_hcount; }

static void history(int dir) {
    int oldest = g_hcount > HIST ? g_hcount - HIST : 0;
    int p = g_hpos + dir;
    if (p < oldest) return;
    if (p >= g_hcount) { g_hpos = g_hcount; ui_set_input(L""); return; }
    g_hpos = p;
    ui_set_input(g_hist[p % HIST]);
}

static void delete_word(void) {
    wchar_t t[LINE_LEN];
    GetWindowTextW(g_edit, t, LINE_LEN);
    DWORD s = 0, e = 0;
    SendMessageW(g_edit, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
    int i = (int)s;
    while (i > 0 && t[i - 1] == L' ') i--;
    while (i > 0 && t[i - 1] != L' ') i--;
    SendMessageW(g_edit, EM_SETSEL, i, e);
    SendMessageW(g_edit, EM_REPLACESEL, TRUE, (LPARAM)L"");
}

static LRESULT CALLBACK edit_proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (settings_active()) {
        if ((m == WM_KEYDOWN || m == WM_SYSKEYDOWN) && settings_key(wp)) return 0;
        if ((m == WM_CHAR || m == WM_SYSCHAR) && settings_char(wp)) return 0;
    } else if (browser_active()) {
        if (m == WM_KEYDOWN && browser_key(wp)) return 0;
        if (m == WM_CHAR && browser_char(wp)) return 0;
    } else if (net_active()) {
        if (m == WM_KEYDOWN && net_key(wp)) return 0;
        if (m == WM_CHAR && net_char(wp)) return 0;
    } else if (changelog_active()) {
        if (m == WM_KEYDOWN && changelog_key(wp)) return 0;
        if (m == WM_CHAR && changelog_char(wp)) return 0;
    } else if (mode_active()) {
        if (m == WM_KEYDOWN && mode_key(wp)) return 0;
        if (m == WM_CHAR && mode_char(wp)) return 0;
    } else if (web_active()) {
        if (m == WM_KEYDOWN && web_key(wp)) return 0;
        if (m == WM_CHAR && web_char(wp)) return 0;
    }
    if (m == WM_KEYDOWN) {
        switch (wp) {
        case VK_RETURN: submit(); return 0;
        case VK_ESCAPE:
            if (GetWindowTextLengthW(h)) SetWindowTextW(h, L""); else ui_hide();
            return 0;
        case VK_UP:
            if (GetKeyState(VK_CONTROL) < 0) {   // Ctrl+Up scrolls the chat up
                g_scroll += 3;
                if (g_scroll > g_count - 1) g_scroll = g_count > 0 ? g_count - 1 : 0;
                InvalidateRect(g_wnd, 0, FALSE);
            } else history(-1);
            return 0;
        case VK_DOWN:
            if (GetKeyState(VK_CONTROL) < 0) {   // Ctrl+Down scrolls the chat down
                g_scroll -= 3; if (g_scroll < 0) g_scroll = 0;
                InvalidateRect(g_wnd, 0, FALSE);
            } else history(+1);
            return 0;
        case VK_PRIOR: g_scroll += 5; if (g_scroll > g_count - 1) g_scroll = g_count > 0 ? g_count - 1 : 0;
                       InvalidateRect(g_wnd, 0, FALSE); return 0;
        case VK_NEXT:  g_scroll -= 5; if (g_scroll < 0) g_scroll = 0; InvalidateRect(g_wnd, 0, FALSE); return 0;
        case VK_TAB: {
            wchar_t t[LINE_LEN];
            GetWindowTextW(h, t, LINE_LEN);
            cmd_complete(t, LINE_LEN);
            ui_set_input(t);
            return 0;
        }
        }
    }
    if (m == WM_CHAR) {
        if (wp == L'\r' || wp == L'\t' || wp == 0x1B) return 0;            // no beeps
        if (wp == 0x7F) { delete_word(); return 0; }                        // ctrl+backspace
        if (wp == 0x0C) { ui_clear(); return 0; }                           // ctrl+L
        if (wp == 0x01) { SendMessageW(h, EM_SETSEL, 0, -1); return 0; }    // ctrl+A
    }
    if (m == WM_MOUSEWHEEL) return SendMessageW(g_wnd, m, wp, lp);
    return CallWindowProcW(g_editProc, h, m, wp, lp);
}

// ---------------------------------------------------------------- main window
static LRESULT CALLBACK wnd_proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_PAINT: paint(w); return 0;
    case WM_ERASEBKGND: return 1;

    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wp, C_BRIGHT);
        SetBkColor((HDC)wp, C_INPUTBG);
        return (LRESULT)g_brInput;

    case WM_NCHITTEST: {   // drag the window by its header
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        ScreenToClient(w, &pt);
        if (pt.y < S(72)) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_LBUTTONDOWN:
        if (settings_active()) settings_click((short)LOWORD(lp), (short)HIWORD(lp));
        else if (browser_active()) browser_click((short)LOWORD(lp), (short)HIWORD(lp), 0);
        else if (net_active()) net_click((short)LOWORD(lp), (short)HIWORD(lp));
        else if (changelog_active()) changelog_click((short)LOWORD(lp), (short)HIWORD(lp));
        else if (mode_active()) mode_click((short)LOWORD(lp), (short)HIWORD(lp), 0);
        else if (web_active()) web_click((short)LOWORD(lp), (short)HIWORD(lp), 0);
        SetFocus(g_edit);
        return 0;
    case WM_LBUTTONDBLCLK:
        if (browser_active()) browser_click((short)LOWORD(lp), (short)HIWORD(lp), 1);
        else if (mode_active()) mode_click((short)LOWORD(lp), (short)HIWORD(lp), 1);
        else if (web_active()) web_click((short)LOWORD(lp), (short)HIWORD(lp), 1);
        SetFocus(g_edit);
        return 0;
    case WM_ACTIVATE: if (LOWORD(wp) != WA_INACTIVE) SetFocus(g_edit); return 0;

    case WM_MOUSEWHEEL: {
        if (browser_active()) { browser_wheel((short)HIWORD(wp) / WHEEL_DELTA); return 0; }
        if (net_active()) { net_wheel((short)HIWORD(wp) / WHEEL_DELTA); return 0; }
        if (changelog_active()) { changelog_wheel((short)HIWORD(wp) / WHEEL_DELTA); return 0; }
        if (mode_active()) { mode_wheel((short)HIWORD(wp) / WHEEL_DELTA); return 0; }
        if (web_active()) { web_wheel((short)HIWORD(wp) / WHEEL_DELTA); return 0; }
        int d = (short)HIWORD(wp) / WHEEL_DELTA * 3;
        g_scroll += d;
        if (g_scroll > g_count - 1) g_scroll = g_count > 0 ? g_count - 1 : 0;
        if (g_scroll < 0) g_scroll = 0;
        InvalidateRect(w, 0, FALSE);
        return 0;
    }

    case WM_HOTKEY: if (wp == HOTKEY_ID) toggle(); return 0;
    case WM_LOOMY_SHOW: show(); return 0;
    case WM_TIMER:
        if (wp == TIMER_NET) net_tick();
        else InvalidateRect(w, 0, FALSE);
        return 0;

    case WM_TRAY:
        if (lp == WM_LBUTTONUP) toggle();
        else if (lp == WM_RBUTTONUP) tray_menu();
        return 0;

    case WM_CLOSE: ui_hide(); return 0;
    case WM_ENDSESSION: if (wp) cleanup(); return 0;
    }
    if (m == g_taskbarMsg && m) { tray_add(); return 0; }   // explorer restarted
    return DefWindowProcW(w, m, wp, lp);
}

// ---------------------------------------------------------------- entry point
void entry(void) {
    g_inst = GetModuleHandleW(0);
    const wchar_t *cmdline = GetCommandLineW();

    // Elevated helper mode: "--kill <pid>" ends one process and exits. No window, no tray.
    // This is the copy that the UAC prompt launched; the normal Loomy stays non-admin.
    const wchar_t *kp = cmdline;
    while (*kp && !s_istarts(kp, L"--kill")) kp++;
    if (*kp) {
        kp += 6;
        UINT code = 0;   // 0 = all good; set to 1 if any kill fails
        int any = 0;
        while (*kp) {
            while (*kp == L' ' || *kp == L',') kp++;
            if (*kp < L'0' || *kp > L'9') break;
            DWORD pid = 0;
            while (*kp >= L'0' && *kp <= L'9') pid = pid * 10 + (*kp++ - L'0');
            any = 1;
            HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
            if (h) { if (!TerminateProcess(h, 1)) code = 1; CloseHandle(h); }
            else code = 1;
        }
        ExitProcess(any ? code : 1);
    }

    int silent = s_has(cmdline, L"--silent");
    int restarted = s_has(cmdline, L"--restarted");

    // only one Loomy at a time: a second launch just pops up the first one
    g_mutex = CreateMutexW(0, TRUE, L"Loomy.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(CLASS_NAME, 0);
        if (other) PostMessageW(other, WM_LOOMY_SHOW, 0, 0);
        ExitProcess(0);
    }

    HDC sdc = GetDC(0); g_dpi = GetDeviceCaps(sdc, LOGPIXELSY); ReleaseDC(0, sdc);

    cfg_load();

    g_icon = (HICON)LoadImageW(g_inst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                               GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    g_iconSmall = (HICON)LoadImageW(g_inst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                    GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(0, (LPCWSTR)IDC_ARROW);
    wc.hIcon = g_icon;
    wc.hIconSm = g_iconSmall;
    wc.lpszClassName = CLASS_NAME;
    RegisterClassExW(&wc);

    g_wnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, CLASS_NAME, L"Loomy",
                            WS_POPUP | WS_CLIPCHILDREN, 0, 0, S(WIN_W), S(WIN_H), 0, 0, g_inst, 0);

    int round = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(g_wnd, 33 /*CORNER_PREFERENCE*/, &round, sizeof(round));

    g_font      = make_font(15, FW_NORMAL);
    g_fontTitle = make_font(19, FW_BOLD);
    g_fontSmall = make_font(12, FW_NORMAL);
    g_brInput   = CreateSolidBrush(C_INPUTBG);
    ui_apply_colors();

    g_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                             0, 0, 10, 10, g_wnd, 0, g_inst, 0);
    SendMessageW(g_edit, WM_SETFONT, (WPARAM)g_font, FALSE);
    SendMessageW(g_edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    SendMessageW(g_edit, EM_LIMITTEXT, LINE_LEN - 10, 0);
    g_editProc = (WNDPROC)SetWindowLongPtrW(g_edit, GWLP_WNDPROC, (LONG_PTR)edit_proc);
    layout();

    register_hotkey(restarted ? 20 : 0);
    g_taskbarMsg = RegisterWindowMessageW(L"TaskbarCreated");
    tray_add();

    cmd_greet();
    if (!g_hotkeyOk) {
        outf(OUT_ERR, L"Couldn't grab %s, another app is using it.", g_cfg.hotkey);
        out(OUT_DIM, L"Pick another in: /config");
        if (silent) tray_balloon(L"Loomy", L"My hotkey is taken by another app. Click my tray icon to open me.");
    }
    if (restarted) out(OUT_OK, L"Reloaded. Fresh as new.");
    if (!silent || !g_hotkeyOk) show();

    MSG msg;
    while (GetMessageW(&msg, 0, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    cleanup();
    ExitProcess(0);
}
