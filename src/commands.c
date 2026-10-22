// commands.c - everything you can type into Loomy lives here.
//
// To add a command:
//   1. write a function:   static void c_thing(wchar_t *args) { out(OUT_TEXT, L"did the thing"); }
//   2. add a row to COMMANDS (see the columns in the Command struct below).
// help, Tab completion and the "Usage:" replies pick it up automatically.
#include "loomy.h"
#include <shellapi.h>

typedef void (*CmdFn)(wchar_t *args);
typedef struct {
    const wchar_t *name;    // shown in help
    const wchar_t *about;   // shown in help
    const wchar_t *type;    // the bare word (sys commands are typed with a / in front)
    const wchar_t *alias;   // optional shorter bare word, 0 if none
    int            sys;     // 1 = acts on Loomy itself, needs the / prefix
    int            modes;   // which modes it lives in (MODE_ALL, MODE_WORK, ...)
    const wchar_t *usage;   // the shape of its arguments, e.g. L"[version]"; 0 = takes none
    int            maxargs; // most words allowed after it; ARGS_TEXT / ARGS_SELF below
    CmdFn fn;
} Command;

#define ARGS_TEXT -1   // free text, any number of words (lp folder names can have spaces)
#define ARGS_SELF -2   // the command checks its own arguments (it has subcommands)

// does this command answer to the given bare word (its type or alias)?
static int cmd_matches(const Command *c, const wchar_t *word) {
    if (s_ieq(c->type, word)) return 1;
    return c->alias && s_ieq(c->alias, word);
}

#define SYS_PREFIX L'/'

// build what the user actually types for a command: "/config" or "net"
static void typed_form(const Command *c, wchar_t *out, int cap) {
    out[0] = 0;
    if (c->sys) { out[0] = SYS_PREFIX; out[1] = 0; }
    s_cat(out, c->type, cap);
}

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
static void c_version(wchar_t *args) { cmd_version(args); }
static void c_vhist(wchar_t *args)   { cmd_versionhistory(args); }
static void c_verc(wchar_t *args)    { changelog_open(args); }
static void c_mode(wchar_t *args)    { cmd_mode(args); }
static void c_web(wchar_t *args)     { cmd_web(args); }
static void c_webconfig(wchar_t *args) { cmd_webconfig(args); }

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
static void c_resetwin(wchar_t *args) { ui_reset_window(); out(OUT_OK, L"Back in the middle."); }
static void c_shutdown(wchar_t *args) { ui_quit(); }

// ---------------------------------------------------------------- the list

// sys = 1 means it acts on Loomy itself, so you type it with a / (e.g. /config).
// sys = 0 means it does a task for you or just prints, so it's plain (e.g. net, help).
#define ALL MODE_ALL
static const Command COMMANDS[] = {
//    name            about                                   type        alias               sys modes usage                              maxargs    fn
    { L"Help",         L"show this list",                     L"help",     0,                  0, ALL, 0,                                  0,         c_help },
    { L"Loomy's Path", L"browse your folders",                L"lp",       0,                  0, ALL, L"[folder name]",                   ARGS_TEXT, c_lp },
    { L"Websites",     L"open a saved website",               L"web",      0,                  0, ALL, L"[alias | many]",                  1,         c_web },
    { L"Network",      L"see what's using the internet",      L"net",      0,                  0, ALL, 0,                                  0,         c_net },
    { L"Recycle Bin",  L"open the recycle bin",               L"rb",       0,                  0, ALL, 0,                                  0,         c_rb },
    { L"Exe Location", L"show where loomy.exe lives",         L"exe",      0,                  0, ALL, 0,                                  0,         c_exe },
    { L"Mode",         L"switch between home and work",       L"mode",     L"m",               1, ALL, L"[switch|hide|show|main <mode>]",  ARGS_SELF, c_mode },
    { L"Web Config",   L"add, remove and batch saved sites",  L"webconfig", L"webc",           1, ALL, L"<add|remove|batch|info|rename|update> ...", ARGS_SELF, c_webconfig },
    { L"Config",       L"open settings",                      L"config",   0,                  1, ALL, 0,                                  0,         c_config },
    { L"Clear",        L"clear the screen",                   L"clear",    0,                  1, ALL, 0,                                  0,         c_clear },
    { L"Hide",         L"hide Loomy (Esc works too)",         L"hide",     0,                  1, ALL, 0,                                  0,         c_hide },
    { L"Reload",       L"restart Loomy fresh",                L"reload",   0,                  1, ALL, 0,                                  0,         c_reload },
    { L"Reset Window", L"move Loomy back to the middle",      L"resetwindow", 0,               1, ALL, 0,                                  0,         c_resetwin },
    { L"Update",       L"get the latest version from GitHub", L"update",   0,                  1, ALL, 0,                                  0,         c_update },
    { L"Version",      L"show version + check for updates",   L"version",  L"ver",             1, ALL, 0,                                  0,         c_version },
    { L"Versions",     L"list all versions and dates",        L"vh",       L"versionhistory",  1, ALL, 0,                                  0,         c_vhist },
    { L"Changelog",    L"what changed in each version", L"verc",    L"versionchangelog", 1, ALL, L"[version]",                      1,         c_verc },
    { L"Shutdown",     L"close Loomy completely",             L"shutdown", 0,                  1, ALL, 0,                                  0,         c_shutdown },
};
static const int NCOMMANDS = sizeof(COMMANDS) / sizeof(COMMANDS[0]);

static void pad_to(wchar_t *s, int width, int cap) {
    int n = s_len(s);
    while (n < width && n < cap - 1) s[n++] = L' ';
    s[n] = 0;
}

static int in_mode(const Command *c) { return (c->modes >> mode_current()) & 1; }

// what help shows in the Type column: "/mode /m" (the alias only if it's shorter)
static void help_form(const Command *c, wchar_t *out, int cap) {
    typed_form(c, out, cap);
    if (c->alias && s_len(c->alias) < s_len(c->type)) {
        s_cat(out, c->sys ? L"  /" : L"  ", cap);
        s_cat(out, c->alias, cap);
    }
}

static void c_help(wchar_t *args) {
    wchar_t head[160] = L"Command";
    pad_to(head, 16, 160); s_cat(head, L"What it does", 160);
    pad_to(head, 51, 160); s_cat(head, L"Type", 160);
    out(OUT_DIM, head);
    for (int i = 0; i < NCOMMANDS; i++) {
        if (!in_mode(&COMMANDS[i])) continue;
        wchar_t row[160], typed[40];
        help_form(&COMMANDS[i], typed, 40);
        s_cpy(row, COMMANDS[i].name, 160);   pad_to(row, 16, 160);
        s_cat(row, COMMANDS[i].about, 160);  pad_to(row, 51, 160);
        s_cat(row, typed, 160);
        out(OUT_TEXT, row);
    }
    outf(OUT_DIM, L"Commands with / change Loomy itself. You're in %s mode.", mode_name(mode_current()));
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

// the command word exactly as typed ("/m", "web"), for usage lines and hints
static wchar_t g_typed[48];
const wchar_t *cmd_typed(void) { return g_typed; }

void cmd_usage(const wchar_t *shape) {
    outf(OUT_ERR, L"Usage: %s %s", g_typed, shape);
}

static const Command *find_command(const wchar_t *word) {
    for (int i = 0; i < NCOMMANDS; i++) if (cmd_matches(&COMMANDS[i], word)) return &COMMANDS[i];
    return 0;
}

// first mode a command lives in, for the "switch to it" hint
static int home_of(const Command *c) {
    for (int m = 0; m < NMODES; m++) if ((c->modes >> m) & 1) return m;
    return 0;
}

void cmd_run(wchar_t *line) {
    line = s_trim(line);
    if (!*line) return;
    out(OUT_ECHO, line);

    wchar_t *args = s_split(line);

    int slashed = line[0] == SYS_PREFIX;
    wchar_t *word = slashed ? line + 1 : line;   // the command name without any /
    s_cpy(g_typed, line, 48);
    s_lower(g_typed);

    const Command *c = find_command(word);
    if (!c) {
        outf(OUT_ERR, L"Hmm, I don't know '%s'.", line);
        out(OUT_DIM, L"Type help to see what I can do.");
        return;
    }
    if (c->sys && !slashed) {             // they typed a system command without the slash
        outf(OUT_ERR, L"'%s' changes Loomy, so type /%s.", word, c->type);
        return;
    }
    if (!c->sys && slashed) {             // they slashed a plain command
        outf(OUT_ERR, L"Just type %s (no slash needed).", c->type);
        return;
    }
    if (!in_mode(c)) {                    // lives in another mode
        int m = home_of(c);
        outf(OUT_ERR, L"%s is a %s mode command. Switch with: /m switch %s", g_typed, mode_name(m), mode_name(m));
        return;
    }
    if (c->maxargs >= 0 && s_count_words(args) > c->maxargs) {   // strict: no stray words
        if (c->usage) cmd_usage(c->usage);
        else outf(OUT_ERR, L"Usage: %s (nothing goes after it)", g_typed);
        return;
    }
    c->fn(args);
}

// ---------------------------------------------------------------- Tab completion

static wchar_t g_matches[64][64];
static int g_nmatches;

static void add_match(const wchar_t *w) {
    for (int i = 0; i < g_nmatches; i++) if (s_ieq(g_matches[i], w)) return;
    if (g_nmatches < 64) s_cpy(g_matches[g_nmatches++], w, 64);
}

void complete_add(const wchar_t *candidate, const wchar_t *partial) {
    if (s_istarts(candidate, partial)) add_match(candidate);
}

static const wchar_t *MODE_SUBS[] = { L"switch", L"hide", L"show", L"main" };

// arguments: words[] is everything typed before the word being completed
static void complete_args(wchar_t **words, int n, const wchar_t *partial) {
    wchar_t *w0 = words[0];
    const Command *c = find_command(w0[0] == SYS_PREFIX ? w0 + 1 : w0);
    if (!c) return;
    if (c->fn == c_web && n == 1) { complete_add(L"many", partial); web_complete(partial); }
    else if (c->fn == c_webconfig) webconfig_complete(words, n, partial);
    else if (c->fn == c_mode) {
        if (n == 1) for (int i = 0; i < 4; i++) complete_add(MODE_SUBS[i], partial);
        else if (n == 2) {
            int wantHidden = s_ieq(words[1], L"show");   // show offers hidden ones, the rest visible ones
            for (int m = 0; m < NMODES; m++)
                if (mode_is_hidden(m) == wantHidden) complete_add(mode_name(m), partial);
        }
    }
}

void cmd_complete(wchar_t *line, int cap) {
    g_nmatches = 0;
    wchar_t *word = line;
    int spaceAfter = 1;                          // a single match gets a space after it

    if (s_istarts(line, L"lp ")) {               // lp <Tab> completes your folder names (they can have spaces)
        word = line + 3;
        spaceAfter = 0;
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
        int len = s_len(line), ws = len;
        while (ws > 0 && line[ws - 1] != L' ') ws--;
        word = line + ws;
        if (ws == 0) {                           // still on the command name
            if (line[0] == SYS_PREFIX) {         // "/..." completes only system commands, with the /
                for (int i = 0; i < NCOMMANDS; i++) {
                    if (!COMMANDS[i].sys || !in_mode(&COMMANDS[i])) continue;
                    wchar_t typed[40]; typed_form(&COMMANDS[i], typed, 40);
                    if (s_istarts(typed, line)) add_match(typed);
                }
            } else {                             // plain word completes only non-system commands
                for (int i = 0; i < NCOMMANDS; i++)
                    if (!COMMANDS[i].sys && in_mode(&COMMANDS[i]) && s_istarts(COMMANDS[i].type, line))
                        add_match(COMMANDS[i].type);
            }
        } else {                                 // an argument: ask the command what fits
            wchar_t head[300], *words[16];
            s_cpy(head, line, ws + 1 < 300 ? ws + 1 : 300);
            int n = s_words(head, words, 16);
            if (n > 0 && n <= 16) complete_args(words, n, word);
        }
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
        if (g_nmatches == 1 && spaceAfter && base + common + 1 < cap) s_cat(line, L" ", cap);
    }
    if (g_nmatches > 1) {
        wchar_t list[1024] = L"";
        for (int i = 0; i < g_nmatches; i++) { s_cat(list, g_matches[i], 1024); s_cat(list, L"   ", 1024); }
        out(OUT_DIM, list);
    }
}
