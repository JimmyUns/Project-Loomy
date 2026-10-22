// changelog.c - the /verc panel. Two levels: a list of versions, and a detail view
// showing one version's changes. Up/Down move, Enter opens, Esc backs out.
//
// The entries are baked in at build time (they never change after release), so the panel
// is instant and works offline. Keep this in step with CHANGELOG.md when you cut a version.
#include "loomy.h"

typedef struct {
    const wchar_t *ver;      // "2.7"
    const wchar_t *date;     // short release date, or 0
    const wchar_t *lines[8]; // change bullets, 0-terminated
} Release;

// newest first
static const Release RELEASES[] = {
    { L"2.11", L"2026-10-22", {
        L"Update checks give up on a bad connection in seconds, not a minute.",
        L"\u201cChecking GitHub\u2026\u201d shows up before the check starts.",
        L"/vh shows the full history (it used to cut off around 15).",
        L"New core mode: /update, /version, /vh and /verc only exist there.",
        L"After /update, Loomy comes back in the mode you were in.",
        L"New display (dp): switch screens like Win+P, by picker or name.",
        0 } },
    { L"2.10", L"2026-10-22", {
        L"/update only installs releases you published, built by GitHub Actions.",
        L"Fix builds like v2.9b are understood (2.9 < 2.9b < 2.9c < 2.10).",
        L"Fixed /update and /version freezing on a version with a letter.",
        L"Downloads the exact release it checked, not whatever is newest.",
        L"Repo cleanup: removed stray copies of files from the root.",
        0 } },
    { L"2.9", L"2026-10-22", {
        L"Switching modes reloads Loomy: clean chat, nothing left over.",
        L"/reload keeps your current mode.",
        L"New setting: Remember position (show up where you last hid it).",
        L"New /resetwindow: move Loomy back to the middle.",
        L"New web many: the list stays open so you can open several sites.",
        0 } },
    { L"2.8", L"2026-10-21", {
        L"Modes: home and work. /mode (/m) to switch, hide, show, pick main.",
        L"The header shows which mode you're in.",
        L"New web: open saved sites by alias, or pick from a list.",
        L"New /webconfig (/webc): add, remove, batch, info, rename, update.",
        L"Each mode keeps its own saved sites.",
        L"Strict input: stray or missing words show the command's usage.",
        L"Tab completes modes, site aliases and subcommands.",
        0 } },
    { L"2.7", L"2026-10-21", {
        L"Show the version number next to the title.",
        L"Ctrl+Up / Ctrl+Down scroll the chat.",
        L"New /version (/ver): your version + check for a newer one.",
        L"New /vh: every version with release and local-update dates.",
        L"New /verc: this changelog panel.",
        0 } },
    { L"2.6", L"2026-10-21", {
        L"Commands that change Loomy need a / prefix (/config, /update...).",
        L"Task and print commands stay plain (lp, net, rb, exe, help).",
        L"A wrong prefix now nudges you to the right one.",
        L"Removed the command table from the README (type help instead).",
        0 } },
    { L"2.5", L"2026-10-20", {
        L"New /update: pulls the newest build from GitHub and swaps itself.",
        L"Automatic builds + releases on GitHub from a version tag.",
        L"Added an MIT license and a setup guide.",
        0 } },
    { L"2.4", L"2026-10-20", {
        L"net groups by app: one row per app, processes totalled.",
        L"Ending a task closes the whole app and its children.",
        L"The confirm shows how many processes it will close.",
        0 } },
    { L"2.3", L"2026-10-20", {
        L"New net: live per-app download/upload speeds.",
        L"End a task on Delete, elevating to admin only when needed.",
        0 } },
    { L"2.2", L"2026-10-20", {
        L"lp is now a folder browser (arrows, Enter, left/right).",
        L"Clean screen each time Loomy hides.",
        0 } },
    { L"2.1", L"2026-10-20", {
        L"Slimmed down; each command has one clear name.",
        L"config is an in-app settings screen.",
        L"lp opens your folders, rb the recycle bin, exe its location.",
        0 } },
    { L"2.0", L"2026-10-20", {
        L"Rebuilt from the old batch/WPF app into one tiny C program.",
        L"Summon anywhere via hotkey, with a tray icon and console.",
        0 } },
};
#define NRELEASES (int)(sizeof(RELEASES) / sizeof(RELEASES[0]))

static int g_active;
static int g_detail;      // -1 = list, else index of the version being shown
static int g_sel, g_top, g_vis = 8;
static int g_listTop, g_rowStep;

static int find_ver(const wchar_t *v) {
    if (*v == L'v' || *v == L'V') v++;
    for (int i = 0; i < NRELEASES; i++) if (s_ieq(RELEASES[i].ver, v)) return i;
    return -1;
}

int changelog_active(void) { return g_active; }

// arg may be "" (open list) or a version like "v2.7" (open that detail)
void changelog_open(const wchar_t *arg) {
    settings_close();
    g_active = 1; g_sel = 0; g_top = 0; g_detail = -1;
    if (arg && *arg) {
        int i = find_ver(arg);
        if (i >= 0) { g_sel = i; g_detail = i; }
        else { outf(OUT_ERR, L"No changelog for '%s'.", arg); g_active = 0; return; }
    }
    ui_set_input(L"");
    ui_redraw();
}

void changelog_close(void) {
    if (!g_active) return;
    g_active = 0;
    ui_redraw();
}

static void keep_visible(void) {
    if (g_sel < g_top) g_top = g_sel;
    if (g_sel >= g_top + g_vis) g_top = g_sel - g_vis + 1;
    if (g_top < 0) g_top = 0;
}

int changelog_key(WPARAM vk) {
    if (g_detail >= 0) {           // detail view
        switch (vk) {
        case VK_ESCAPE: case VK_LEFT:
            g_detail = -1; ui_redraw(); break;
        case VK_UP:   if (g_sel > 0) { g_sel--; g_detail = g_sel; keep_visible(); ui_redraw(); } break;
        case VK_DOWN: if (g_sel < NRELEASES - 1) { g_sel++; g_detail = g_sel; keep_visible(); ui_redraw(); } break;
        }
        return 1;
    }
    switch (vk) {                  // list view
    case VK_UP:    if (g_sel > 0) g_sel--; keep_visible(); ui_redraw(); break;
    case VK_DOWN:  if (g_sel < NRELEASES - 1) g_sel++; keep_visible(); ui_redraw(); break;
    case VK_RETURN: case VK_RIGHT: g_detail = g_sel; ui_redraw(); break;
    case VK_ESCAPE: changelog_close(); break;
    }
    return 1;
}

int changelog_char(WPARAM ch) { return 1; }

static int row_at(int y) {
    if (!g_rowStep || y < g_listTop) return -1;
    int i = g_top + (y - g_listTop) / g_rowStep;
    return (i < NRELEASES && i < g_top + g_vis) ? i : -1;
}

void changelog_click(int x, int y) {
    if (g_detail >= 0) return;
    int i = row_at(y);
    if (i < 0) return;
    g_sel = i; g_detail = i; ui_redraw();
}

void changelog_wheel(int notches) {
    if (g_detail >= 0) return;
    g_sel -= notches;
    if (g_sel < 0) g_sel = 0;
    if (g_sel > NRELEASES - 1) g_sel = NRELEASES - 1;
    keep_visible(); ui_redraw();
}

void changelog_paint(HDC dc, RECT a) {
    SelectObject(dc, g_font);

    if (g_detail >= 0) {
        const Release *r = &RELEASES[g_detail];
        wchar_t title[64]; wsprintfW(title, L"What's new in v%s", r->ver);
        int w = ui_text(dc, a.left, a.top, title, g_accent);
        if (r->date) ui_text(dc, a.left + w + S(16), a.top, r->date, C_DIM);

        int y = a.top + g_lineH + S(16);
        for (int i = 0; r->lines[i]; i++) {
            ui_text(dc, a.left + S(6), y, L"\x2022", g_accent);   // bullet
            RECT tr = { a.left + S(24), y, a.right, y + g_lineH * 3 };
            DrawTextW(dc, r->lines[i], -1, &tr, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
            SetTextColor(dc, C_TEXT);
            DrawTextW(dc, r->lines[i], -1, &tr, DT_WORDBREAK | DT_NOPREFIX);
            y += (tr.bottom - tr.top) + S(6);
        }

        SelectObject(dc, g_fontSmall);
        ui_text(dc, a.left, a.bottom - S(16),
                L"\x2191\x2193 other versions    \x2190 / Esc back to list", C_DIM);
        SelectObject(dc, g_font);
        return;
    }

    // list view
    ui_text(dc, a.left, a.top, L"Changelog", g_accent);
    int y = a.top + g_lineH + S(14);

    int rowH = g_lineH + S(10);
    g_rowStep = rowH + S(2);
    g_listTop = y;
    int bottom = a.bottom - S(26);
    g_vis = (bottom - y) / g_rowStep;
    if (g_vis < 1) g_vis = 1;
    keep_visible();

    for (int i = g_top; i < NRELEASES && i < g_top + g_vis; i++) {
        const Release *r = &RELEASES[i];
        int sel = i == g_sel;
        if (sel) ui_fill(dc, a.left, y, a.right, y + rowH, ui_mix(g_accent, C_BG, 13));
        int ty = y + (rowH - g_lineH) / 2;
        if (sel) ui_text(dc, a.left + S(10), ty, L"\x203A", g_accent);

        wchar_t vlabel[16]; wsprintfW(vlabel, L"v%s", r->ver);
        ui_text(dc, a.left + S(28), ty, vlabel, sel ? C_BRIGHT : C_TEXT);
        if (r->date) ui_text(dc, a.left + S(110), ty, r->date, C_DIM);
        // show the first change as a teaser
        if (r->lines[0]) {
            SIZE sz; GetTextExtentPoint32W(dc, L"\x2192 ", 2, &sz);
            ui_text(dc, a.left + S(230), ty, r->lines[0], sel ? C_TEXT : C_DIM);
        }
        y += g_rowStep;
    }

    if (NRELEASES > g_vis) {
        wchar_t pos[32]; wsprintfW(pos, L"%d/%d", g_sel + 1, NRELEASES);
        SelectObject(dc, g_fontSmall);
        SIZE sz; GetTextExtentPoint32W(dc, pos, s_len(pos), &sz);
        ui_text(dc, a.right - sz.cx, a.bottom - S(16), pos, C_DIM);
        SelectObject(dc, g_font);
    }

    SelectObject(dc, g_fontSmall);
    ui_text(dc, a.left, a.bottom - S(16),
            L"\x2191\x2193 choose    Enter open    Esc close", C_DIM);
    SelectObject(dc, g_font);
}
