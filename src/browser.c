// browser.c - the folder list you get from typing lp
// Up/Down picks, Right goes inside, Left goes back, Enter opens it in Explorer, Esc closes.
#include "loomy.h"
#include <shellapi.h>

#define MAX_ITEMS 256
#define NAME_LEN  128
#define MAX_DEPTH 32

typedef struct { wchar_t name[NAME_LEN]; int hasSub; } Item;

static int     g_active;
static wchar_t g_cur[MAX_PATH];
static Item    g_items[MAX_ITEMS];
static int     g_n, g_sel, g_top, g_vis = 8;
static int     g_back[MAX_DEPTH], g_depth;          // remembers where you were when going back
static int     g_listTop, g_rowStep;                 // for mouse clicks

static int has_subfolder(const wchar_t *dir) {
    wchar_t pat[MAX_PATH];
    s_cpy(pat, dir, MAX_PATH); s_cat(pat, L"\\*", MAX_PATH);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int found = 0;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != L'.') { found = 1; break; }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

static int name_less(const wchar_t *a, const wchar_t *b) {
    while (*a && s_low(*a) == s_low(*b)) { a++; b++; }
    return s_low(*a) < s_low(*b);
}

static void load(void) {
    g_n = 0; g_sel = 0; g_top = 0;
    wchar_t pat[MAX_PATH];
    s_cpy(pat, g_cur, MAX_PATH); s_cat(pat, L"\\*", MAX_PATH);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;   // folders only
        if (fd.cFileName[0] == L'.' || (fd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM)) continue;
        if (g_n >= MAX_ITEMS || s_len(fd.cFileName) >= NAME_LEN) continue;
        // insert sorted A-Z
        int i = g_n++;
        while (i > 0 && name_less(fd.cFileName, g_items[i - 1].name)) { g_items[i] = g_items[i - 1]; i--; }
        s_cpy(g_items[i].name, fd.cFileName, NAME_LEN);
        wchar_t full[MAX_PATH];
        s_cpy(full, g_cur, MAX_PATH); s_cat(full, L"\\", MAX_PATH); s_cat(full, fd.cFileName, MAX_PATH);
        g_items[i].hasSub = has_subfolder(full);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static void keep_visible(void) {
    if (g_sel < g_top) g_top = g_sel;
    if (g_sel >= g_top + g_vis) g_top = g_sel - g_vis + 1;
    if (g_top < 0) g_top = 0;
}

static void move(int d) {
    if (!g_n) return;
    g_sel += d;
    if (g_sel < 0) g_sel = 0;
    if (g_sel > g_n - 1) g_sel = g_n - 1;
    keep_visible();
    ui_redraw();
}

static void go_inside(void) {
    if (!g_n || g_depth >= MAX_DEPTH) return;
    if (s_len(g_cur) + s_len(g_items[g_sel].name) + 2 >= MAX_PATH) return;
    g_back[g_depth++] = g_sel;
    s_cat(g_cur, L"\\", MAX_PATH);
    s_cat(g_cur, g_items[g_sel].name, MAX_PATH);
    load();
    ui_redraw();
}

static void go_back(void) {
    if (!g_depth) return;                 // already at the top
    int n = s_len(g_cur);
    while (n > 0 && g_cur[n - 1] != L'\\') n--;
    if (n > 0) g_cur[n - 1] = 0;
    load();
    g_sel = g_back[--g_depth];
    if (g_sel > g_n - 1) g_sel = g_n > 0 ? g_n - 1 : 0;
    keep_visible();
    ui_redraw();
}

static void open_selected(void) {
    wchar_t path[MAX_PATH];
    s_cpy(path, g_cur, MAX_PATH);
    if (g_n) { s_cat(path, L"\\", MAX_PATH); s_cat(path, g_items[g_sel].name, MAX_PATH); }
    ShellExecuteW(0, L"open", L"explorer.exe", path, 0, SW_SHOWNORMAL);
    ui_hide();
}

// ---------------------------------------------------------------- open / close
int browser_active(void) { return g_active; }

void browser_open(const wchar_t *root) {
    settings_close();
    s_cpy(g_cur, root, MAX_PATH);
    g_depth = 0;
    load();
    g_active = 1;
    ui_set_input(L"");
    ui_redraw();
}

void browser_close(void) {
    if (!g_active) return;
    g_active = 0;
    ui_redraw();
}

// ---------------------------------------------------------------- input
int browser_key(WPARAM vk) {
    switch (vk) {
    case VK_UP:    move(-1); break;
    case VK_DOWN:  move(+1); break;
    case VK_PRIOR: move(-g_vis); break;
    case VK_NEXT:  move(+g_vis); break;
    case VK_HOME:  move(-MAX_ITEMS); break;
    case VK_END:   move(+MAX_ITEMS); break;
    case VK_RIGHT: case VK_TAB: go_inside(); break;
    case VK_LEFT:  case VK_BACK: go_back(); break;
    case VK_RETURN: open_selected(); break;
    case VK_ESCAPE: browser_close(); break;
    }
    return 1;
}

int browser_char(WPARAM ch) {
    // type a letter to jump to the next folder starting with it
    if (ch > 32 && g_n) {
        for (int k = 1; k <= g_n; k++) {
            int i = (g_sel + k) % g_n;
            if (s_low(g_items[i].name[0]) == s_low((wchar_t)ch)) { g_sel = i; keep_visible(); ui_redraw(); break; }
        }
    }
    return 1;
}

static int row_at(int y) {
    if (!g_rowStep || y < g_listTop) return -1;
    int i = g_top + (y - g_listTop) / g_rowStep;
    return (i < g_n && i < g_top + g_vis) ? i : -1;
}

void browser_click(int x, int y, int dbl) {
    int i = row_at(y);
    if (i < 0) return;
    g_sel = i;
    ui_redraw();
    if (dbl) open_selected();
}

void browser_wheel(int notches) { move(-notches); }

// ---------------------------------------------------------------- drawing
void browser_paint(HDC dc, RECT a) {
    SelectObject(dc, g_font);
    int y = a.top;
    int w = ui_text(dc, a.left, y, L"Loomy's Path", g_accent);

    // breadcrumb: Folders > proj > sub
    wchar_t crumb[MAX_PATH] = L"Folders";
    const wchar_t *rel = g_cur + s_len(folders_dir());
    for (; *rel; rel++) {
        if (*rel == L'\\') s_cat(crumb, L" \x203A ", MAX_PATH);
        else { wchar_t c[2] = { *rel, 0 }; s_cat(crumb, c, MAX_PATH); }
    }
    ui_text(dc, a.left + w + S(16), y, crumb, C_DIM);
    y += g_lineH + S(14);

    int rowH = g_lineH + S(10);
    g_rowStep = rowH + S(2);
    g_listTop = y;
    int bottom = a.bottom - S(26);
    g_vis = (bottom - y) / g_rowStep;
    if (g_vis < 1) g_vis = 1;
    keep_visible();

    if (!g_n) {
        ui_text(dc, a.left + S(10), y + S(4), g_depth ? L"No folders in here." : L"No folders yet.", C_TEXT);
        ui_text(dc, a.left + S(10), y + S(4) + g_lineH + S(6),
                g_depth ? L"Enter opens this one, \x2190 goes back." : L"Make one with: lp <name>", C_DIM);
    }

    for (int i = g_top; i < g_n && i < g_top + g_vis; i++) {
        int sel = i == g_sel;
        if (sel) ui_sel_row(dc, a.left, y, a.right, y + rowH);
        int ty = y + (rowH - g_lineH) / 2;
        if (sel) ui_text(dc, a.left + S(10), ty, L"\x203A", g_accent);
        ui_text(dc, a.left + S(28), ty, g_items[i].name, sel ? C_BRIGHT : C_TEXT);
        if (g_items[i].hasSub) {
            SIZE sz; GetTextExtentPoint32W(dc, L"\x2192", 1, &sz);
            ui_text(dc, a.right - S(12) - sz.cx, ty, L"\x2192", sel ? g_accent : C_DIM);
        }
        y += g_rowStep;
    }

    // little scroll hint when the list is longer than the screen
    if (g_n > g_vis) {
        wchar_t pos[32]; wsprintfW(pos, L"%d/%d", g_sel + 1, g_n);
        SelectObject(dc, g_fontSmall);
        SIZE sz; GetTextExtentPoint32W(dc, pos, s_len(pos), &sz);
        ui_text(dc, a.right - sz.cx, a.bottom - S(16), pos, C_DIM);
    }

    SelectObject(dc, g_fontSmall);
    ui_text(dc, a.left, a.bottom - S(16),
            L"\x2191\x2193 choose    \x2192 inside    \x2190 back    Enter open    Esc close", C_DIM);
    SelectObject(dc, g_font);
}
