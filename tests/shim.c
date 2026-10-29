#include <windows.h>
#include <locale.h>
#include <time.h>
// ---- files (paths are wide; convert to utf-8)
static void w2u(const wchar_t *w, char *o, int cap) { wcstombs(o, w, cap); }
HANDLE CreateFileW(const wchar_t *p, DWORD acc, DWORD sh, void *sa, DWORD disp, DWORD fl, void *t) {
    char path[1024]; w2u(p, path, 1024);
    FILE *f = fopen(path, disp == CREATE_ALWAYS ? "wb" : "rb");
    return f ? (HANDLE)f : INVALID_HANDLE_VALUE;
}
BOOL ReadFile(HANDLE h, void *b, DWORD n, DWORD *got, void *o) { *got = fread(b, 1, n, (FILE*)h); return 1; }
BOOL WriteFile(HANDLE h, const void *b, DWORD n, DWORD *w, void *o) { *w = fwrite(b, 1, n, (FILE*)h); return *w == n; }
DWORD GetFileSize(HANDLE h, DWORD *hi) { FILE *f = h; long c = ftell(f); fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, c, SEEK_SET); return n; }
BOOL CloseHandle(HANDLE h) { fclose((FILE*)h); return 1; }
HANDLE GetProcessHeap(void) { return (HANDLE)1; }
void *HeapAlloc(HANDLE h, DWORD f, size_t n) { return malloc(n); }
BOOL HeapFree(HANDLE h, DWORD f, void *p) { free(p); return 1; }
int MultiByteToWideChar(UINT cp, DWORD f, const char *s, int n, wchar_t *o, int cap) {
    char *tmp = malloc(n + 1); memcpy(tmp, s, n); tmp[n] = 0;
    size_t r = mbstowcs(o, tmp, cap); free(tmp); return (int)r;
}
int WideCharToMultiByte(UINT cp, DWORD f, const wchar_t *s, int n, char *o, int cap, void *a, void *b) {
    int whole = n < 0; if (whole) n = wcslen(s) + 1;
    wchar_t *tmp = malloc((n + 1) * sizeof(wchar_t));
    memcpy(tmp, s, n * sizeof(wchar_t)); tmp[n] = 0;
    size_t r = wcstombs(o, tmp, cap); free(tmp); return (int)r + whole;   // like Win32: -1 input counts the 0
}
static int g_tick; void GetLocalTime(SYSTEMTIME *t) { t->wYear=2026; t->wMonth=10; t->wDay=21; t->wHour=14; t->wMinute=3 + (g_tick++ / 4) % 50; }
wchar_t g_opened[64][512]; int g_nopened;
INT_PTR ShellExecuteW(HWND h, const wchar_t *v, const wchar_t *f, const wchar_t *p, const wchar_t *d, int s) {
    wcscpy(g_opened[g_nopened++], f); return 42;
}
void Sleep(DWORD ms) {}
static void fixfmt(const wchar_t *in, wchar_t *o) {   // wsprintf %s is wide; glibc wants %ls
    while (*in) {
        if (*in == L'%') { *o++ = *in++;
            if (*in == L'%') { *o++ = *in++; continue; }
            while (*in && wcschr(L"0123456789-.", *in)) *o++ = *in++;
            if (*in == L's') { *o++ = L'l'; }
        }
        *o++ = *in++;
    }
    *o = 0;
}
int wvsprintfW(wchar_t *b, const wchar_t *f, va_list ap) { wchar_t ff[2048]; fixfmt(f, ff); return vswprintf(b, 1025, ff, ap); }
int wsprintfW(wchar_t *b, const wchar_t *f, ...) { va_list ap; va_start(ap, f); int r = wvsprintfW(b, f, ap); va_end(ap); return r; }
DWORD ExpandEnvironmentStringsW(const wchar_t *s, wchar_t *o, DWORD n) { wcscpy(o, L"/tmp/loomytest"); return 1; }
BOOL CreateDirectoryW(const wchar_t *p, void *s) { return 0; }
BOOL GetFileAttributesExW(const wchar_t *p, int l, void *d) { return 0; }
LONG CompareFileTime(const FILETIME *a, const FILETIME *b) { return 0; }
HGDIOBJ SelectObject(HDC d, HGDIOBJ o) { return 0; }
int DrawTextW(HDC d, const wchar_t *t, int n, RECT *r, UINT f) { return 0; }
BOOL GetTextExtentPoint32W(HDC d, const wchar_t *t, int n, SIZE *s) { s->cx = n * 8; s->cy = 14; return 1; }
COLORREF SetTextColor(HDC d, COLORREF c) { return 0; }
HANDLE FindFirstFileW(const wchar_t *p, WIN32_FIND_DATAW *d) { return INVALID_HANDLE_VALUE; }
BOOL FindNextFileW(HANDLE h, WIN32_FIND_DATAW *d) { return 0; }
BOOL FindClose(HANDLE h) { return 1; }
DWORD GetModuleFileNameW(void *m, wchar_t *b, DWORD n) { wcscpy(b, L"C:\\loomy.exe"); return 1; }
// fake screens: g_topo is what Windows is on; g_screens = how many are plugged in
int g_topo = SDC_TOPOLOGY_INTERNAL, g_screens = 2, g_setcalls, g_screensOff;
void ui_screens_off_soon(void) { g_screensOff++; }
LONG GetDisplayConfigBufferSizes(UINT32 f, UINT32 *np, UINT32 *nm) { *np = g_screens; *nm = g_screens * 2; return 0; }
LONG QueryDisplayConfig(UINT32 f, UINT32 *np, DISPLAYCONFIG_PATH_INFO *p, UINT32 *nm, DISPLAYCONFIG_MODE_INFO *m, DISPLAYCONFIG_TOPOLOGY_ID *t) { *t = g_topo; return 0; }
LONG SetDisplayConfig(UINT32 a, void *b, UINT32 c, void *d, UINT32 flags) {
    g_setcalls++;
    if (!(flags & SDC_APPLY)) return 87;
    int topo = flags & 15;
    if (g_screens < 2 && topo != SDC_TOPOLOGY_INTERNAL) return 31;   // ERROR_GEN_FAILURE, like a lone screen
    g_topo = topo; return 0;
}

// ---- ui.c stand-ins
#include "../src/loomy.h"
COLORREF g_accent; HFONT g_font, g_fontSmall; int g_lineH = 16;
wchar_t g_out[4000][1100]; int g_nout;
int S(int px) { return px; }
void ui_fill(HDC dc, int l, int t, int r, int b, COLORREF c) {}
void ui_sel_row(HDC dc, int l, int t, int r, int b) {}
void anim_config_changed(void) {}
int ui_text(HDC dc, int x, int y, const wchar_t *t, COLORREF c) { return wcslen(t) * 8; }
COLORREF ui_mix(COLORREF a, COLORREF b, int p) { return 0; }
void ui_redraw(void) {}
void ui_set_input(const wchar_t *t) {}
int g_hidden;
void ui_hide(void) { g_hidden = 1; }
void ui_clear(void) {}
void ui_quit(void) {}
// a real reload exits and starts a new process; here we note it and "start" in that mode
int g_restarts; wchar_t g_restartNote[600];
void ui_restart_to(int mode, const wchar_t *note) {
    g_restarts++; wcscpy(g_restartNote, note ? note : L"");
    out(OUT_OK, L"[reload]"); if (note && *note) { wchar_t t[700]; swprintf(t, 700, L"[note: %ls]", note); out(OUT_DIM, t); }
    mode_start(mode);
}
void ui_restart(void) { ui_restart_to(mode_current(), 0); }
int g_resets; void ui_reset_window(void) { g_resets++; }
int g_refocus; void ui_refocus_soon(void) { g_refocus++; }
static const char *KIND[] = { "TXT", "DIM", "ACC", "OK ", "ERR", ">  " };
void out(int kind, const wchar_t *t) { swprintf(g_out[g_nout++ % 4000], 1100, L"%s %ls", KIND[kind], t); }
void outf(int kind, const wchar_t *f, ...) { wchar_t b[1100]; va_list ap; va_start(ap, f); wvsprintfW(b, f, ap); va_end(ap); out(kind, b); }
int settings_active(void){return 0;} int g_settingsSec = -1; void settings_open(int s){ g_settingsSec = s; } void settings_close(void){}
void net_open(void){} void browser_open(const wchar_t *r){} void changelog_open(const wchar_t *a){ outf(OUT_TEXT, L"[changelog_open '%s']", a); }
void cmd_update(wchar_t *a){} void cmd_version(wchar_t *a){ out(OUT_TEXT, L"[version ran]"); } void cmd_versionhistory(wchar_t *a){}

// ---- sysinfo stand-ins: fake sensors (sysdata.c is Windows-only) and the window growing
void browser_close(void){} void net_close(void){} void changelog_close(void){}
int g_shift; SHORT GetKeyState(int k) { return k == VK_SHIFT && g_shift ? -1 : 0; }
int g_timer, g_big, g_sysOpen, g_sysSamples, g_fonts;
void ui_set_timer_1s(int on) { g_timer = on; }
void ui_set_big(int on) { g_big = on; }
void sysview_fonts(int on) { g_fonts = on; }
void sysview_paint(HDC dc, RECT a, SiView *v) { v->ntabs = 2 + v->snap->ngpu; }
int g_fakeGpus = 2;
int sysdata_open(SiSnap *s) {
    memset(s, 0, sizeof(*s)); g_sysOpen = 1;
    wcscpy(s->cpuName, L"Fake CPU"); s->logical = 16; s->cores = 8; s->nthread = 16;
    s->ngpu = g_fakeGpus;
    for (int i = 0; i < s->ngpu; i++) { swprintf(s->gpu[i].name, 80, L"Fake GPU %d", i + 1); s->gpu[i].integrated = i == 1; }
    return 1;
}
void sysdata_sample(SiSnap *s) { g_sysSamples++; s->ready = 1; }
void sysdata_close(void) { g_sysOpen = 0; }
