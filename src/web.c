// web.c - saved websites you open by alias. Each mode has its own list, stored in
// %APPDATA%\Loomy\web-<mode>.txt, so "docs" can be one site at work and another at home.
//   web                          list panel: Up/Down, Enter opens, Esc closes
//   web many                     same list, but it stays open so you can open several
//   web <alias>                  open it (a batch opens all its sites)
//   /webconfig add|remove|batch|info|rename|update ...
//
// A batch keeps its own copy of the URLs, taken when you made it. That's what lets "false"
// fold the singles away without the batch losing anything. It also means updating a single
// later won't change a batch that already holds a copy of it.
#include "loomy.h"
#include <shellapi.h>

#define MAX_SITES 128
#define MAX_URLS  256
#define URL_LEN   512
#define ALIAS_LEN 32
#define STAMP_LEN 20
#define MAX_BATCH 32

typedef struct {
    wchar_t alias[ALIAS_LEN];
    wchar_t created[STAMP_LEN], changed[STAMP_LEN];
    int     batch, n;
    short   url[MAX_BATCH];       // slots in the URL pool
} Site;

static Site    g_s[MAX_SITES];
static int     g_n;
static wchar_t g_pool[MAX_URLS][URL_LEN];
static char    g_used[MAX_URLS];

// ---------------------------------------------------------------- small helpers
static const wchar_t *cur_mode(void) { return mode_name(mode_current()); }

static void file_path(wchar_t *p) {
    s_cpy(p, cfg_dir(), MAX_PATH);
    s_cat(p, L"\\web-", MAX_PATH);
    s_cat(p, cur_mode(), MAX_PATH);
    s_cat(p, L".txt", MAX_PATH);
}

static void now_stamp(wchar_t *out) {
    SYSTEMTIME t; GetLocalTime(&t);
    wsprintfW(out, L"%04d-%02d-%02d %02d:%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute);
}

static int find(const wchar_t *alias) {
    for (int i = 0; i < g_n; i++) if (s_ieq(g_s[i].alias, alias)) return i;
    return -1;
}

static int url_new(const wchar_t *u) {
    for (int i = 0; i < MAX_URLS; i++)
        if (!g_used[i]) { g_used[i] = 1; s_cpy(g_pool[i], u, URL_LEN); return i; }
    return -1;
}

static int urls_free(void) {
    int n = 0;
    for (int i = 0; i < MAX_URLS; i++) if (!g_used[i]) n++;
    return n;
}

static void site_remove(int i) {
    for (int k = 0; k < g_s[i].n; k++) g_used[g_s[i].url[k]] = 0;
    for (int j = i; j < g_n - 1; j++) g_s[j] = g_s[j + 1];
    g_n--;
}

static const wchar_t *url_of(const Site *s, int k) { return g_pool[s->url[k]]; }

// aliases: no spaces (the input splits on them anyway), up to 31 chars, and not
// "true"/"false" (batch's keep flag) or "many" (web many)
static int reserved(const wchar_t *a) { return s_ieq(a, L"true") || s_ieq(a, L"false") || s_ieq(a, L"many"); }

static int alias_ok(const wchar_t *a) {
    int n = s_len(a);
    if (n < 1 || n >= ALIAS_LEN) return 0;
    for (int i = 0; i < n; i++) if (a[i] < 33) return 0;
    return !reserved(a);
}

static void alias_bad(const wchar_t *a) {
    if (s_ieq(a, L"many"))
        outf(OUT_ERR, L"'%s' can't be an alias, web uses it (web many).", a);
    else if (reserved(a))
        outf(OUT_ERR, L"'%s' can't be an alias, batch uses it as the keep flag.", a);
    else
        outf(OUT_ERR, L"'%s' is too long for an alias (31 characters max).", a);
}

// does it start with a scheme like https:// ?
static int has_scheme(const wchar_t *u) {
    int i = 0;
    while ((u[i] >= L'a' && u[i] <= L'z') || (u[i] >= L'A' && u[i] <= L'Z') ||
           (i && ((u[i] >= L'0' && u[i] <= L'9') || u[i] == L'+' || u[i] == L'-' || u[i] == L'.'))) i++;
    return i > 0 && u[i] == L':' && u[i + 1] == L'/' && u[i + 2] == L'/';
}

// "youtube.com" -> "https://youtube.com", otherwise Windows tries to open it as a file
static int norm_url(const wchar_t *in, wchar_t *out) {
    if (s_len(in) > URL_LEN - 16) { out[0] = 0; return 0; }
    out[0] = 0;
    if (!has_scheme(in) && !s_istarts(in, L"mailto:")) s_cpy(out, L"https://", URL_LEN);
    s_cat(out, in, URL_LEN);
    return 1;
}

// for display: drop "https://" and a trailing "/", cut to max chars with an ellipsis
static void short_url(const wchar_t *u, wchar_t *out, int max) {
    if (s_istarts(u, L"https://")) u += 8;
    else if (s_istarts(u, L"http://")) u += 7;
    int n = s_len(u);
    if (n > 1 && u[n - 1] == L'/') n--;
    if (n > max) {
        for (int i = 0; i < max - 1; i++) out[i] = u[i];
        out[max - 1] = 0x2026; out[max] = 0;
    } else {
        for (int i = 0; i < n; i++) out[i] = u[i];
        out[n] = 0;
    }
}

// ---------------------------------------------------------------- load / save
// File format, one entry per line, tab separated (tabs can't be typed into an alias or URL):
//   site <TAB> alias <TAB> created <TAB> changed <TAB> url
//   batch <TAB> alias <TAB> created <TAB> changed <TAB> url <TAB> url ...
static void parse_line(wchar_t *line) {
    if (*line == L'#' || !*line || g_n >= MAX_SITES) return;
    wchar_t *f[5 + MAX_BATCH];
    int nf = 0;
    wchar_t *p = line;
    while (nf < 5 + MAX_BATCH) {
        f[nf++] = p;
        while (*p && *p != L'\t') p++;
        if (!*p) break;
        *p++ = 0;
    }
    if (nf < 5) return;
    int batch = s_ieq(f[0], L"batch");
    if (!batch && !s_ieq(f[0], L"site")) return;
    if (!alias_ok(f[1]) || find(f[1]) >= 0) return;

    Site *s = &g_s[g_n];
    memset(s, 0, sizeof(*s));
    s_cpy(s->alias, f[1], ALIAS_LEN);
    s_cpy(s->created, f[2], STAMP_LEN);
    s_cpy(s->changed, f[3], STAMP_LEN);
    s->batch = batch;
    int want = batch ? nf - 4 : 1;
    for (int k = 0; k < want && s->n < MAX_BATCH; k++) {
        wchar_t *u = s_trim(f[4 + k]);
        if (!*u) continue;
        int slot = url_new(u);
        if (slot < 0) break;
        s->url[s->n++] = (short)slot;
    }
    if (s->n) g_n++;
}

// read the current mode's list fresh every time (it's tiny, and this way switching
// modes or hand-editing the file just works)
static void load(void) {
    g_n = 0;
    memset(g_used, 0, sizeof(g_used));
    wchar_t path[MAX_PATH]; file_path(path);
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD size = GetFileSize(f, 0);
    if (size == INVALID_FILE_SIZE || size == 0 || size > 2 * 1024 * 1024) { CloseHandle(f); return; }

    HANDLE heap = GetProcessHeap();
    char *raw = HeapAlloc(heap, 0, size + 1);
    wchar_t *wide = HeapAlloc(heap, 0, (size + 1) * sizeof(wchar_t));
    DWORD got = 0;
    if (raw && wide && ReadFile(f, raw, size, &got, 0)) {
        char *src = raw;
        if (got >= 3 && (unsigned char)raw[0] == 0xEF && (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF) {
            src += 3; got -= 3;
        }
        int w = MultiByteToWideChar(CP_UTF8, 0, src, (int)got, wide, (int)size);
        wide[w] = 0;
        wchar_t *line = wide;
        while (line) {
            wchar_t *next = line;
            while (*next && *next != L'\n') next++;
            if (*next) *next++ = 0; else next = 0;
            int n = s_len(line);
            if (n && line[n - 1] == L'\r') line[n - 1] = 0;
            parse_line(line);
            line = next;
        }
    }
    CloseHandle(f);
    if (raw) HeapFree(heap, 0, raw);
    if (wide) HeapFree(heap, 0, wide);
}

static void put(wchar_t *b, int *pos, int cap, const wchar_t *s) {
    while (*s && *pos < cap - 1) b[(*pos)++] = *s++;
    b[*pos] = 0;
}

static int save(void) {
    int cap = 1024 + MAX_SITES * 128 + MAX_URLS * (URL_LEN + 2);
    HANDLE heap = GetProcessHeap();
    wchar_t *text = HeapAlloc(heap, 0, cap * sizeof(wchar_t));
    char *utf8 = HeapAlloc(heap, 0, cap * 3);
    int ok = 0;
    if (text && utf8) {
        int pos = 0;
        text[0] = 0;
        put(text, &pos, cap, L"# Loomy saved sites for ");
        put(text, &pos, cap, cur_mode());
        put(text, &pos, cap, L" mode. Easiest to change with /webconfig.\r\n");
        for (int i = 0; i < g_n; i++) {
            Site *s = &g_s[i];
            put(text, &pos, cap, s->batch ? L"batch\t" : L"site\t");
            put(text, &pos, cap, s->alias);   put(text, &pos, cap, L"\t");
            put(text, &pos, cap, s->created); put(text, &pos, cap, L"\t");
            put(text, &pos, cap, s->changed);
            for (int k = 0; k < s->n; k++) { put(text, &pos, cap, L"\t"); put(text, &pos, cap, url_of(s, k)); }
            put(text, &pos, cap, L"\r\n");
        }
        int n = WideCharToMultiByte(CP_UTF8, 0, text, pos, utf8, cap * 3, 0, 0);
        wchar_t path[MAX_PATH]; file_path(path);
        HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD w; ok = WriteFile(f, utf8, n > 0 ? n : 0, &w, 0);
            CloseHandle(f);
        }
    }
    if (text) HeapFree(heap, 0, text);
    if (utf8) HeapFree(heap, 0, utf8);
    if (!ok) out(OUT_ERR, L"Couldn't save the sites file.");
    return ok;
}

// ---------------------------------------------------------------- opening
// stay = 1 keeps Loomy up (web many); returns 1 if it opened
static int launch(int i, int stay) {
    Site *s = &g_s[i];
    int failed = 0;
    for (int k = 0; k < s->n; k++) {
        if (k) Sleep(150);   // a breath between tabs so the browser doesn't spawn extra windows
        if ((INT_PTR)ShellExecuteW(0, L"open", url_of(s, k), 0, 0, SW_SHOWNORMAL) <= 32) failed++;
    }
    wchar_t su[80];
    if (failed == s->n) {
        short_url(url_of(s, 0), su, 60);
        outf(OUT_ERR, L"Windows couldn't open %s.", s->batch ? s->alias : su);
        return 0;
    }
    if (s->batch) outf(OUT_OK, L"Opening %s (%d sites)", s->alias, s->n);
    else { short_url(url_of(s, 0), su, 60); outf(OUT_OK, L"Opening %s", su); }
    if (failed) { outf(OUT_ERR, L"%d of them didn't open.", failed); return 0; }
    if (stay) ui_refocus_soon();
    else ui_hide();
    return 1;
}

// ---------------------------------------------------------------- web
void cmd_web(wchar_t *args) {
    load();
    if (!*args) { web_open(0); return; }
    if (s_ieq(args, L"many")) { web_open(1); return; }
    int i = find(args);
    if (i >= 0) { launch(i, 0); return; }
    outf(OUT_ERR, L"No site called '%s' in %s mode.", args, cur_mode());
    out(OUT_DIM, g_n ? L"Type web to see them all." : L"Add one with: /webconfig add <alias> <site>");
}

void web_complete(const wchar_t *partial) {
    load();
    for (int i = 0; i < g_n; i++) complete_add(g_s[i].alias, partial);
}

// ---------------------------------------------------------------- /webconfig
static void wc_add(const wchar_t *alias, const wchar_t *site) {
    if (!alias_ok(alias)) { alias_bad(alias); return; }
    if (find(alias) >= 0) {
        outf(OUT_ERR, L"%s already exists. To change it: %s update %s <site>", alias, cmd_typed(), alias);
        return;
    }
    wchar_t url[URL_LEN];
    if (!norm_url(site, url)) { out(OUT_ERR, L"That URL is too long."); return; }
    if (g_n >= MAX_SITES || !urls_free()) { out(OUT_ERR, L"The list is full. Remove something first."); return; }

    Site *s = &g_s[g_n++];
    memset(s, 0, sizeof(*s));
    s_cpy(s->alias, alias, ALIAS_LEN);
    now_stamp(s->created);
    s_cpy(s->changed, s->created, STAMP_LEN);
    s->url[0] = (short)url_new(url);
    s->n = 1;
    if (!save()) return;
    wchar_t su[80]; short_url(url, su, 50);
    outf(OUT_OK, L"Saved %s \x2192 %s (%s mode). Open it with: web %s", alias, su, cur_mode(), alias);
}

static void wc_remove(const wchar_t *alias) {
    int i = find(alias);
    if (i < 0) { outf(OUT_ERR, L"No site called '%s' in %s mode.", alias, cur_mode()); return; }
    wchar_t name[ALIAS_LEN]; s_cpy(name, g_s[i].alias, ALIAS_LEN);
    int batch = g_s[i].batch, n = g_s[i].n;
    site_remove(i);
    if (!save()) return;
    if (batch) outf(OUT_OK, L"Removed the %s batch and its %d sites.", name, n);
    else       outf(OUT_OK, L"Removed %s.", name);
}

static void wc_batch(const wchar_t *name, wchar_t **members, int count, int keep) {
    if (!alias_ok(name)) { alias_bad(name); return; }
    if (find(name) >= 0) { outf(OUT_ERR, L"%s already exists. Pick another name for the batch.", name); return; }

    // every member has to exist, or nothing happens
    wchar_t missing[256] = L"";
    for (int m = 0; m < count; m++)
        if (find(members[m]) < 0) {
            if (missing[0]) s_cat(missing, L", ", 256);
            s_cat(missing, members[m], 256);
        }
    if (missing[0]) { outf(OUT_ERR, L"Not found in %s mode: %s. Nothing changed.", cur_mode(), missing); return; }

    // gather the URLs (batches inside batches flatten out), skipping exact repeats
    const wchar_t *urls[MAX_BATCH];
    int nurls = 0;
    for (int m = 0; m < count; m++) {
        Site *s = &g_s[find(members[m])];
        for (int k = 0; k < s->n; k++) {
            int dup = 0;
            for (int j = 0; j < nurls; j++) if (s_ieq(urls[j], url_of(s, k))) { dup = 1; break; }
            if (dup) continue;
            if (nurls >= MAX_BATCH) { outf(OUT_ERR, L"A batch can hold %d sites at most. Nothing changed.", MAX_BATCH); return; }
            urls[nurls++] = url_of(s, k);
        }
    }
    if (g_n >= MAX_SITES || urls_free() < nurls) { out(OUT_ERR, L"The list is full. Remove something first."); return; }

    // build it (copies, so the singles can go away safely)
    Site *b = &g_s[g_n++];
    memset(b, 0, sizeof(*b));
    s_cpy(b->alias, name, ALIAS_LEN);
    now_stamp(b->created);
    s_cpy(b->changed, b->created, STAMP_LEN);
    b->batch = 1;
    for (int j = 0; j < nurls; j++) b->url[b->n++] = (short)url_new(urls[j]);

    // the names, for the message (taken before anything gets removed)
    wchar_t names[256] = L"";
    for (int m = 0; m < count; m++) {
        int dup = 0;
        for (int j = 0; j < m; j++) if (s_ieq(members[j], members[m])) dup = 1;
        if (dup) continue;
        if (names[0]) s_cat(names, L", ", 256);
        s_cat(names, g_s[find(members[m])].alias, 256);
    }

    if (!keep)
        for (int m = 0; m < count; m++) {
            int i = find(members[m]);
            if (i >= 0) site_remove(i);
        }
    if (!save()) return;
    outf(OUT_OK, L"Made batch %s: %s (%d sites). Open it with: web %s", name, names, nurls, name);
    if (!keep) outf(OUT_DIM, L"%s now only open through %s.", names, name);
}

static void wc_info(const wchar_t *alias) {
    int i = find(alias);
    if (i < 0) { outf(OUT_ERR, L"No site called '%s' in %s mode.", alias, cur_mode()); return; }
    Site *s = &g_s[i];
    if (s->batch) outf(OUT_ACCENT, L"%s \x00B7 batch of %d sites \x00B7 %s mode", s->alias, s->n, cur_mode());
    else          outf(OUT_ACCENT, L"%s \x00B7 site \x00B7 %s mode", s->alias, cur_mode());
    outf(OUT_TEXT, L"Created  %s", s->created[0] ? s->created : L"unknown");
    if (s->changed[0] && !s_ieq(s->changed, s->created)) outf(OUT_TEXT, L"Changed  %s", s->changed);
    for (int k = 0; k < s->n; k++) {
        wchar_t u[96];
        const wchar_t *full = url_of(s, k);
        if (s_len(full) > 80) { short_url(full, u, 80); outf(OUT_TEXT, L"  %s", u); }
        else outf(OUT_TEXT, L"  %s", full);
    }
}

static void wc_rename(const wchar_t *alias, const wchar_t *to) {
    int i = find(alias);
    if (i < 0) { outf(OUT_ERR, L"No site called '%s' in %s mode.", alias, cur_mode()); return; }
    if (!alias_ok(to)) { alias_bad(to); return; }
    int j = find(to);
    if (j >= 0 && j != i) { outf(OUT_ERR, L"%s already exists.", to); return; }
    wchar_t old[ALIAS_LEN]; s_cpy(old, g_s[i].alias, ALIAS_LEN);
    s_cpy(g_s[i].alias, to, ALIAS_LEN);
    now_stamp(g_s[i].changed);
    if (!save()) return;
    outf(OUT_OK, L"Renamed %s to %s.", old, to);
}

static void wc_update(const wchar_t *alias, const wchar_t *site) {
    int i = find(alias);
    if (i < 0) { outf(OUT_ERR, L"No site called '%s' in %s mode.", alias, cur_mode()); return; }
    if (g_s[i].batch) {
        outf(OUT_ERR, L"%s is a batch. Remove it and batch it again to change its sites.", g_s[i].alias);
        return;
    }
    wchar_t url[URL_LEN];
    if (!norm_url(site, url)) { out(OUT_ERR, L"That URL is too long."); return; }
    s_cpy(g_pool[g_s[i].url[0]], url, URL_LEN);
    now_stamp(g_s[i].changed);
    if (!save()) return;
    wchar_t su[80]; short_url(url, su, 50);
    outf(OUT_OK, L"%s now opens %s.", g_s[i].alias, su);
}

static const wchar_t *WC_SUBS[] = { L"add", L"remove", L"batch", L"info", L"rename", L"update" };

void cmd_webconfig(wchar_t *args) {
    wchar_t *w[MAX_BATCH + 4];
    int n = s_words(args, w, MAX_BATCH + 4);
    const wchar_t *sub = n ? w[0] : L"";
    load();

    if (s_ieq(sub, L"add")) {
        if (n != 3) { cmd_usage(L"add <alias> <site>"); return; }
        wc_add(w[1], w[2]);
    }
    else if (s_ieq(sub, L"remove")) {
        if (n != 2) { cmd_usage(L"remove <alias>"); return; }
        wc_remove(w[1]);
    }
    else if (s_ieq(sub, L"batch")) {
        if (n > MAX_BATCH + 4) { outf(OUT_ERR, L"Too many at once, a batch holds %d sites at most.", MAX_BATCH); return; }
        int keep = 1, last = n - 1;
        if (n >= 1 && (s_ieq(w[last], L"true") || s_ieq(w[last], L"false"))) { keep = s_ieq(w[last], L"true"); last--; }
        if (last < 2) { cmd_usage(L"batch <name> <alias> [more aliases] [keep: true|false, default true]"); return; }
        wc_batch(w[1], &w[2], last - 1, keep);
    }
    else if (s_ieq(sub, L"info")) {
        if (n != 2) { cmd_usage(L"info <alias>"); return; }
        wc_info(w[1]);
    }
    else if (s_ieq(sub, L"rename")) {
        if (n != 3) { cmd_usage(L"rename <alias> <new alias>"); return; }
        wc_rename(w[1], w[2]);
    }
    else if (s_ieq(sub, L"update")) {
        if (n != 3) { cmd_usage(L"update <alias> <new site>"); return; }
        wc_update(w[1], w[2]);
    }
    else cmd_usage(L"<add|remove|batch|info|rename|update> ...");
}

// Tab completion inside /webconfig: words[] is what's typed before the word being completed
void webconfig_complete(wchar_t **words, int n, const wchar_t *partial) {
    if (n == 1) {                                          // "/webconfig <Tab>"
        for (int i = 0; i < 6; i++) complete_add(WC_SUBS[i], partial);
        return;
    }
    const wchar_t *sub = words[1];
    if (n == 2 && (s_ieq(sub, L"remove") || s_ieq(sub, L"info") || s_ieq(sub, L"rename") || s_ieq(sub, L"update")))
        web_complete(partial);
    else if (n >= 3 && s_ieq(sub, L"batch")) {             // members, then the keep flag
        web_complete(partial);
        if (n >= 4) { complete_add(L"true", partial); complete_add(L"false", partial); }
    }
}

// ---------------------------------------------------------------- the list panel
static int  g_active, g_many, g_sel, g_top, g_vis = 8, g_listTop, g_rowStep;
static int  g_order[MAX_SITES];
static char g_opened[MAX_SITES];          // web many: which rows you've opened this time

int web_active(void) { return g_active; }

void web_open(int many) {
    g_many = many;
    memset(g_opened, 0, sizeof(g_opened));
    settings_close();
    load();
    for (int i = 0; i < g_n; i++) {                        // A-Z by alias
        int j = i;
        while (j > 0) {
            const wchar_t *a = g_s[i].alias, *b = g_s[g_order[j - 1]].alias;
            while (*a && s_low(*a) == s_low(*b)) { a++; b++; }
            if (s_low(*a) >= s_low(*b)) break;
            g_order[j] = g_order[j - 1]; j--;
        }
        g_order[j] = i;
    }
    g_sel = 0; g_top = 0;
    g_active = 1;
    ui_set_input(L"");
    ui_redraw();
}

void web_close(void) {
    if (!g_active) return;
    g_active = 0;
    ui_redraw();
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

static void open_selected(void) {
    if (!g_n) return;
    int i = g_order[g_sel];
    if (g_many) {
        if (launch(i, 1)) { g_opened[g_sel] = 1; ui_redraw(); }
        else web_close();                     // show the error in the chat
        return;
    }
    web_close();
    launch(i, 0);
}

int web_key(WPARAM vk) {
    switch (vk) {
    case VK_UP:     move(-1); break;
    case VK_DOWN:   move(+1); break;
    case VK_PRIOR:  move(-g_vis); break;
    case VK_NEXT:   move(+g_vis); break;
    case VK_HOME:   move(-MAX_SITES); break;
    case VK_END:    move(+MAX_SITES); break;
    case VK_RETURN: open_selected(); break;
    case VK_ESCAPE: web_close(); break;
    }
    return 1;
}

int web_char(WPARAM ch) {
    for (int k = 1; ch > 32 && k <= g_n; k++) {            // type a letter to jump to it
        int i = (g_sel + k) % g_n;
        if (s_low(g_s[g_order[i]].alias[0]) == s_low((wchar_t)ch)) { g_sel = i; keep_visible(); ui_redraw(); break; }
    }
    return 1;
}

void web_click(int x, int y, int dbl) {
    if (!g_rowStep || y < g_listTop) return;
    int i = g_top + (y - g_listTop) / g_rowStep;
    if (i >= g_n || i >= g_top + g_vis) return;
    g_sel = i;
    ui_redraw();
    if (dbl) open_selected();
}

void web_wheel(int notches) { move(-notches); }

void web_paint(HDC dc, RECT a) {
    SelectObject(dc, g_font);
    int y = a.top;
    int w = ui_text(dc, a.left, y, L"Websites", g_accent);
    wchar_t sub[48]; wsprintfW(sub, L"%s mode", cur_mode());
    int sw = ui_text(dc, a.left + w + S(16), y, sub, C_DIM);
    if (g_many) ui_text(dc, a.left + w + S(16) + sw + S(12), y, L"\x00B7 many", g_accent);
    y += g_lineH + S(14);

    int rowH = g_lineH + S(10);
    g_rowStep = rowH + S(2);
    g_listTop = y;
    int bottom = a.bottom - S(26);
    g_vis = (bottom - y) / g_rowStep;
    if (g_vis < 1) g_vis = 1;
    keep_visible();

    if (!g_n) {
        wchar_t none[64]; wsprintfW(none, L"No sites saved in %s mode yet.", cur_mode());
        ui_text(dc, a.left + S(10), y + S(4), none, C_TEXT);
        ui_text(dc, a.left + S(10), y + S(4) + g_lineH + S(6), L"Add one with: /webconfig add <alias> <site>", C_DIM);
    }

    int col2 = a.left + S(220);
    for (int r = g_top; r < g_n && r < g_top + g_vis; r++) {
        Site *s = &g_s[g_order[r]];
        int sel = r == g_sel;
        if (sel) ui_fill(dc, a.left, y, a.right, y + rowH, ui_mix(g_accent, C_BG, 13));
        int ty = y + (rowH - g_lineH) / 2;
        if (sel) ui_text(dc, a.left + S(10), ty, L"\x203A", g_accent);

        RECT ar = { a.left + S(28), ty, col2 - S(12), ty + g_lineH };
        SetTextColor(dc, sel ? C_BRIGHT : C_TEXT);
        DrawTextW(dc, s->alias, -1, &ar, DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        int x = col2;
        if (s->batch) {
            wchar_t cnt[24]; wsprintfW(cnt, L"%d sites  ", s->n);
            x += ui_text(dc, x, ty, cnt, g_accent);
        }
        wchar_t line[512] = L"";
        for (int k = 0; k < s->n; k++) {
            wchar_t su[96]; short_url(url_of(s, k), su, 90);
            if (k) s_cat(line, L", ", 512);
            s_cat(line, su, 512);
        }
        int right = a.right - S(10);
        if (g_opened[r]) {                    // web many: mark what you've opened
            SIZE tz; GetTextExtentPoint32W(dc, L"opened", 6, &tz);
            ui_text(dc, right - tz.cx, ty, L"opened", C_OK);
            right -= tz.cx + S(14);
        }
        RECT ur = { x, ty, right, ty + g_lineH };
        SetTextColor(dc, sel ? C_TEXT : C_DIM);
        DrawTextW(dc, line, -1, &ur, DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        y += g_rowStep;
    }

    if (g_n > g_vis) {
        wchar_t pos[32]; wsprintfW(pos, L"%d/%d", g_sel + 1, g_n);
        SelectObject(dc, g_fontSmall);
        SIZE sz; GetTextExtentPoint32W(dc, pos, s_len(pos), &sz);
        ui_text(dc, a.right - sz.cx, a.bottom - S(16), pos, C_DIM);
    }
    SelectObject(dc, g_fontSmall);
    ui_text(dc, a.left, a.bottom - S(16), g_many ? L"\x2191\x2193 choose    Enter open (I stay open)    Esc close"
                                                 : L"\x2191\x2193 choose    Enter open    Esc close", C_DIM);
    SelectObject(dc, g_font);
}
