// commands.c - everything you can type into Loomy lives here.
//
// To add a command:
//   1. write a function:   static void c_thing(wchar_t *args) { out(OUT_TEXT, L"did the thing"); }
//   2. add a row to COMMANDS:  { L"Thing", L"what it does", L"th", c_thing },
// help and Tab completion pick it up automatically.
#include "loomy.h"
#include <shellapi.h>

typedef void (*CmdFn)(wchar_t *args);
typedef struct {
    const wchar_t *name;    // shown in help
    const wchar_t *about;   // shown in help
    const wchar_t *type;    // what you actually type
    CmdFn fn;
} Command;

// ---------------------------------------------------------------- Loomy's Path folders

static wchar_t g_folders[MAX_PATH];

const wchar_t *folders_dir(void) {
    if (!g_folders[0]) {
        s_cpy(g_folders, cfg_dir(), MAX_PATH);
        s_cat(g_folders, L"\\Folders", MAX_PATH);
    }
    CreateDirectoryW(g_folders, 0);
    return g_folders;
}

static void open_folder(const wchar_t *path) {
    ShellExecuteW(0, L"open", L"explorer.exe", path, 0, SW_SHOWNORMAL);
}

// ---------------------------------------------------------------- commands

static void c_help(wchar_t *args);

static void c_config(wchar_t *args) { settings_open(); }
static void c_net(wchar_t *args)    { net_open(); }
static void c_update(wchar_t *args)  { cmd_update(args); }

static void c_lp(wchar_t *args) {
    if (!*args) { browser_open(folders_dir()); return; }   // no name: show the folder list

    wchar_t path[MAX_PATH];
    s_cpy(path, folders_dir(), MAX_PATH);
    for (wchar_t *c = args; *c; c++)              // keep it one safe folder name
        if (*c == L'\\' || *c == L'/' || *c == L':' || *c == L'*' || *c == L'?' ||
            *c == L'"' || *c == L'<' || *c == L'>' || *c == L'|') *c = L'-';
    s_cat(path, L"\\", MAX_PATH);
    s_cat(path, args, MAX_PATH);
    if (CreateDirectoryW(path, 0)) outf(OUT_OK, L"Made a new folder: %s", args);
    open_folder(path);
    outf(OUT_OK, L"Opening %s", path);
    ui_hide();
}

static void c_rb(wchar_t *args) {
    open_folder(L"shell:RecycleBinFolder");
    out(OUT_OK, L"Opening the recycle bin");
    ui_hide();
}

static void c_exe(wchar_t *args) {
    wchar_t exe[MAX_PATH], sel[MAX_PATH + 16] = L"/select,\"";
    GetModuleFileNameW(0, exe, MAX_PATH);
    s_cat(sel, exe, MAX_PATH + 16);
    s_cat(sel, L"\"", MAX_PATH + 16);
    ShellExecuteW(0, L"open", L"explorer.exe", sel, 0, SW_SHOWNORMAL);   // opens the folder with the exe highlighted
    outf(OUT_OK, L"I live at %s", exe);
    ui_hide();
}

static void c_clear(wchar_t *args)    { ui_clear(); }
static void c_hide(wchar_t *args)     { ui_hide(); }
static void c_reload(wchar_t *args)   { ui_restart(); }
static void c_shutdown(wchar_t *args) { ui_quit(); }

// ---------------------------------------------------------------- the list

static const Command COMMANDS[] = {
    { L"Help",         L"show this list",                  L"help",     c_help },
    { L"Config",       L"open settings",                   L"config",   c_config },
    { L"Loomy's Path", L"browse your folders",  L"lp",           c_lp },
    { L"Network",      L"see what's using the internet",   L"net",      c_net },
    { L"Recycle Bin",  L"open the recycle bin",            L"rb",       c_rb },
    { L"Exe Location", L"show where loomy.exe lives",      L"exe",      c_exe },
    { L"Clear",        L"clear the screen",                L"clear",    c_clear },
    { L"Hide",         L"hide Loomy (Esc works too)",      L"hide",     c_hide },
    { L"Reload",       L"restart Loomy fresh",             L"reload",   c_reload },
    { L"Update",       L"get the latest version from GitHub", L"update", c_update },
    { L"Shutdown",     L"close Loomy completely",          L"shutdown", c_shutdown },
};
static const int NCOMMANDS = sizeof(COMMANDS) / sizeof(COMMANDS[0]);

static void pad_to(wchar_t *s, int width, int cap) {
    int n = s_len(s);
    while (n < width && n < cap - 1) s[n++] = L' ';
    s[n] = 0;
}

static void c_help(wchar_t *args) {
    wchar_t head[160] = L"Command";
    pad_to(head, 16, 160); s_cat(head, L"What it does", 160);
    pad_to(head, 51, 160); s_cat(head, L"Type", 160);
    out(OUT_DIM, head);
    for (int i = 0; i < NCOMMANDS; i++) {
        wchar_t row[160];
        s_cpy(row, COMMANDS[i].name, 160);   pad_to(row, 16, 160);
        s_cat(row, COMMANDS[i].about, 160);  pad_to(row, 51, 160);
        s_cat(row, COMMANDS[i].type, 160);
        out(OUT_TEXT, row);
    }
}

// ---------------------------------------------------------------- running a line

void cmd_greet(void) {
    SYSTEMTIME t; GetLocalTime(&t);
    const wchar_t *g = t.wHour < 5 ? L"Up late" : t.wHour < 12 ? L"Good morning" :
                       t.wHour < 18 ? L"Good afternoon" : L"Good evening";
    if (g_cfg.name[0]) outf(OUT_ACCENT, L"%s, %s.", g, g_cfg.name);
    else               outf(OUT_ACCENT, L"%s.", g);
    out(OUT_DIM, L"Type help to see what I can do.");
}

void cmd_run(wchar_t *line) {
    line = s_trim(line);
    if (!*line) return;
    out(OUT_ECHO, line);

    wchar_t *args = s_split(line);
    for (int i = 0; i < NCOMMANDS; i++)
        if (s_ieq(COMMANDS[i].type, line)) { COMMANDS[i].fn(args); return; }

    outf(OUT_ERR, L"Hmm, I don't know '%s'.", line);
    out(OUT_DIM, L"Type help to see what I can do.");
}

// ---------------------------------------------------------------- Tab completion

static wchar_t g_matches[64][64];
static int g_nmatches;

static void add_match(const wchar_t *w) {
    if (g_nmatches < 64) s_cpy(g_matches[g_nmatches++], w, 64);
}

void cmd_complete(wchar_t *line, int cap) {
    g_nmatches = 0;
    wchar_t *word = line;

    if (s_istarts(line, L"lp ")) {               // lp <Tab> completes your folder names
        word = line + 3;
        wchar_t pattern[MAX_PATH];
        s_cpy(pattern, folders_dir(), MAX_PATH);
        s_cat(pattern, L"\\*", MAX_PATH);
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(pattern, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != L'.' &&
                    s_istarts(fd.cFileName, word)) add_match(fd.cFileName);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    } else {
        for (int i = 0; i < NCOMMANDS; i++)
            if (s_istarts(COMMANDS[i].type, line)) add_match(COMMANDS[i].type);
    }
    if (!g_nmatches) return;

    // fill in as much as all matches agree on
    int common = s_len(g_matches[0]);
    for (int i = 1; i < g_nmatches; i++) {
        int j = 0;
        while (j < common && s_low(g_matches[i][j]) == s_low(g_matches[0][j])) j++;
        common = j;
    }
    int base = (int)(word - line);
    if (base + common < cap) {
        for (int j = 0; j < common; j++) line[base + j] = g_matches[0][j];
        line[base + common] = 0;
        if (g_nmatches == 1 && word == line && base + common + 1 < cap) s_cat(line, L" ", cap);
    }
    if (g_nmatches > 1) {
        wchar_t list[1024] = L"";
        for (int i = 0; i < g_nmatches; i++) { s_cat(list, g_matches[i], 1024); s_cat(list, L"   ", 1024); }
        out(OUT_DIM, list);
    }
}
