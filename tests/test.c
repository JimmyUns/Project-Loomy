#include <windows.h>
#include <locale.h>
#include "../src/loomy.h"
extern wchar_t g_out[400][1100]; extern int g_nout;
extern wchar_t g_opened[64][512]; extern int g_nopened;
extern int g_hidden, g_restarts, g_resets, g_refocus; extern wchar_t g_restartNote[600];
static int fails;
static int last_out;

static void run(const wchar_t *line) {
    wchar_t buf[300]; wcscpy(buf, line);
    int from = g_nout; g_nopened = 0; g_hidden = 0;
    cmd_run(buf);
    for (int i = from; i < g_nout; i++) wprintf(L"   %ls\n", g_out[i]);
    for (int i = 0; i < g_nopened; i++) wprintf(L"   [opened %ls]\n", g_opened[i]);
    last_out = from;
}
static int saw(const wchar_t *needle) {
    for (int i = last_out; i < g_nout; i++) if (wcsstr(g_out[i], needle)) return 1;
    return 0;
}
#define EXPECT(cond, msg) do { if (!(cond)) { wprintf(L"!! FAIL: %s\n", msg); fails++; } } while (0)
static void tab(const wchar_t *line, const wchar_t *want) {
    wchar_t buf[300]; wcscpy(buf, line);
    int from = g_nout;
    cmd_complete(buf, 300);
    wprintf(L"TAB '%ls' -> '%ls'", line, buf);
    for (int i = from; i < g_nout; i++) wprintf(L"   (%ls)", g_out[i] + 4);
    wprintf(L"\n");
    if (want && wcscmp(buf, want)) { wprintf(L"!! FAIL: wanted '%ls'\n", want); fails++; }
}
static void cat(const char *p) { FILE *f = fopen(p, "r"); char l[2048]; if (!f) { wprintf(L"   (no file %s)\n", p); return; } while (fgets(l, 2048, f)) { for (char *c = l; *c; c++) if (*c == '\t') *c = '|'; wprintf(L"   | %s", l); } fclose(f); }

int main(void) {
    setlocale(LC_ALL, "C.UTF-8");
    system("rm -rf /tmp/loomytest /tmp/loomytest\\\\* && mkdir -p /tmp/loomytest");
    cfg_load();

    wprintf(L"\n== strict args ==\n");
    run(L"help blabla");        EXPECT(saw(L"Usage: help (nothing goes after it)"), "help extra");
    run(L"rb now");             EXPECT(saw(L"Usage: rb"), "rb extra");
    run(L"web a b");            EXPECT(saw(L"Usage: web [alias | many]"), "web extra");
    run(L"/m blah");            EXPECT(saw(L"Usage: /m [switch|hide|show|main <mode>]"), "m bad sub");
    run(L"/m switch");          EXPECT(saw(L"Usage: /m switch <home|work|core>"), "m switch no arg");
    run(L"/m switch work now"); EXPECT(saw(L"Usage: /m switch <home|work|core>"), "m switch extra");
    run(L"/webconfig");         EXPECT(saw(L"Usage: /webconfig <add|remove|batch|info|rename|update> ..."), "wc bare");
    run(L"/webc info");         EXPECT(saw(L"Usage: /webc info <alias>"), "wc info bare");
    run(L"/webconfig add yt");  EXPECT(saw(L"Usage: /webconfig add <alias> <site>"), "wc add short");
    run(L"/webconfig batch media"); EXPECT(saw(L"keep: true|false, default true"), "wc batch short");
    run(L"/webconfig batch media true"); EXPECT(saw(L"Usage: /webconfig batch"), "wc batch flag only");
    run(L"m");                  EXPECT(saw(L"so type /mode"), "m without slash");
    run(L"/web");               EXPECT(saw(L"no slash"), "web slashed");

    wprintf(L"\n== modes ==\n");
    EXPECT(mode_current() == 0, "starts home");
    run(L"/m switch work");     EXPECT(mode_current() == 1 && g_restarts == 1 && !wcscmp(g_restartNote, L"Switched to work mode."), "switch reloads into work");
    run(L"/m switch work");     EXPECT(saw(L"Already") && g_restarts == 1, "already: no reload");
    run(L"/m switch office");   EXPECT(saw(L"No mode called 'office'"), "unknown mode");
    run(L"/m main work");       EXPECT(g_cfg.modeMain == 1, "main work");
    run(L"/m hide work");       EXPECT(mode_current() == 0 && g_cfg.modeMain == 0 && mode_is_hidden(1), "hide current+main moves both");
    EXPECT(!wcscmp(g_restartNote, L"Hid work.|It was the main mode, so home is main now.|Switched to home mode."), "hide-current note survives the reload");
    run(L"/m hide core");       EXPECT(mode_is_hidden(2) && mode_current() == 0, "hide core (not current)");
    run(L"/m hide home");       EXPECT(saw(L"at least one") && !mode_is_hidden(0), "can't hide last");
    run(L"/m show core");
    run(L"/m switch work");     EXPECT(saw(L"hidden. Show it first"), "switch to hidden");
    run(L"/m main work");       EXPECT(saw(L"hidden. Show it first"), "main hidden");
    { int r = g_restarts;
    run(L"/m show work");       EXPECT(!mode_is_hidden(1) && mode_current() == 0 && g_restarts == r, "show doesn't move or reload"); }
    run(L"/m show work");       EXPECT(saw(L"isn't hidden"), "show twice");
    wprintf(L"   loomy.cfg after hide/show:\n"); cat("/tmp/loomytest\\loomy.cfg");
    { int r = g_restarts;
    run(L"/m hide work");       EXPECT(saw(L"Hid work.") && g_restarts == r, "hiding a non-current mode doesn't reload"); }
    cfg_load();                 EXPECT(mode_is_hidden(1) && g_cfg.modeMain == 0, "hidden survives reload");
    run(L"/m show work");
    run(L"/m main work");
    cfg_load();                 EXPECT(g_cfg.modeMain == 1, "main survives reload");
    run(L"/m main home");
    // hand-edited cfg with everything hidden -> main stays visible
    { FILE *f = fopen("/tmp/loomytest\\loomy.cfg", "w"); fprintf(f, "mode_main = work\nmode_hidden = home, work, core\n"); fclose(f); }
    cfg_load();                 EXPECT(g_cfg.modeMain == 1 && !mode_is_hidden(1) && mode_is_hidden(0), "sanitize all-hidden");
    { FILE *f = fopen("/tmp/loomytest\\loomy.cfg", "w"); fprintf(f, "mode_main = work\nmode_hidden = work\n"); fclose(f); }
    cfg_load();                 EXPECT(g_cfg.modeMain == 0, "sanitize hidden main");
    g_cfg.modeHidden = 0; g_cfg.modeMain = 0; cfg_save();
    run(L"/m switch home");
    run(L"help");               EXPECT(saw(L"/mode  /m") && saw(L"/webconfig  /webc") && saw(L"home mode"), "help shows aliases + mode");

    wprintf(L"\n== core mode lock ==\n");
    // update/version commands only exist in core; anywhere else they're plain typos
    run(L"/update");            EXPECT(saw(L"don't know '/update'"), "update unknown in home");
    run(L"update");             EXPECT(saw(L"don't know 'update'") && !saw(L"changes Loomy"), "no slash hint either");
    run(L"/ver");               EXPECT(saw(L"don't know '/ver'"), "ver unknown in home");
    run(L"/vh");                EXPECT(saw(L"don't know '/vh'"), "vh unknown in home");
    run(L"/verc v2.6");         EXPECT(saw(L"don't know '/verc'") && !saw(L"changelog_open"), "verc unknown in home");
    run(L"help");               EXPECT(!saw(L"/update") && !saw(L"/verc"), "help hides core commands");
    tab(L"/up", L"/up");
    tab(L"/verc ", L"/verc ");
    run(L"/m switch work");
    run(L"/version");           EXPECT(saw(L"don't know '/version'"), "unknown in work too");
    run(L"/m switch core");     EXPECT(mode_current() == 2 && !wcscmp(g_restartNote, L"Switched to core mode."), "switch to core");
    run(L"/ver");               EXPECT(saw(L"[version ran]"), "ver runs in core");
    run(L"/ver now");           EXPECT(saw(L"Usage: /ver (nothing"), "ver extra");
    run(L"/verc v2.6 extra");   EXPECT(saw(L"Usage: /verc [version]"), "verc extra");
    run(L"/verc v2.6");         EXPECT(saw(L"changelog_open 'v2.6'"), "verc ok");
    run(L"help");               EXPECT(saw(L"/update") && saw(L"/verc") && saw(L"/mode  /m") && saw(L"core mode"), "help in core");
    tab(L"/up", L"/update ");
    run(L"/m hide core");       EXPECT(mode_current() == 0 && mode_is_hidden(2), "hide current core goes to home, not past it");
    run(L"/m show core");
    run(L"/m main core");
    run(L"/m hide home");       EXPECT(mode_current() == 1 && g_cfg.modeMain == 2, "hiding home (main core) keeps core main, moves to work");
    run(L"/m show home");
    run(L"/m main home");
    { // hiding work from work, with core also visible: lands on home, not core
      run(L"/m switch work");
      run(L"/m hide work");     EXPECT(mode_current() == 0, "work hidden -> home, skipping core");
      run(L"/m show work"); }
    run(L"/m switch home");

    wprintf(L"\n== web (home mode) ==\n");
    run(L"web");                // empty panel opens
    run(L"/resetwindow");       EXPECT(g_resets == 1 && saw(L"middle"), "resetwindow");
    run(L"/resetwindow now");   EXPECT(saw(L"Usage: /resetwindow (nothing"), "resetwindow strict");
    run(L"/reload");            EXPECT(g_restarts > 0 && !g_restartNote[0] && mode_current() == 0, "reload keeps mode, no note");
    EXPECT(web_active(), "panel open"); web_close();
    run(L"web yt");             EXPECT(saw(L"No site called 'yt' in home mode"), "missing");
    run(L"/webconfig add yt youtube.com");        EXPECT(saw(L"Saved yt"), "add");
    run(L"/webconfig add yt other.com");          EXPECT(saw(L"already exists"), "dup add");
    run(L"/webconfig add true x.com");            EXPECT(saw(L"keep flag"), "reserved alias");
    run(L"/webconfig add many x.com");            EXPECT(saw(L"web many"), "many is reserved");
    run(L"web many now");                         EXPECT(saw(L"Usage: web [alias | many]"), "web usage");
    run(L"/webconfig add fb https://facebook.com/");
    run(L"/webconfig add ig http://instagram.com");
    run(L"/webconfig add YT2 www.youtube.com/feed");
    run(L"web YT");             EXPECT(g_nopened == 1 && !wcscmp(g_opened[0], L"https://youtube.com") && g_hidden, "open by alias, case-insensitive, scheme added");
    run(L"/webconfig batch media yt fb");          EXPECT(saw(L"Made batch media: yt, fb (2 sites)"), "batch keep default");
    run(L"web media");          EXPECT(g_nopened == 2, "batch opens 2");
    run(L"web yt");             EXPECT(g_nopened == 1, "single still there (keep=true)");
    run(L"/webconfig batch social ig yt2 nope false"); EXPECT(saw(L"Not found in home mode: nope. Nothing changed."), "atomic missing");
    run(L"web ig");             EXPECT(g_nopened == 1, "ig untouched after failed batch");
    run(L"/webconfig batch social ig yt2 FALSE");  EXPECT(saw(L"only open through social"), "batch keep false");
    run(L"web ig");             EXPECT(saw(L"No site called 'ig'"), "ig folded away");
    run(L"web social");         EXPECT(g_nopened == 2, "social still opens 2");
    run(L"/webconfig batch all media social yt yt true"); EXPECT(saw(L"(4 sites)"), "nested batches flatten + dedupe");
    run(L"/webconfig info all");
    run(L"/webconfig info yt"); EXPECT(saw(L"site") && saw(L"https://youtube.com"), "info site");
    run(L"/webconfig update media x.com");        EXPECT(saw(L"is a batch"), "update batch refused");
    run(L"/webconfig update yt youtu.be");        EXPECT(saw(L"yt now opens youtu.be"), "update");
    run(L"/webconfig info yt");                   EXPECT(saw(L"Changed"), "changed shows");
    run(L"web media");          EXPECT(!wcscmp(g_opened[0], L"https://youtube.com"), "batch kept its copy");
    run(L"/webconfig rename yt tube");            EXPECT(saw(L"Renamed yt to tube"), "rename old new");
    run(L"/webconfig rename tube fb");            EXPECT(saw(L"fb already exists"), "rename clash");
    run(L"/webconfig rename tube TUBE");          EXPECT(saw(L"Renamed tube to TUBE"), "rename case only");
    run(L"/webconfig remove social");             EXPECT(saw(L"Removed the social batch and its 2 sites"), "remove batch");
    run(L"web ig");             EXPECT(saw(L"No site called"), "folded singles don't come back");
    wprintf(L"   web-home.txt:\n"); cat("/tmp/loomytest\\web-home.txt");

    wprintf(L"\n== per-mode lists ==\n");
    run(L"/m switch work");
    run(L"web tube");           EXPECT(saw(L"No site called 'tube' in work mode"), "work list separate");
    run(L"/webconfig add tube jira.example.com");
    run(L"web tube");           EXPECT(!wcscmp(g_opened[0], L"https://jira.example.com"), "same alias, different site at work");
    run(L"/m switch home");
    run(L"web tube");           EXPECT(!wcscmp(g_opened[0], L"https://youtu.be"), "home alias intact");

    wprintf(L"\n== Tab ==\n");
    tab(L"/m", L"/mode ");
    tab(L"/mode s", 0);
    tab(L"/m sw", L"/m switch ");
    tab(L"/m switch w", L"/m switch work ");
    run(L"/m hide work");
    tab(L"/m show ", L"/m show work ");
    tab(L"/m switch ", 0);              // home and core left
    run(L"/m show work");
    tab(L"/webc", L"/webconfig ");
    tab(L"/webconfig re", 0);
    tab(L"/webconfig ren", L"/webconfig rename ");
    tab(L"/webconfig info me", L"/webconfig info media ");
    tab(L"web T", L"web TUBE ");
    tab(L"web ma", 0);
    tab(L"/webc batch x media f", 0);
    tab(L"/webc batch x media fa", L"/webc batch x media false ");
    tab(L"we", L"web ");

    wprintf(L"\n== panels ==\n");
    g_nopened = 0; web_open(0); web_key(VK_RETURN);
    EXPECT(g_nopened == 4 && !web_active(), "web panel: first A-Z is 'all' (4 sites), panel closes");
    g_nopened = 0; web_open(0); web_key(VK_DOWN); web_key(VK_RETURN);
    EXPECT(g_nopened == 1 && !wcscmp(g_opened[0], L"https://facebook.com/"), "web panel: second is fb");
    g_nopened = 0; web_open(0); web_char(L't'); web_key(VK_RETURN);
    EXPECT(g_nopened == 1 && !wcscmp(g_opened[0], L"https://youtu.be"), "web panel: letter jump to TUBE");
    // web many: stays open, marks rows, asks for focus back; plain web hides
    g_nopened = 0; g_refocus = 0; g_hidden = 0;
    web_open(1); web_key(VK_RETURN); web_key(VK_DOWN); web_key(VK_RETURN);
    EXPECT(web_active() && g_nopened == 5 && g_refocus == 2 && !g_hidden, "web many: two picks, panel stays, Loomy stays");
    web_key(VK_ESCAPE); EXPECT(!web_active(), "web many: Esc closes");
    run(L"web many");           EXPECT(web_active(), "web many opens the panel"); web_close();
    // remember position setting persists
    g_cfg.rememberPos = 1; cfg_save(); g_cfg.rememberPos = 0; cfg_load();
    EXPECT(g_cfg.rememberPos == 1, "remember_position saved + loaded");
    g_cfg.rememberPos = 0; cfg_save();
    mode_open(); mode_key(VK_DOWN); mode_key(VK_RETURN);
    EXPECT(mode_current() == 1 && !mode_active(), "mode panel: Down+Enter switches to work");
    run(L"/m hide home");
    mode_open(); mode_key(VK_UP); mode_key(VK_RETURN);
    EXPECT(mode_current() == 1, "mode panel: hidden home isn't listed");
    run(L"/m show home");

    wprintf(L"\n== versions ==\n");
    {
        struct { const wchar_t *a, *b; int gt; } V[] = {
            { L"2.10", L"2.9", 1 },  { L"2.9", L"2.10", 0 },  { L"2.9b", L"2.9", 1 },  { L"2.9", L"2.9b", 0 },
            { L"2.9c", L"2.9b", 1 }, { L"2.9b", L"2.9c", 0 }, { L"2.10", L"2.9c", 1 }, { L"2.9c", L"2.10", 0 },
            { L"2.9b", L"2.9b", 0 }, { L"2.9B", L"2.9b", 0 }, { L"2.9.1", L"2.9", 1 }, { L"3.0", L"2.99z", 1 },
            { L"2.x?!", L"2.9", 0 }, { L"", L"2.9", 0 },      { L"2.9", L"", 1 },      { L"..b..", L"2", 0 },
        };
        for (int i = 0; i < (int)(sizeof(V) / sizeof(V[0])); i++) {
            int r = ver_gt(V[i].a, V[i].b);
            wprintf(L"   %ls > %ls ? %d\n", V[i].a, V[i].b, r);
            EXPECT(r == V[i].gt, "ver_gt");
        }
    }

    wprintf(L"\n== release check (real v2.9b JSON from GitHub) ==\n");
    {
        static char real[256 * 1024], j[256 * 1024];
        FILE *f = fopen("fixtures/release_v2.9b.json", "rb");
        size_t n = f ? fread(real, 1, sizeof(real) - 1, f) : 0; if (f) fclose(f); real[n] = 0;
        char tag[32]; wchar_t ver[32];
        #define SWAP(from, to) do { strcpy(j, real); char *p = strstr(j, from); if (p) { memmove(p + strlen(to), p + strlen(from), strlen(p + strlen(from)) + 1); memcpy(p, to, strlen(to)); } } while (0)
        int r = release_check(real, "JimmyUns", tag, 32, ver, 32);
        wprintf(L"   real: %d tag=%s ver=%ls\n", r, tag, ver);
        EXPECT(r == REL_OK && !strcmp(tag, "v2.9b") && !wcscmp(ver, L"2.9b"), "real release trusted");
        EXPECT(release_check(real, "SomeoneElse", tag, 32, ver, 32) == REL_BAD_AUTHOR, "wrong owner refused");
        SWAP("\"login\": \"JimmyUns\"", "\"login\": \"LoomyBot\"");
        EXPECT(release_check(j, "JimmyUns", tag, 32, ver, 32) == REL_BAD_AUTHOR, "release made by the bot refused");
        SWAP("\"login\": \"github-actions[bot]\"", "\"login\": \"LoomyBot\"");
        EXPECT(release_check(j, "JimmyUns", tag, 32, ver, 32) == REL_BAD_ASSET, "exe uploaded by the bot refused");
        SWAP("\"name\": \"loomy.exe\"", "\"name\": \"other.exe\"");
        EXPECT(release_check(j, "JimmyUns", tag, 32, ver, 32) == REL_NO_ASSET, "no loomy.exe refused");
        SWAP("\"tag_name\": \"v2.9b\"", "\"tag_name\": \"v2.9b&calc\"");
        EXPECT(release_check(j, "JimmyUns", tag, 32, ver, 32) == REL_NO_RELEASE, "weird tag refused");
        EXPECT(release_check("{}", "JimmyUns", tag, 32, ver, 32) == REL_NO_RELEASE, "empty json");
        EXPECT(release_check("{\"tag_name\":\"v3.0\",\"author\":{\"login\":\"JimmyUns\"},\"assets\":[{\"name\":\"loomy.exe\",\"uploader\":{\"login\":\"github-actions[bot]\"}}]}",
                               "JimmyUns", tag, 32, ver, 32) == REL_OK, "compact json (no spaces) works too");
    }

    wprintf(L"\n%d failure(s)\n", fails);
    return fails != 0;
}
