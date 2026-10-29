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

#define LOOMY_VERSION L"2.14"
#define GH_USER L"JimmyUns"          // the repo /update and `github` use
#define GH_REPO L"Project-Loomy"

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
void     ui_flush(void);
void     ui_screens_off_soon(void);                   // screens to standby in ~0.5 s
void     ui_glow_border(int k);                       // Windows 11 border glow, 0..1024
void     ui_invalidate_strip(void);                   // repaint just the halftone strip
void     ui_sel_row(HDC dc, int l, int t, int r, int b);   // panels: draw the selected row's highlight

// ---------- anim.c (optional UI animations, /config > Customization) ----------
#define TIMER_ANIM 5
void     anim_show(HWND w);
void     anim_hide(void);
void     anim_tick(void);
void     anim_kick(void);
void     anim_config_changed(void);
int      anim_dots_on(void);
int      anim_wave(int x, int wavelen);               // 0..1024 at the crest, -1 = off
int      anim_glow(void);                             // line glow progress 0..1024, -1 = none
COLORREF anim_text(COLORREF c, ULONGLONG born);
int      anim_glide_active(void);
void     anim_paint_begin(HDC dc, int screen);
void     anim_paint_end(void);                              // paint right now (before a slow call)
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
void ui_restart(void);                                // reload, staying in the current mode
void ui_restart_to(int mode, const wchar_t *note);    // reload into a mode; note ('|' = new line) shows after
void ui_reset_window(void);                           // forget the remembered spot, re-center
void ui_refocus_soon(void);                           // grab focus back after a browser steals it
void ui_set_big(int on);                              // grow to ~1.5x for sysinfo (fits the screen), or back

// ---------- commands.c ----------
void cmd_run(wchar_t *line);
void cmd_complete(wchar_t *line, int cap);            // Tab completion, edits line in place
void cmd_greet(void);
void cmd_update(wchar_t *args);
void cmd_version(wchar_t *args);
void cmd_versionhistory(wchar_t *args);
const wchar_t *folders_dir(void);                     // %APPDATA%\Loomy\Folders
const wchar_t *cmd_typed(void);                       // the command word as typed, e.g. "/m"
void cmd_usage(const wchar_t *shape);                 // prints "Usage: <typed> <shape>"
void complete_add(const wchar_t *candidate, const wchar_t *partial);   // for Tab completion

// ---------- modes.c (home / work / core, the /mode command + picker panel) ----------
#define NMODES 3
enum { MODE_HOME = 1, MODE_WORK = 2, MODE_CORE = 4, MODE_ALL = 7 };  // bit masks: which modes a command lives in
int  mode_current(void);                              // index into the mode list
int  mode_find(const wchar_t *name);                  // -1 if no such mode
const wchar_t *mode_name(int m);
int  mode_is_hidden(int m);
void cmd_mode(wchar_t *args);
int  mode_active(void);
void mode_open(void);
void mode_close(void);
void mode_paint(HDC dc, RECT area);
int  mode_key(WPARAM vk);
int  mode_char(WPARAM ch);
void mode_click(int x, int y, int dbl);
void mode_wheel(int notches);
void mode_start(int m);                               // a reload told us which mode to start in

// ---------- display.c (switch screen layout, like Win+P) ----------
void cmd_display(wchar_t *args);
void display_complete(const wchar_t *partial);
int  display_active(void);
void display_open(void);
void display_close(void);
void display_paint(HDC dc, RECT area);
int  display_key(WPARAM vk);
int  display_char(WPARAM ch);
void display_click(int x, int y, int dbl);
void display_wheel(int notches);

// ---------- web.c (saved websites, per mode) ----------
void cmd_web(wchar_t *args);
void cmd_webconfig(wchar_t *args);
void web_complete(const wchar_t *partial);
void webconfig_complete(wchar_t **words, int n, const wchar_t *partial);
int  web_active(void);
void web_open(int many);                              // many = stay open after each pick
void web_close(void);
void web_paint(HDC dc, RECT area);
int  web_key(WPARAM vk);
int  web_char(WPARAM ch);
void web_click(int x, int y, int dbl);
void web_wheel(int notches);

// ---------- version.c (version compare + is-this-release-mine check) ----------
enum { REL_OK, REL_NO_RELEASE, REL_BAD_AUTHOR, REL_BAD_ASSET, REL_NO_ASSET };
int ver_gt(const wchar_t *a, const wchar_t *b);       // 1 if a is newer: 2.10 > 2.9c > 2.9b > 2.9
int release_check(const char *json, const char *owner, char *tag, int tagCap, wchar_t *ver, int verCap);

// ---------- settings.c (the config screen) ----------
int  settings_active(void);
int  settings_wants_input(void);                     // typing a name/color into the input box
void settings_open(int section);                      // 0 = General, 1 = Customization
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

// ---------- sysdata.c (reads the machine: CPU, every GPU, memory, disk, net, top apps) ----------
// Everything is read without admin and only while the sysinfo panel is open.
// Numbers that Windows won't give us stay -1 ("n/a" on screen).
#define SI_HIST   60        // one sample a second: a minute of graph
#define SI_MAXCPU 64        // logical processors we draw bars for
#define SI_MAXGPU 4
#define SI_MAXENG 6         // engines shown per GPU (busiest first)
#define SI_TOP    5         // top apps by CPU

typedef struct { wchar_t name[24]; int pct; } SiEngine;
typedef struct { wchar_t name[40]; int cpu10; ULONGLONG mem; } SiProc;   // cpu10 = tenths of a %

typedef struct {
    wchar_t name[80];
    int  integrated;                      // 1 = shares system RAM (laptop iGPU)
    int  util;                            // % (busiest engine, like Task Manager)
    ULONGLONG memUsed, memTotal;   // VRAM, or shared RAM for an integrated GPU
    int  tempC, tempWarnC;                // -1 = not reported
    int  clkMHz, clkMaxMHz;               // -1 = not reported
    int  neng; SiEngine eng[SI_MAXENG];
    unsigned char hist[SI_HIST];
} SiGpu;

typedef struct {
    int  ready;                           // 1 once a full second has been measured
    // CPU
    wchar_t cpuName[64];
    int  cores, logical, baseMHz;
    int  cpuUtil;                         // %
    int  speedMHz, perfPct;               // current clock and % of base; -1 = unknown
    int  queue10;                         // threads waiting for a core, tenths (smoothed)
    int  dpc;                             // % of CPU time in drivers (DPC + interrupts); -1 = unknown
    int  procs, threads, handles;
    ULONGLONG uptimeMs;
    int  nthread; unsigned char thread[SI_MAXCPU];
    unsigned char cpuHist[SI_HIST];
    // memory
    ULONGLONG memTotal, memUsed, commit, commitLimit;
    int  hardFaults;                      // pages read back from disk per second; -1 = unknown
    unsigned char memHist[SI_HIST];
    // disk + network (bytes per second)
    int  diskActive;                      // %, -1 = unknown
    ULONGLONG diskRead, diskWrite, netDown, netUp;
    // GPUs, as many as Windows lists (software-only adapters skipped)
    int  ngpu; SiGpu gpu[SI_MAXGPU];
    // apps, grouped by name (all the chrome.exe processes = one row)
    int  ntop; SiProc top[SI_TOP];
    int  selfCpu10; ULONGLONG selfMem;   // what Loomy itself costs
} SiSnap;

int  sysdata_open(SiSnap *s);             // set up + fill the fixed info (names, GPU list); 0 = failed
void sysdata_sample(SiSnap *s);           // call once a second
void sysdata_close(void);                 // free everything (back to zero cost)

// ---------- sysinfo.c (the `sysinfo` / `si` command, picker and live panel) ----------
enum { FLOW_SMOOTH, FLOW_STRAINED, FLOW_CHOKING };
typedef struct { int score, level; wchar_t why[112]; } SiFlow;
void flow_cpu(const SiSnap *s, SiFlow *f);
void flow_gpu(const SiGpu *g, SiFlow *f);

enum { SIV_GENERAL = 0, SIV_CPU = 1, SIV_GPU = 2 };   // view; GPU views are SIV_GPU + index
void cmd_sysinfo(wchar_t *args);
void sysinfo_complete(wchar_t **words, int n, const wchar_t *partial);
int  sysinfo_active(void);
int  sysinfo_expanded(void);              // 1 = the big live view (window is enlarged)
void sysinfo_open(int view);              // -1 = the picker
void sysinfo_close(void);
void sysinfo_tick(void);
void sysinfo_paint(HDC dc, RECT area);
int  sysinfo_key(WPARAM vk);
int  sysinfo_char(WPARAM ch);
void sysinfo_click(int x, int y, int dbl);
void sysinfo_wheel(int notches);

// sysview.c: drawing the big view (state lives in sysinfo.c)
typedef struct { int view; const SiSnap *snap; RECT tabs[2 + SI_MAXGPU]; int ntabs; } SiView;
void sysview_paint(HDC dc, RECT area, SiView *v);
void sysview_fonts(int on);               // make / free the extra fonts (only while open)

// ---------- config.c ----------
typedef struct {
    wchar_t  hotkey[64];
    wchar_t  name[64];
    COLORREF accent;
    int      modeMain;      // the mode Loomy starts in
    unsigned modeHidden;    // bit per mode: 1 = hidden from the /mode list
    int      rememberPos;   // 1 = show up where it was last hidden
    int      anim;          // ANIM_OFF / ANIM_AUTO (skip when the PC is busy) / ANIM_ON
    int      animDots, animLines, animSelect, animText;   // each effect on/off
    int      fadeIn;        // fade the window in on open (off by default)
    int      fadeMs;        // how long that fade takes
} Config;
enum { ANIM_OFF, ANIM_AUTO, ANIM_ON };
#define FADE_MIN  100
#define FADE_MAX  800
#define FADE_STEP 50
#define FADE_DEF  300
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
int      s_words(wchar_t *s, wchar_t **w, int max);    // split in place, returns the true count
int      s_count_words(const wchar_t *s);

#endif
