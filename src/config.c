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
        }
        line = next;
    }
}

void cfg_save(void) {
    wchar_t text[512];
    wsprintfW(text,
        L"# Loomy settings. Easiest way to change them: type config in Loomy.\r\n"
        L"hotkey = %s\r\nname = %s\r\naccent = #%02X%02X%02X\r\n",
        g_cfg.hotkey, g_cfg.name,
        GetRValue(g_cfg.accent), GetGValue(g_cfg.accent), GetBValue(g_cfg.accent));
    char utf8[1024];
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
    read_stamp(&g_stamp);
}

int cfg_reload_if_changed(void) {
    FILETIME ft;
    if (!read_stamp(&ft) || CompareFileTime(&ft, &g_stamp) == 0) return 0;
    cfg_load();
    return 1;
}
