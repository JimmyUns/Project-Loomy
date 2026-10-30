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
#define TIMER_REFOCUS 3
#define TIMER_SCREENS 4

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
static RECT      g_clockRect;              // double-click the clock = hide (for when your hands are busy)
static UINT      g_taskbarMsg;
static int       g_havePos, g_posX, g_posY;   // where it was last hidden (memory only: a reload forgets it)
static int       g_refocusLeft;
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
typedef struct { int kind; ULONGLONG born; wchar_t text[LINE_LEN]; } Line;   // born: for the fade-in
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
    l->born = GetTickCount64();
}

void out(int kind, const wchar_t *text) {
    phone_out_tap(kind, text);             // a command from the phone: its answer goes back too
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

static COLORREF border_color(void) { return ui_mix(g_accent, C_BG, 55); }
static COLORREF glow_color(void)   { return ui_mix(RGB(0xFF, 0xFF, 0xFF), g_accent, 35); }

void ui_apply_colors(void) {
    g_accent = g_cfg.accent;
    g_accentDim = ui_mix(g_accent, C_BG, 45);
    if (g_wnd) {
        COLORREF border = border_color();
        g_dwmBorder = SUCCEEDED(DwmSetWindowAttribute(g_wnd, 34 /*BORDER_COLOR*/, &border, sizeof(border)));
    }
}

// Windows 11 draws the border itself (one color), so the line glow there is a brightness pulse.
// Windows 10's border is drawn by paint(), which sweeps the glow along it instead.
void ui_glow_border(int k) {
    if (!g_wnd || !g_dwmBorder) return;
    COLORREF c = ui_mix(glow_color(), border_color(), k * 100 / 1024);
    DwmSetWindowAttribute(g_wnd, 34 /*BORDER_COLOR*/, &c, sizeof(c));
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
// ui_redraw only queues a paint, which waits until we're back in the message loop.
// Before a slow blocking call (network), use this so "Checking..." is on screen first.
// `dp off`: wait a beat so letting go of Enter isn't the input that wakes them back up.
// Sent to our own window (DefWindowProc does the work), not broadcast: a broadcast
// waits on every app, so one hung window could freeze Loomy.
void ui_screens_off_soon(void) { SetTimer(g_wnd, TIMER_SCREENS, 500, 0); }

void ui_flush(void) { if (g_wnd) { InvalidateRect(g_wnd, 0, FALSE); UpdateWindow(g_wnd); } }

static int screen_active(void) {
    return settings_active() || browser_active() || net_active() || changelog_active() || mode_active() || web_active() ||
           display_active() || sysinfo_active() || pkg_active() || phone_active();
}
static void reset_history_pos(void);

// ---------------------------------------------------------------- show / hide
// Panels (lists, pickers, sysinfo...) take their keys straight from the edit box, so it
// keeps focus, but you can't type into them. Only settings' name/color edit uses the box.
static int input_shown(void) { return !screen_active() || settings_wants_input(); }
static int g_inputShown = 1;

static void layout(void) {
    HDC dc = GetDC(g_wnd);
    HGDIOBJ old = SelectObject(dc, g_font);
    TEXTMETRICW tm; GetTextMetricsW(dc, &tm);
    SelectObject(dc, old); ReleaseDC(g_wnd, dc);
    g_lineH = tm.tmHeight;

    RECT rc; GetClientRect(g_wnd, &rc);
    if (rc.right <= 0) { rc.right = S(WIN_W); rc.bottom = S(WIN_H); }
    int inputTop = rc.bottom - S(52);
    int x = S(22) + S(20);
    g_inputShown = input_shown();
    if (!g_inputShown) x = -S(4000);       // parked off-screen: still has focus, so panel keys keep working
    MoveWindow(g_edit, x, inputTop + (S(52) - g_lineH) / 2, rc.right - S(42) - S(22), g_lineH, TRUE);
}

// the default spot: middle of the screen the mouse is on, a bit above center
static void centered(int *x, int *y) {
    POINT pt; GetCursorPos(&pt);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY), &mi);
    RECT wa = mi.rcWork;
    *x = wa.left + (wa.right - wa.left - S(WIN_W)) / 2;
    *y = wa.top + (wa.bottom - wa.top - S(WIN_H)) * 2 / 9;
}

// is the remembered spot still usable? (its header has to be on a screen that still exists,
// so an unplugged monitor can't strand the window off-screen)
static int pos_ok(int x, int y) {
    POINT top = { x + S(WIN_W) / 2, y + S(20) };
    return MonitorFromPoint(top, MONITOR_DEFAULTTONULL) != 0;
}

void ui_reset_window(void) {
    g_havePos = 0;
    if (!IsWindowVisible(g_wnd)) return;
    int x, y; centered(&x, &y);
    SetWindowPos(g_wnd, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE);
}

// sysinfo's big view: ~1.5x, grown around the window's middle, shrunk to fit the screen
// (a 150% scaled laptop screen can't fit the full size). Back to normal the same way.
static int g_big;
void ui_set_big(int on) {
    if (!g_wnd || on == g_big) return;
    g_big = on;
    RECT r; GetWindowRect(g_wnd, &r);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(MonitorFromWindow(g_wnd, MONITOR_DEFAULTTONEAREST), &mi);
    RECT wa = mi.rcWork;
    int w = S(WIN_W), h = S(WIN_H);
    if (on) {
        w = S(WIN_W * 3 / 2); h = S(WIN_H * 3 / 2);
        if (w > wa.right - wa.left - S(24)) w = wa.right - wa.left - S(24);
        if (h > wa.bottom - wa.top - S(24)) h = wa.bottom - wa.top - S(24);
        if (w < S(WIN_W)) w = S(WIN_W);
        if (h < S(WIN_H)) h = S(WIN_H);
    }
    int x = (r.left + r.right) / 2 - w / 2, y = (r.top + r.bottom) / 2 - h / 2;
    if (x + w > wa.right) x = wa.right - w;
    if (y + h > wa.bottom) y = wa.bottom - h;
    if (x < wa.left) x = wa.left;
    if (y < wa.top) y = wa.top;
    SetWindowPos(g_wnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
    layout();
    InvalidateRect(g_wnd, 0, FALSE);
}

// after opening a site, the browser takes focus. Ask for it back a couple of times;
// Windows refuses on its own if you've clicked somewhere else since, so this never fights you.
void ui_refocus_soon(void) {
    g_refocusLeft = 2;
    SetTimer(g_wnd, TIMER_REFOCUS, 400, 0);
}

static void show(void) {
    KillTimer(g_wnd, TIMER_SCREENS);       // summoned right after `dp off`: keep the screens on
    if (cfg_reload_if_changed()) { register_hotkey(0); ui_apply_colors(); }
    if (g_fresh) {                 // it was hidden last time: come back with a clean screen
        g_fresh = 0;
        ui_clear();
        ui_set_input(L"");
        reset_history_pos();
    }

    int x, y, w = S(WIN_W), h = S(WIN_H);
    if (g_cfg.rememberPos && g_havePos && pos_ok(g_posX, g_posY)) { x = g_posX; y = g_posY; }
    else centered(&x, &y);

    if (!IsWindowVisible(g_wnd)) anim_show(g_wnd);   // before showing: the fade-in starts see-through
    SetWindowPos(g_wnd, HWND_TOPMOST, x, y, w, h, SWP_SHOWWINDOW);
    SetForegroundWindow(g_wnd);
    SetFocus(g_edit);
    SetTimer(g_wnd, TIMER_CLOCK, 15000, 0);
    InvalidateRect(g_wnd, 0, FALSE);
}

void ui_hide(void) {
    sysinfo_close();                       // first: it puts the normal size back
    if (IsWindowVisible(g_wnd)) {          // remember where it was, for "Remember position"
        RECT r; GetWindowRect(g_wnd, &r);
        g_posX = r.left; g_posY = r.top; g_havePos = 1;
    }
    KillTimer(g_wnd, TIMER_REFOCUS);
    settings_close();
    browser_close();
    net_close();
    changelog_close();
    mode_close();
    web_close();
    display_close();
    pkg_close();
    phone_close();
    g_fresh = 1;
    KillTimer(g_wnd, TIMER_CLOCK);
    anim_hide();
    ShowWindow(g_wnd, SW_HIDE);
}

void ui_show(void) { show(); }
void ui_show_if_panel(void) { if (screen_active() && !IsWindowVisible(g_wnd)) show(); }
HWND ui_hwnd(void) { return g_wnd; }
void ui_set_timer(int id, int ms) {
    if (!g_wnd) return;
    if (ms > 0) SetTimer(g_wnd, id, ms, 0);
    else KillTimer(g_wnd, id);
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

void ui_balloon(const wchar_t *title, const wchar_t *msg) { tray_balloon(title, msg); }

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
    else if (id == MENU_SETTINGS) { show(); sysinfo_close(); settings_open(0); }
    else if (id == MENU_RELOAD) ui_restart();
    else if (id == MENU_SHUTDOWN) ui_quit();
}

// ---------------------------------------------------------------- shutdown / restart
static void cleanup(void) {
    bt_shutdown();                         // phone: unregister the Bluetooth service, close sockets
    UnregisterHotKey(g_wnd, HOTKEY_ID);
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    if (g_mutex) { CloseHandle(g_mutex); g_mutex = 0; }
}

void ui_quit(void) { cleanup(); ExitProcess(0); }

// A reload is a brand-new process: clean chat, nothing stuck, window re-centered.
// It carries the mode across (and an optional note to print) on its command line.
void ui_restart_to(int mode, const wchar_t *note) {
    wchar_t exe[MAX_PATH], cmd[MAX_PATH + 600] = L"\"";
    GetModuleFileNameW(0, exe, MAX_PATH);
    s_cat(cmd, exe, MAX_PATH + 600);
    s_cat(cmd, L"\" --restarted --mode ", MAX_PATH + 600);
    s_cat(cmd, mode_name(mode), MAX_PATH + 600);
    if (note && *note) {                       // always last: everything after it is the note
        s_cat(cmd, L" --note ", MAX_PATH + 600);
        s_cat(cmd, note, MAX_PATH + 600);
    }
    // We're the foreground app right now (you just typed). Let the new copy take the
    // foreground too, or Windows opens it behind your other windows and your typing
    // goes to whatever's underneath. Has to happen before cleanup() closes our window.
    AllowSetForegroundWindow(ASFW_ANY);
    cleanup();
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    if (CreateProcessW(0, cmd, 0, 0, FALSE, 0, 0, 0, &si, &pi)) {
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    }
    ExitProcess(0);
}

void ui_restart(void) { ui_restart_to(mode_current(), 0); }

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

// halftone dots that fade out, like the dither in the old Loomy logo.
// With the dot wave on, a slow crest drifts through: dots it passes grow a little and brighten.
#define HALFTONE_Y   S(46)
#define HALFTONE_TOP S(44)
#define HALFTONE_BOT S(74)
static void halftone(HDC dc, int y, int w) {
    int step = S(6), rows = 4, fadeEnd = w * 3 / 4, wavelen = S(260);
    int waving = anim_dots_on();
    for (int r = 0; r < rows; r++) {
        int off = (r & 1) ? step / 2 : 0;
        int rowEnd = fadeEnd - r * fadeEnd / 6;          // each row fades out a bit sooner
        for (int x = off; x < rowEnd; x += step) {
            int size = (S(4) * (rowEnd - x) + rowEnd - 1) / rowEnd;
            COLORREF c = g_accentDim;
            if (waving) {
                int k = anim_wave(x + r * S(14), wavelen);          // rows lag a bit: the crest leans
                size = size * (820 + 380 * k / 1024) / 1024;        // ~80 % .. ~117 %
                if (size < 1) size = 1;
                c = ui_mix(g_accent, g_accentDim, 45 * k / 1024);
            }
            ui_fill(dc, x, y + r * step, x + size, y + r * step + size, c);
        }
    }
}

void ui_invalidate_strip(void) {
    if (!g_wnd) return;
    RECT rc; GetClientRect(g_wnd, &rc);
    RECT s = { 0, HALFTONE_TOP, rc.right, HALFTONE_BOT };
    InvalidateRect(g_wnd, &s, FALSE);
}

// the line glow: a soft bright band sweeps left to right along a 1-px line
static void glow_line(HDC dc, int y, int x0, int x1, COLORREF base, int g) {
    int span = x1 - x0, half = span / 6;
    int head = x0 - half + (span + 2 * half) * g / 1024;
    int fade = g > 800 ? (1024 - g) * 1024 / 224 : 1024;             // dims out at the end
    COLORREF hi = glow_color();
    for (int i = -8; i < 8; i++) {
        int a = head + i * half / 8, b = a + half / 8 + 1;
        if (b <= x0 || a >= x1) continue;
        if (a < x0) a = x0;
        if (b > x1) b = x1;
        int k = (8 - (i < 0 ? -i - 1 : i)) * 1024 / 8 * fade / 1024;  // peak in the middle
        ui_fill(dc, a, y, b, y + 1, ui_mix(hi, base, k * 100 / 1024));
    }
}

static void paint(HWND w) {
    PAINTSTRUCT ps;
    HDC wdc = BeginPaint(w, &ps);
    RECT rc; GetClientRect(w, &rc);
    int W = rc.right, H = rc.bottom;

    // dot-wave frame: only the strip changed, so redraw just that band (the cheap path)
    if (ps.rcPaint.top >= HALFTONE_TOP && ps.rcPaint.bottom <= HALFTONE_BOT) {
        int bh = HALFTONE_BOT - HALFTONE_TOP;
        HDC sdc = CreateCompatibleDC(wdc);
        HBITMAP sb = CreateCompatibleBitmap(wdc, W, bh);
        HGDIOBJ so = SelectObject(sdc, sb);
        SetViewportOrgEx(sdc, 0, -HALFTONE_TOP, 0);
        ui_fill(sdc, 0, HALFTONE_TOP, W, HALFTONE_BOT, C_BG);
        halftone(sdc, HALFTONE_Y, W);
        if (!g_dwmBorder) {
            COLORREF b = border_color();
            ui_fill(sdc, 0, HALFTONE_TOP, 1, HALFTONE_BOT, b);
            ui_fill(sdc, W - 1, HALFTONE_TOP, W, HALFTONE_BOT, b);
        }
        SetViewportOrgEx(sdc, 0, 0, 0);
        BitBlt(wdc, 0, HALFTONE_TOP, W, bh, sdc, 0, 0, SRCCOPY);
        SelectObject(sdc, so); DeleteObject(sb); DeleteDC(sdc);
        EndPaint(w, &ps);
        return;
    }

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
    RECT ck = { W - px - sz.cx - S(8), S(8), W - px + S(8), S(17) + sz.cy + S(8) };   // a forgiving target
    g_clockRect = ck;

    halftone(dc, HALFTONE_Y, W);

    // output lines, newest at the bottom
    if (input_shown() != g_inputShown) layout();   // a panel opened/closed: show or hide the input bar
    int inputTop = g_inputShown ? H - S(52) : H - S(8);
    RECT area = { px, S(84), W - px, inputTop - S(12) };
    SelectObject(dc, g_font);
    IntersectClipRect(dc, area.left, area.top, area.right, area.bottom);
    int screen = settings_active() ? 1 : browser_active() ? 2 : net_active() ? 3 : changelog_active() ? 4 :
                 mode_active() ? 5 : web_active() ? 6 : display_active() ? 7 : sysinfo_active() ? 8 : pkg_active() ? 9 :
                 phone_active() ? 10 : 0;
    anim_paint_begin(dc, screen);          // list glide: the highlight goes under the rows
    if (settings_active()) settings_paint(dc, area);
    else if (browser_active()) browser_paint(dc, area);
    else if (net_active()) net_paint(dc, area);
    else if (changelog_active()) changelog_paint(dc, area);
    else if (mode_active()) mode_paint(dc, area);
    else if (web_active()) web_paint(dc, area);
    else if (display_active()) display_paint(dc, area);
    else if (pkg_active()) pkg_paint(dc, area);
    else if (phone_active()) phone_paint(dc, area);
    else if (sysinfo_active()) sysinfo_paint(dc, area);
    anim_paint_end();
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
        c = anim_text(c, l->born);
        SetTextColor(dc, c);
        DrawTextW(dc, l->text, -1, &r, fmt);
        if (echo) {
            ui_text(dc, area.left, y, L"\x203A", anim_text(g_accent, l->born));
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

    // input bar (hidden while a panel that doesn't take typing is open)
    if (g_inputShown) {
        ui_fill(dc, 0, inputTop, W, H, C_INPUTBG);
        ui_fill(dc, 0, inputTop, W, inputTop + 1, C_RULE);
        int g = anim_glow();
        if (g >= 0) glow_line(dc, inputTop, 0, W, C_RULE, g);
        SelectObject(dc, g_font);
        ui_text(dc, px, inputTop + (S(52) - g_lineH) / 2, L"\x203A", g_accent);
    }

    // fallback border for Windows 10 (Windows 11 draws a rounded one for us)
    if (!g_dwmBorder) {
        COLORREF b = border_color();
        ui_fill(dc, 0, 0, W, 1, b); ui_fill(dc, 0, H - 1, W, H, b);
        ui_fill(dc, 0, 0, 1, H, b); ui_fill(dc, W - 1, 0, W, H, b);
        int g = anim_glow();
        if (g >= 0) { glow_line(dc, 0, 0, W, b, g); glow_line(dc, H - 1, 0, W, b, g); }
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
    } else if (display_active()) {
        if (m == WM_KEYDOWN && display_key(wp)) return 0;
        if (m == WM_CHAR && display_char(wp)) return 0;
    } else if (pkg_active()) {
        if (m == WM_KEYDOWN && pkg_key(wp)) return 0;
        if (m == WM_CHAR && pkg_char(wp)) return 0;
    } else if (phone_active()) {
        if (m == WM_KEYDOWN && phone_key(wp)) return 0;
        if (m == WM_CHAR && phone_char(wp)) return 0;
    } else if (sysinfo_active()) {
        if (m == WM_KEYDOWN && sysinfo_key(wp)) return 0;
        if (m == WM_CHAR && sysinfo_char(wp)) return 0;
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

    case WM_NCLBUTTONDBLCLK: {   // the header is a drag handle, so its double-clicks arrive here
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        ScreenToClient(w, &pt);
        if (wp == HTCAPTION && PtInRect(&g_clockRect, pt)) { ui_hide(); return 0; }
        break;
    }
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
        else if (display_active()) display_click((short)LOWORD(lp), (short)HIWORD(lp), 0);
        else if (pkg_active()) pkg_click((short)LOWORD(lp), (short)HIWORD(lp), 0);
        else if (phone_active()) phone_click((short)LOWORD(lp), (short)HIWORD(lp), 0);
        else if (sysinfo_active()) sysinfo_click((short)LOWORD(lp), (short)HIWORD(lp), 0);
        SetFocus(g_edit);
        return 0;
    case WM_LBUTTONDBLCLK:
        if (browser_active()) browser_click((short)LOWORD(lp), (short)HIWORD(lp), 1);
        else if (mode_active()) mode_click((short)LOWORD(lp), (short)HIWORD(lp), 1);
        else if (web_active()) web_click((short)LOWORD(lp), (short)HIWORD(lp), 1);
        else if (display_active()) display_click((short)LOWORD(lp), (short)HIWORD(lp), 1);
        else if (sysinfo_active()) sysinfo_click((short)LOWORD(lp), (short)HIWORD(lp), 1);
        SetFocus(g_edit);
        return 0;
    case WM_ACTIVATE: if (LOWORD(wp) != WA_INACTIVE) SetFocus(g_edit); return 0;

    case WM_MOUSEWHEEL: {
        if (browser_active()) { browser_wheel((short)HIWORD(wp) / WHEEL_DELTA); return 0; }
        if (net_active()) { net_wheel((short)HIWORD(wp) / WHEEL_DELTA); return 0; }
        if (changelog_active()) { changelog_wheel((short)HIWORD(wp) / WHEEL_DELTA); return 0; }
        if (mode_active()) { mode_wheel((short)HIWORD(wp) / WHEEL_DELTA); return 0; }
        if (web_active()) { web_wheel((short)HIWORD(wp) / WHEEL_DELTA); return 0; }
        if (display_active()) { display_wheel((short)HIWORD(wp) / WHEEL_DELTA); return 0; }
        if (pkg_active()) { pkg_wheel((short)HIWORD(wp) / WHEEL_DELTA); return 0; }
        if (phone_active()) { phone_wheel((short)HIWORD(wp) / WHEEL_DELTA); return 0; }
        if (sysinfo_active()) { sysinfo_wheel((short)HIWORD(wp) / WHEEL_DELTA); return 0; }
        int d = (short)HIWORD(wp) / WHEEL_DELTA * 3;
        g_scroll += d;
        if (g_scroll > g_count - 1) g_scroll = g_count > 0 ? g_count - 1 : 0;
        if (g_scroll < 0) g_scroll = 0;
        InvalidateRect(w, 0, FALSE);
        return 0;
    }

    case WM_HOTKEY: if (wp == HOTKEY_ID) toggle(); return 0;
    case WM_LOOMY_SHOW: show(); return 0;
    case WM_PHONE: bt_event(wp, lp); return 0;
    case WM_TIMER:
        if (wp == TIMER_SCREENS) {
            KillTimer(w, TIMER_SCREENS);
            SendMessageW(w, WM_SYSCOMMAND, SC_MONITORPOWER, 2);   // 2 = off (standby)
        }
        else if (wp == TIMER_ANIM) anim_tick();
        else if (wp == TIMER_PHONE) phone_tick();
        else if (wp == TIMER_NET) { net_tick(); sysinfo_tick(); }
        else if (wp == TIMER_REFOCUS) {
            if (IsWindowVisible(w)) { SetForegroundWindow(w); SetFocus(g_edit); }
            if (--g_refocusLeft <= 0) KillTimer(w, TIMER_REFOCUS);
        }
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

    // a reload passes "--mode <name>" and maybe "--note <text>" (always last)
    static wchar_t args[1024];
    s_cpy(args, cmdline, 1024);
    const wchar_t *note = 0;
    for (wchar_t *p = args; *p; p++)
        if (s_istarts(p, L" --note ")) { *p = 0; note = p + 8; break; }
    int silent = s_has(args, L"--silent");
    int restarted = s_has(args, L"--restarted");

    // only one Loomy at a time: a second launch just pops up the first one
    g_mutex = CreateMutexW(0, TRUE, L"Loomy.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(CLASS_NAME, 0);
        if (other) PostMessageW(other, WM_LOOMY_SHOW, 0, 0);
        ExitProcess(0);
    }

    HDC sdc = GetDC(0); g_dpi = GetDeviceCaps(sdc, LOGPIXELSY); ReleaseDC(0, sdc);

    cfg_load();
    for (wchar_t *p = args; *p; p++)
        if (s_istarts(p, L"--mode ")) {
            wchar_t name[32]; int i = 0;
            p += 7;
            while (*p && *p != L' ' && i < 31) name[i++] = *p++;
            name[i] = 0;
            mode_start(mode_find(name));
            break;
        }

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
    phone_startup();                           // only does anything with /pkg add phone
    if (!g_hotkeyOk) {
        outf(OUT_ERR, L"Couldn't grab %s, another app is using it.", g_cfg.hotkey);
        out(OUT_DIM, L"Pick another in: /config");
        if (silent) tray_balloon(L"Loomy", L"My hotkey is taken by another app. Click my tray icon to open me.");
    }
    if (note && *note) {                       // e.g. "Switched to work mode."
        static wchar_t lines[600];
        s_cpy(lines, note, 600);
        for (wchar_t *p = lines; *p; p++) if (*p == L'|') *p = L'\n';
        out(OUT_OK, lines);
    }
    else if (restarted) out(OUT_OK, L"Reloaded. Fresh as new.");
    if (!silent || !g_hotkeyOk) show();

    MSG msg;
    while (GetMessageW(&msg, 0, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    cleanup();
    ExitProcess(0);
}
