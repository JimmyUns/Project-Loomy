// loomy.h - shared stuff every file can see
#ifndef LOOMY_H
#define LOOMY_H

#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <stdarg.h>

#define LOOMY_VERSION L"2.7"

// ---------- palette (accent comes from settings) ----------
#define C_BG      RGB(0x13, 0x09, 0x1C)
#define C_INPUTBG RGB(0x1A, 0x0E, 0x27)
#define C_RULE    RGB(0x2C, 0x1B, 0x3E)
#define C_TEXT    RGB(0xD9, 0xCE, 0xE6)
#define C_BRIGHT  RGB(0xF4, 0xEE, 0xFB)
#define C_DIM     RGB(0x84, 0x73, 0x99)
#define C_OK      RGB(0x8E, 0xE3, 0xB8)
#define C_ERR     RGB(0xFF, 0x7C, 0x96)

// ---------- output line styles ----------
enum { OUT_TEXT, OUT_DIM, OUT_ACCENT, OUT_OK, OUT_ERR, OUT_ECHO };

// ---------- ui.c ----------
extern COLORREF g_accent;
extern HFONT    g_font, g_fontSmall;
extern int      g_lineH;
int      S(int px);                                   // scale for screen DPI
void     ui_fill(HDC dc, int l, int t, int r, int b, COLORREF c);
int      ui_text(HDC dc, int x, int y, const wchar_t *t, COLORREF c);   // returns width
COLORREF ui_mix(COLORREF a, COLORREF b, int pctA);
void     ui_redraw(void);
void     ui_apply_colors(void);
void     ui_set_input(const wchar_t *t);
void     ui_get_input(wchar_t *buf, int cap);
int      ui_register_hotkey(void);                    // 1 = ok
void     ui_unregister_hotkey(void);
int      ui_hotkey_ok(void);

void out(int kind, const wchar_t *text);              // print a line (\n splits lines)
void outf(int kind, const wchar_t *fmt, ...);         // printf-style (wsprintf rules, 1024 chars max)
void ui_clear(void);
void ui_set_timer_1s(int on);
void ui_hide(void);
void ui_quit(void);
void ui_restart(void);

// ---------- commands.c ----------
void cmd_run(wchar_t *line);
void cmd_complete(wchar_t *line, int cap);            // Tab completion, edits line in place
void cmd_greet(void);
void cmd_update(wchar_t *args);
void cmd_version(wchar_t *args);
void cmd_versionhistory(wchar_t *args);
const wchar_t *folders_dir(void);                     // %APPDATA%\Loomy\Folders

// ---------- settings.c (the config screen) ----------
int  settings_active(void);
void settings_open(void);
void settings_close(void);
void settings_paint(HDC dc, RECT area);
int  settings_key(WPARAM vk);                         // 1 = handled
int  settings_char(WPARAM ch);                        // 1 = swallow it
void settings_click(int x, int y);
int  startup_get(void);
void startup_set(int on);

// ---------- browser.c (the lp folder list) ----------
int  browser_active(void);
void browser_open(const wchar_t *root);
void browser_close(void);
void browser_paint(HDC dc, RECT area);
int  browser_key(WPARAM vk);
int  browser_char(WPARAM ch);
void browser_click(int x, int y, int dbl);
void browser_wheel(int notches);

// ---------- changelog.c (the /verc panel) ----------
int  changelog_active(void);
void changelog_open(const wchar_t *arg);
void changelog_close(void);
void changelog_paint(HDC dc, RECT area);
int  changelog_key(WPARAM vk);
int  changelog_char(WPARAM ch);
void changelog_click(int x, int y);
void changelog_wheel(int notches);

// ---------- net.c (the `net` connections screen) ----------
int  net_active(void);
void net_open(void);
void net_close(void);
void net_tick(void);
void net_paint(HDC dc, RECT area);
int  net_key(WPARAM vk);
int  net_char(WPARAM ch);
void net_click(int x, int y);
void net_wheel(int notches);

// ---------- config.c ----------
typedef struct {
    wchar_t  hotkey[64];
    wchar_t  name[64];
    COLORREF accent;
} Config;
extern Config g_cfg;

void cfg_load(void);
void cfg_save(void);
int  cfg_reload_if_changed(void);                     // 1 if it reloaded
const wchar_t *cfg_dir(void);                         // %APPDATA%\Loomy

// ---------- util.c ----------
int      s_len(const wchar_t *s);
void     s_cpy(wchar_t *d, const wchar_t *s, int cap);
void     s_cat(wchar_t *d, const wchar_t *s, int cap);
int      s_ieq(const wchar_t *a, const wchar_t *b);
int      s_istarts(const wchar_t *s, const wchar_t *prefix);
int      s_has(const wchar_t *s, const wchar_t *needle);
wchar_t  s_low(wchar_t c);
void     s_lower(wchar_t *s);
wchar_t *s_trim(wchar_t *s);
wchar_t *s_split(wchar_t *s);                          // cuts first word, returns the rest
int      s_hex(const wchar_t *s, unsigned *out);

#endif
