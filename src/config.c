// config.c - settings live in %APPDATA%\Loomy\loomy.cfg (plain text, key = value)
// You normally change them through the config screen, not by hand.
#include "loomy.h"

Config g_cfg;

static wchar_t  g_dir[MAX_PATH];
static wchar_t  g_file[MAX_PATH];
static FILETIME g_stamp;

const wchar_t *cfg_dir(void) { return g_dir; }

static void set_defaults(void) {
    s_cpy(g_cfg.hotkey, L"ctrl+alt+space", 64);
    g_cfg.name[0] = 0;
    g_cfg.accent = RGB(0xA8, 0x55, 0xF7);
    g_cfg.modeMain = 0;
    g_cfg.modeHidden = 0;
    g_cfg.rememberPos = 0;
    g_cfg.anim = ANIM_AUTO;
    g_cfg.animDots = g_cfg.animLines = g_cfg.animSelect = g_cfg.animText = 1;
    g_cfg.fadeIn = 0;
    g_cfg.fadeMs = FADE_DEF;
}

static int is_on(const wchar_t *v) {
    return s_ieq(v, L"on") || s_ieq(v, L"1") || s_ieq(v, L"true") || s_ieq(v, L"yes");
}
static const wchar_t *onoff(int v) { return v ? L"on" : L"off"; }

// never let every mode be hidden, and never let main be a hidden one
static void fix_modes(void) {
    unsigned all = (1u << NMODES) - 1;
    if (g_cfg.modeMain < 0 || g_cfg.modeMain >= NMODES) g_cfg.modeMain = 0;
    g_cfg.modeHidden &= all;
    if (g_cfg.modeHidden == all) g_cfg.modeHidden &= ~(1u << g_cfg.modeMain);
    if (g_cfg.modeHidden & (1u << g_cfg.modeMain))
        for (int i = 0; i < NMODES; i++)
            if (!(g_cfg.modeHidden & (1u << i))) { g_cfg.modeMain = i; break; }
}

static int read_stamp(FILETIME *ft) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(g_file, GetFileExInfoStandard, &a)) return 0;
    *ft = a.ftLastWriteTime;
    return 1;
}

static void parse(wchar_t *text) {
    wchar_t *line = text;
    while (line && *line) {
        wchar_t *next = line;
        while (*next && *next != L'\n') next++;
        if (*next) *next++ = 0; else next = 0;

        wchar_t *l = s_trim(line), *eq = l;
        while (*eq && *eq != L'=') eq++;
        if (*l != L'#' && *eq) {
            *eq = 0;
            wchar_t *k = s_trim(l), *v = s_trim(eq + 1);
            unsigned hex;
            if (s_ieq(k, L"hotkey") && *v) { s_cpy(g_cfg.hotkey, v, 64); s_lower(g_cfg.hotkey); }
            else if (s_ieq(k, L"name")) s_cpy(g_cfg.name, v, 64);
            else if (s_ieq(k, L"accent") && s_hex(v, &hex))
                g_cfg.accent = RGB((hex >> 16) & 255, (hex >> 8) & 255, hex & 255);
            else if (s_ieq(k, L"remember_position")) g_cfg.rememberPos = is_on(v);
            else if (s_ieq(k, L"animations"))
                g_cfg.anim = s_ieq(v, L"off") ? ANIM_OFF : s_ieq(v, L"on") ? ANIM_ON : ANIM_AUTO;
            else if (s_ieq(k, L"anim_dots"))   g_cfg.animDots = is_on(v);
            else if (s_ieq(k, L"anim_lines"))  g_cfg.animLines = is_on(v);
            else if (s_ieq(k, L"anim_select")) g_cfg.animSelect = is_on(v);
            else if (s_ieq(k, L"anim_text"))   g_cfg.animText = is_on(v);
            else if (s_ieq(k, L"fade_in"))     g_cfg.fadeIn = is_on(v);
            else if (s_ieq(k, L"fade_ms")) {
                int n = 0, ok = *v != 0;
                for (wchar_t *p = v; *p; p++) {
                    if (*p < L'0' || *p > L'9') { ok = 0; break; }
                    if (n < 100000) n = n * 10 + (*p - L'0');      // huge numbers just clamp below
                }
                if (ok) g_cfg.fadeMs = n < FADE_MIN ? FADE_MIN : n > FADE_MAX ? FADE_MAX : n;
            }
            else if (s_ieq(k, L"mode_main")) {
                int m = mode_find(v);
                if (m >= 0) g_cfg.modeMain = m;
            }
            else if (s_ieq(k, L"mode_hidden")) {         // "work" or "home, work"
                g_cfg.modeHidden = 0;
                for (wchar_t *p = v; *p; p++) if (*p == L',') *p = L' ';
                wchar_t *w[NMODES + 4];
                int n = s_words(v, w, NMODES + 4);
                for (int i = 0; i < n && i < NMODES + 4; i++) {
                    int m = mode_find(w[i]);
                    if (m >= 0) g_cfg.modeHidden |= 1u << m;
                }
            }
        }
        line = next;
    }
}

void cfg_save(void) {
    wchar_t hidden[64] = L"";
    for (int i = 0; i < NMODES; i++)
        if (g_cfg.modeHidden & (1u << i)) {
            if (hidden[0]) s_cat(hidden, L", ", 64);
            s_cat(hidden, mode_name(i), 64);
        }
    wchar_t text[1024];
    int n0 = wsprintfW(text,
        L"# Loomy settings. Easiest way to change them: type /config or /mode in Loomy.\r\n"
        L"hotkey = %s\r\nname = %s\r\naccent = #%02X%02X%02X\r\n"
        L"remember_position = %s\r\nmode_main = %s\r\nmode_hidden = %s\r\n",
        g_cfg.hotkey, g_cfg.name,
        GetRValue(g_cfg.accent), GetGValue(g_cfg.accent), GetBValue(g_cfg.accent),
        onoff(g_cfg.rememberPos), mode_name(g_cfg.modeMain), hidden);
    wsprintfW(text + n0,
        L"animations = %s\r\nanim_dots = %s\r\nanim_lines = %s\r\nanim_select = %s\r\n"
        L"anim_text = %s\r\nfade_in = %s\r\nfade_ms = %d\r\n",
        g_cfg.anim == ANIM_OFF ? L"off" : g_cfg.anim == ANIM_ON ? L"on" : L"auto",
        onoff(g_cfg.animDots), onoff(g_cfg.animLines), onoff(g_cfg.animSelect),
        onoff(g_cfg.animText), onoff(g_cfg.fadeIn), g_cfg.fadeMs);
    char utf8[2048];
    int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, sizeof(utf8), 0, 0) - 1;
    HANDLE f = CreateFileW(g_file, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD w; WriteFile(f, utf8, n > 0 ? n : 0, &w, 0);
    CloseHandle(f);
    read_stamp(&g_stamp);
}

void cfg_load(void) {
    static char    raw[8 * 1024];
    static wchar_t wide[8 * 1024];

    if (!g_dir[0]) {
        ExpandEnvironmentStringsW(L"%APPDATA%\\Loomy", g_dir, MAX_PATH);
        s_cpy(g_file, g_dir, MAX_PATH);
        s_cat(g_file, L"\\loomy.cfg", MAX_PATH);
        CreateDirectoryW(g_dir, 0);
    }
    set_defaults();

    HANDLE f = CreateFileW(g_file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0,
                           OPEN_EXISTING, 0, 0);
    if (f == INVALID_HANDLE_VALUE) { cfg_save(); return; }   // first run

    DWORD n = 0;
    ReadFile(f, raw, sizeof(raw) - 1, &n, 0);
    CloseHandle(f);

    char *src = raw;
    if (n >= 3 && (unsigned char)raw[0] == 0xEF && (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF) {
        src += 3; n -= 3;
    }
    int w = MultiByteToWideChar(CP_UTF8, 0, src, (int)n, wide, 8 * 1024 - 1);
    wide[w] = 0;
    parse(wide);
    fix_modes();
    read_stamp(&g_stamp);
}

int cfg_reload_if_changed(void) {
    FILETIME ft;
    if (!read_stamp(&ft) || CompareFileTime(&ft, &g_stamp) == 0) return 0;
    cfg_load();
    return 1;
}
