#define _GNU_SOURCE
#include <windows.h>
#include <locale.h>
#include "../src/loomy.h"
#include "../src/pocket.h"
#include "../src/monocypher.h"
extern wchar_t g_out[4000][1100]; extern int g_nout;
extern wchar_t g_opened[64][512]; extern int g_nopened;
extern int g_topo, g_screens, g_setcalls, g_screensOff, g_settingsSec;
extern int g_timer, g_big, g_sysOpen, g_sysSamples, g_fonts, g_fakeGpus, g_shift;
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

// ---- Pocket Loomy: a real phone-role engine talking to phone.c through the fake radio
extern int g_shows, g_balloons, g_phoneTimer, g_btOk, g_btListening, g_btDrops, g_btSendFail, g_rngFail;
extern wchar_t g_balloonMsg[300]; extern unsigned char g_toPhone[8192]; extern int g_toPhoneN; extern ULONGLONG g_now;
static PkConn ph; static int ph_ev[16], ph_nev;
// move bytes both ways until nobody has anything left to say
static void pump(void) {
    for (int rounds = 0; rounds < 20; rounds++) {
        int moved = 0;
        if (g_toPhoneN) {
            unsigned char b[8192]; int n = g_toPhoneN; memcpy(b, g_toPhone, n); g_toPhoneN = 0; moved = 1;
            int off = 0;
            while (off < n) { int used, e = pk_feed(&ph, b + off, n - off, &used); off += used; if (e && ph_nev < 16) ph_ev[ph_nev++] = e; if (e == PK_EV_FAIL) break; }
        }
        if (ph.outlen) { int n = ph.outlen; unsigned char b[1100]; memcpy(b, ph.out, n); ph.outlen = 0; moved = 1; phone_on_data(b, n); }
        if (!moved) return;
    }
}
static int ph_saw(int e) { for (int i = 0; i < ph_nev; i++) if (ph_ev[i] == e) return 1; return 0; }
static uint8_t keep_sk[32], keep_pk[32], keep_pcpk[32];   // the phone's side of the pairing
static int phone_pair(const char *code) {
    ph_nev = 0;
    pk_init_phone(&ph, (const uint8_t *)code, 0, 0, 0, 0, (const uint8_t *)"Pixel 8", 7);
    if (!phone_on_accept(L"AA:BB:CC:DD:EE:01")) return 0;
    pump();
    if (ph_saw(PK_EV_PAIRED)) { memcpy(keep_sk, ph.my_sk, 32); memcpy(keep_pk, ph.my_pk, 32); memcpy(keep_pcpk, ph.peer_pk, 32); phone_on_closed(); return 1; }
    return 0;
}
static int phone_connect(void) {
    ph_nev = 0;
    pk_init_phone(&ph, 0, 1, keep_sk, keep_pk, keep_pcpk, (const uint8_t *)"Pixel 8", 7);
    if (!phone_on_accept(L"AA:BB:CC:DD:EE:01")) return 0;
    pump();
    return ph_saw(PK_EV_READY);
}
static const char *ask(const char *cmd) {      // say a command, return the PC's answer
    static char ans[PK_TEXTMAX + 1];
    ph_nev = 0; ans[0] = 0;
    pk_send_cmd(&ph, (const uint8_t *)cmd, (int)strlen(cmd));
    last_out = g_nout;
    pump();
    if (ph_saw(PK_EV_REPLY)) strcpy(ans, (char *)ph.text);
    wprintf(L"   phone> %s\n   pc   < %s\n", cmd, ans);
    return ans;
}
static void raw_to_pc(const unsigned char *b, int n) { last_out = g_nout; phone_on_data(b, n); }

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
    run(L"github");             EXPECT(saw(L"don't know 'github'") && g_nopened == 0, "github unknown in home");
    run(L"gh");                 EXPECT(saw(L"don't know 'gh'") && g_nopened == 0, "gh unknown in home");
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
    g_hidden = 0;
    run(L"github");             EXPECT(g_nopened == 1 && !wcscmp(g_opened[0], L"https://github.com/JimmyUns/Project-Loomy") && g_hidden, "github opens the repo, hides");
    g_hidden = 0;
    run(L"gh");                 EXPECT(g_nopened == 1 && g_hidden, "gh alias");
    run(L"github now");         EXPECT(saw(L"Usage: github") && g_nopened == 0, "github takes nothing");
    run(L"/github");            EXPECT(saw(L"no slash") && g_nopened == 0, "github is plain");
    tab(L"git", L"github ");
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

    wprintf(L"\n== display ==\n");
    run(L"display");            EXPECT(display_active() && g_setcalls == 0, "bare display opens picker, changes nothing");
    display_key(VK_RETURN);     EXPECT(!display_active() && g_topo == SDC_TOPOLOGY_EXTEND && g_hidden, "Enter on first row = Extend, hides Loomy");
    g_hidden = 0; display_open(); display_key(VK_DOWN); display_key(VK_RETURN);
    EXPECT(g_topo == SDC_TOPOLOGY_EXTERNAL, "second row = second screen only");
    display_open(); display_char(L'p'); display_key(VK_RETURN);   EXPECT(g_topo == SDC_TOPOLOGY_INTERNAL, "letter p jumps to PC screen only");
    display_open(); display_char(L'd'); display_key(VK_RETURN);   EXPECT(g_topo == SDC_TOPOLOGY_CLONE, "letter d = duplicate");
    display_open(); display_key(VK_END); display_key(VK_UP); display_key(VK_ESCAPE);
    EXPECT(!display_active() && g_topo == SDC_TOPOLOGY_CLONE, "Esc closes without switching");
    run(L"display Extend");     EXPECT(g_topo == SDC_TOPOLOGY_EXTEND && saw(L"Switched to Extend"), "by name, any case");
    run(L"dp second");          EXPECT(g_topo == SDC_TOPOLOGY_EXTERNAL, "dp alias");
    { int c = g_setcalls; g_hidden = 0;
    run(L"display second");     EXPECT(saw(L"Already on Second screen only") && g_setcalls == c && !g_hidden, "already: no switch, stays open"); }
    run(L"display pc");         EXPECT(g_topo == SDC_TOPOLOGY_INTERNAL, "pc");
    run(L"display duplicate");  EXPECT(g_topo == SDC_TOPOLOGY_CLONE, "duplicate");
    run(L"display mirror");     EXPECT(saw(L"No display layout called 'mirror'") && saw(L"Usage: display [extend|second|pc|duplicate|off]"), "unknown layout");
    run(L"display extend now"); EXPECT(saw(L"Usage: display [extend|second|pc|duplicate|off]"), "strict args");
    run(L"/display");           EXPECT(saw(L"no slash"), "display is plain");
    g_screens = 1; g_hidden = 0;
    run(L"display extend");     EXPECT(saw(L"couldn't switch to Extend") && saw(L"plugged in") && !g_hidden, "one screen: clear error, stays open");
    g_screens = 2;
    run(L"/m switch core");
    run(L"display extend");     EXPECT(g_topo == SDC_TOPOLOGY_EXTEND, "works in core too");
    run(L"/m switch home");
    tab(L"display s", L"display second ");
    tab(L"dp d", L"dp duplicate ");
    tab(L"display ", 0);
    tab(L"dp o", L"dp off ");
    { int c = g_setcalls, t = g_topo; g_hidden = 0; g_screensOff = 0;
    run(L"dp off");             EXPECT(g_screensOff == 1 && g_hidden && g_setcalls == c && g_topo == t && saw(L"Screens off"), "dp off: standby, hides, layout untouched");
    g_hidden = 0;
    run(L"display Off");        EXPECT(g_screensOff == 2 && g_hidden, "off, any case");
    run(L"dp off now");         EXPECT(g_screensOff == 2 && saw(L"Usage: dp [extend|second|pc|duplicate|off]"), "off takes nothing more");
    display_open(); display_key(VK_END); display_key(VK_RETURN);
    EXPECT(g_screensOff == 3 && g_setcalls == c && !display_active(), "End = last row = screens off");
    display_open(); display_char(L'o'); display_key(VK_RETURN);
    EXPECT(g_screensOff == 4 && g_topo == t, "letter o = screens off");
    display_open(); display_char(L'o'); display_key(VK_UP); display_key(VK_RETURN);
    EXPECT(g_screensOff == 4 && g_topo == SDC_TOPOLOGY_CLONE, "row above off is duplicate"); }
    tab(L"di", L"display ");

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
    // animation settings: defaults, save/load, hand-edited values
    wprintf(L"\n== animation settings ==\n");
    { FILE *f = fopen("/tmp/loomytest\\loomy.cfg", "w"); fprintf(f, "hotkey = ctrl+alt+space\n"); fclose(f); }
    cfg_load();  EXPECT(g_cfg.anim == ANIM_AUTO && g_cfg.animDots && g_cfg.animLines && g_cfg.animSelect && g_cfg.animText && !g_cfg.fadeIn,
                        "anim defaults: auto, all effects on, fade-in off");
    g_cfg.anim = ANIM_OFF; g_cfg.animDots = 0; g_cfg.animText = 0; g_cfg.fadeIn = 1; cfg_save();
    g_cfg.anim = ANIM_ON; g_cfg.animDots = 1; g_cfg.animText = 1; g_cfg.fadeIn = 0; cfg_load();
    EXPECT(g_cfg.anim == ANIM_OFF && !g_cfg.animDots && g_cfg.animLines && !g_cfg.animText && g_cfg.fadeIn, "anim settings saved + loaded");
    { FILE *f = fopen("/tmp/loomytest\\loomy.cfg", "w"); fprintf(f, "animations = ON\nanim_lines = no\nanim_select = 0\nfade_in = yes\n"); fclose(f); }
    cfg_load();  EXPECT(g_cfg.anim == ANIM_ON && !g_cfg.animLines && !g_cfg.animSelect && g_cfg.fadeIn && g_cfg.animDots, "hand-edited anim values");
    { FILE *f = fopen("/tmp/loomytest\\loomy.cfg", "w"); fprintf(f, "animations = sometimes\n"); fclose(f); }
    cfg_load();  EXPECT(g_cfg.anim == ANIM_AUTO, "unknown animations value falls back to auto");
    // fade speed: default, save/load, clamped, junk ignored
    { FILE *f = fopen("/tmp/loomytest\\loomy.cfg", "w"); fprintf(f, "hotkey = ctrl+alt+space\n"); fclose(f); }
    cfg_load();  EXPECT(g_cfg.fadeMs == FADE_DEF, "fade_ms default 300");
    g_cfg.fadeMs = 550; cfg_save(); g_cfg.fadeMs = 0; cfg_load();
    EXPECT(g_cfg.fadeMs == 550, "fade_ms saved + loaded");
    { FILE *f = fopen("/tmp/loomytest\\loomy.cfg", "w"); fprintf(f, "fade_ms = 20\n"); fclose(f); }
    cfg_load();  EXPECT(g_cfg.fadeMs == FADE_MIN, "fade_ms too small -> 100");
    { FILE *f = fopen("/tmp/loomytest\\loomy.cfg", "w"); fprintf(f, "fade_ms = 99999999\n"); fclose(f); }
    cfg_load();  EXPECT(g_cfg.fadeMs == FADE_MAX, "fade_ms too big -> 800");
    { FILE *f = fopen("/tmp/loomytest\\loomy.cfg", "w"); fprintf(f, "fade_ms = fast\n"); fclose(f); }
    cfg_load();  EXPECT(g_cfg.fadeMs == FADE_DEF, "fade_ms junk ignored");
    cfg_load(); g_cfg.anim = ANIM_AUTO; cfg_save();
    // /config sections
    run(L"/config");            EXPECT(g_settingsSec == 0, "/config opens General");
    run(L"/config custom");     EXPECT(g_settingsSec == 1, "/config custom");
    g_settingsSec = -1;
    run(L"/config Customization"); EXPECT(g_settingsSec == 1, "/config customization, any case");
    run(L"/config general");    EXPECT(g_settingsSec == 0, "/config general");
    g_settingsSec = -1;
    run(L"/config colors");     EXPECT(g_settingsSec == -1 && saw(L"No settings section called 'colors'") && saw(L"Usage: /config [general|custom]"), "unknown section");
    run(L"/config custom now"); EXPECT(g_settingsSec == -1 && saw(L"Usage: /config [general|custom]"), "config strict args");
    tab(L"/config c", L"/config custom ");
    tab(L"/config g", L"/config general ");
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


    wprintf(L"\n== sysinfo ==\n");
    run(L"sysinfo");
    EXPECT(sysinfo_active() && !sysinfo_expanded() && g_sysOpen && g_timer && !g_big, "bare sysinfo: picker at normal size, sensors on");
    sysinfo_key(VK_DOWN); sysinfo_key(VK_RETURN);
    EXPECT(sysinfo_expanded() && g_big && g_fonts, "Enter on CPU grows the window");
    { RECT a = { 0, 0, 1000, 600 }; sysinfo_paint(0, a); }
    sysinfo_tick(); EXPECT(g_sysSamples == 1, "a tick samples once");
    sysinfo_key(VK_TAB); sysinfo_key(VK_TAB); sysinfo_key(VK_TAB);   // cpu -> gpu1 -> gpu2 -> general
    sysinfo_char(L'c'); sysinfo_char(L'2'); sysinfo_char(L'9');       // 9: no such GPU, ignored
    sysinfo_key(VK_LEFT); sysinfo_key(VK_LEFT); sysinfo_key(VK_LEFT); sysinfo_key(VK_LEFT);   // gpu2 -> wraps all the way round
    g_shift = 1; sysinfo_key(VK_TAB); g_shift = 0;                    // shift+tab: back one (gpu1)
    sysinfo_key(VK_BACK);
    EXPECT(sysinfo_active() && !sysinfo_expanded() && !g_big && !g_fonts, "Backspace: back to the picker, normal size");
    sysinfo_key(VK_ESCAPE);
    EXPECT(!sysinfo_active() && !g_sysOpen && !g_timer && !g_big, "Esc closes and frees the sensors");

    run(L"si gpu 2");       EXPECT(sysinfo_expanded() && g_big, "si gpu 2 opens the second GPU");
    sysinfo_key(VK_ESCAPE);
    run(L"si gpu 3");       EXPECT(saw(L"There's no GPU 3. This PC has 2.") && !sysinfo_active() && !g_sysOpen, "GPU out of range: clear error, nothing left open");
    run(L"si gpu 0");       EXPECT(saw(L"There's no GPU 0") && !sysinfo_active(), "GPU 0");
    run(L"si gpu x");       EXPECT(saw(L"'x' isn't a GPU number") && saw(L"Usage: si [general|cpu|gpu [n]]") && !sysinfo_active(), "GPU not a number");
    run(L"si cpu now");     EXPECT(saw(L"Usage: si [general|cpu|gpu [n]]") && !sysinfo_active(), "strict args");
    run(L"si gpu 1 2");     EXPECT(saw(L"Usage: si") && !sysinfo_active(), "too many words");
    run(L"sysinfo banana"); EXPECT(saw(L"No sysinfo view called 'banana'") && saw(L"Usage: sysinfo"), "unknown view");
    run(L"/sysinfo");       EXPECT(saw(L"no slash") && !sysinfo_active(), "sysinfo is plain");
    run(L"SI General");     EXPECT(sysinfo_expanded(), "any case"); sysinfo_key(VK_ESCAPE);
    run(L"si gpu");         EXPECT(sysinfo_expanded(), "gpu alone = GPU 1"); sysinfo_key(VK_ESCAPE);
    g_fakeGpus = 0;
    run(L"si gpu");         EXPECT(saw(L"isn't showing me any GPU") && !sysinfo_active(), "no GPU at all");
    run(L"si");             EXPECT(sysinfo_active(), "picker still works with no GPU");
    sysinfo_key(VK_END); sysinfo_key(VK_RETURN); EXPECT(sysinfo_expanded(), "End = last row (CPU when there's no GPU)");
    sysinfo_char(L'1'); sysinfo_key(VK_ESCAPE);
    g_fakeGpus = 2;
    sysinfo_open(-1); sysinfo_click(5, 5, 0); sysinfo_wheel(-1); sysinfo_wheel(-5); sysinfo_key(VK_ESCAPE);
    EXPECT(!sysinfo_active(), "wheel/click in the picker don't crash");
    run(L"/m switch core"); run(L"si cpu"); EXPECT(sysinfo_expanded(), "works in core too"); sysinfo_key(VK_ESCAPE);
    run(L"/m switch home");
    run(L"help");           EXPECT(saw(L"System Info") && saw(L"sysinfo  si"), "in help with its alias");
    tab(L"si c", L"si cpu ");
    tab(L"sysinfo ge", L"sysinfo general ");
    tab(L"si g", L"si g");
    tab(L"sy", L"sysinfo ");

    wprintf(L"\n== Flow score ==\n");
    {
        SiSnap s; SiFlow f; memset(&s, 0, sizeof(s));
        s.logical = 16; s.memTotal = 16ull << 30; s.dpc = 2; s.hardFaults = 20;
        flow_cpu(&s, &f); EXPECT(f.score == -1 && wcsstr(f.why, L"Measuring"), "not measured yet");
        s.ready = 1;
        // gaming: busy, boosting, nothing waiting
        s.cpuUtil = 65; s.perfPct = 118; s.speedMHz = 4480; s.queue10 = 12; s.memUsed = 12ull << 30;
        flow_cpu(&s, &f); wprintf(L"   gaming: %d %ls\n", f.score, f.why);
        EXPECT(f.score == 100 && f.level == FLOW_SMOOTH && wcsstr(f.why, L"Busy, but keeping up"), "busy but smooth");
        // idle and downclocked to save power is not throttling
        s.cpuUtil = 5; s.perfPct = 40; s.speedMHz = 1520;
        flow_cpu(&s, &f); EXPECT(f.score == 100 && wcsstr(f.why, L"Relaxed"), "idle downclock isn't a penalty");
        // choking: queue piling up, held back, RAM full and paging
        s.cpuUtil = 98; s.perfPct = 57; s.speedMHz = 2150; s.queue10 = 150; s.memUsed = (16ull << 30) * 96 / 100; s.hardFaults = 1800;
        flow_cpu(&s, &f); wprintf(L"   choking: %d %ls\n", f.score, f.why);
        EXPECT(f.level == FLOW_CHOKING && f.score < 30, "choking");
        EXPECT(wcsstr(f.why, L"Held back to 2.15 GHz") == f.why && wcsstr(f.why, L"15 threads waiting"), "top two reasons, biggest first, capitalised");
        // drivers hogging a bit: still smooth, but says why
        s.cpuUtil = 30; s.perfPct = 100; s.queue10 = 0; s.memUsed = 8ull << 30; s.hardFaults = 0; s.dpc = 15;
        flow_cpu(&s, &f); wprintf(L"   dpc: %d %ls\n", f.score, f.why);
        EXPECT(f.level == FLOW_SMOOTH && wcsstr(f.why, L"Mostly fine: drivers using 15%"), "small issue mentioned");

        SiGpu g; memset(&g, 0, sizeof(g));
        g.memTotal = 8ull << 30; g.memUsed = 6ull << 30; g.tempC = 70; g.tempWarnC = -1; g.clkMHz = 1900; g.clkMaxMHz = 1950; g.util = 96;
        flow_gpu(&g, &f); EXPECT(f.score == 100 && wcsstr(f.why, L"Maxed at full clock"), "maxed GPU is normal");
        g.tempC = 90;
        flow_gpu(&g, &f); wprintf(L"   hot: %d %ls\n", f.score, f.why);
        EXPECT(f.level == FLOW_STRAINED && wcsstr(f.why, L"Hot (90"), "hot GPU");
        g.tempC = 80; g.tempWarnC = 80;
        flow_gpu(&g, &f); EXPECT(f.score < 100, "uses the driver's own warning temp");
        g.tempC = -1; g.memUsed = (8ull << 30) * 97 / 100; g.clkMHz = 1100; g.util = 80;
        flow_gpu(&g, &f); wprintf(L"   vram+clock: %d %ls\n", f.score, f.why);
        EXPECT(wcsstr(f.why, L"VRAM 96% full") && wcsstr(f.why, L"clock held at 1100 MHz"), "full VRAM + low clock");
        g.integrated = 1; g.clkMHz = -1; g.clkMaxMHz = -1; g.util = 2;
        flow_gpu(&g, &f); EXPECT(f.score == 100 && wcsstr(f.why, L"Idle"), "integrated: shared memory never counts as full VRAM");
    }

    wprintf(L"\n== features (/pkg) ==\n");
    {
        run(L"/m switch home");
        { FILE *f = fopen("/tmp/loomytest\\loomy.cfg", "w"); fprintf(f, "hotkey = ctrl+alt+space\n"); fclose(f); }
        cfg_load();
        EXPECT(g_cfg.featuresOff == 0 && feature_on(FEAT_SYSINFO) && feature_on(FEAT_WEB), "everything on by default");
        run(L"/pkg");               EXPECT(pkg_active(), "/pkg opens the toggle list");
        pkg_key(VK_ESCAPE);         EXPECT(!pkg_active(), "Esc closes it");

        run(L"/pkg remove sysinfo"); EXPECT(!feature_on(FEAT_SYSINFO) && saw(L"System Info is off on this PC") && saw(L"/pkg add sysinfo"), "remove sysinfo");
        run(L"si");                 EXPECT(saw(L"don't know 'si'") && !sysinfo_active(), "si unknown when off");
        run(L"sysinfo cpu");        EXPECT(saw(L"don't know 'sysinfo'"), "sysinfo unknown when off");
        run(L"help");               EXPECT(!saw(L"System Info") && saw(L"1 feature is turned off on this PC"), "help hides it, says 1 is off");
        tab(L"sys", L"sys");
        run(L"/pkg remove sysinfo"); EXPECT(saw(L"System Info is already off"), "already off");
        cfg_load();                 EXPECT(!feature_on(FEAT_SYSINFO), "off survives a reload (saved per PC)");

        run(L"/pkg remove web");    EXPECT(!feature_on(FEAT_WEB) && saw(L"Your saved sites are kept"), "remove web");
        run(L"web");                EXPECT(saw(L"don't know 'web'"), "web unknown");
        run(L"/webc info x");       EXPECT(saw(L"don't know '/webc'"), "its /webconfig goes with it");
        run(L"help");               EXPECT(saw(L"2 features are turned off"), "help counts 2");

        run(L"/pkg add sysinfo");   EXPECT(feature_on(FEAT_SYSINFO) && saw(L"System Info is on"), "add sysinfo back");
        run(L"si");                 EXPECT(sysinfo_active(), "si works again");
        sysinfo_close();
        run(L"/pkg add sysinfo");   EXPECT(saw(L"already on"), "already on");
        run(L"/pkg add web");       EXPECT(feature_on(FEAT_WEB), "web back");
        run(L"help");               EXPECT(!saw(L"turned off"), "help: nothing off, no note");

        run(L"/pkg remove help");   EXPECT(saw(L"help is part of Loomy's core"), "core: help");
        run(L"/pkg remove /config"); EXPECT(saw(L"/config is part of Loomy's core"), "core: /config");
        run(L"/pkg remove pkg");    EXPECT(saw(L"part of Loomy's core"), "core: pkg itself");
        run(L"/pkg remove update"); EXPECT(saw(L"part of Loomy's core"), "core: update");
        run(L"/pkg remove banana"); EXPECT(saw(L"No feature called 'banana'") && saw(L"Features: web, lp, net, rb, display, sysinfo, github, phone"), "unknown feature");
        run(L"/pkg remove");        EXPECT(saw(L"Usage: /pkg [add|remove <feature>]"), "remove needs a name");
        run(L"/pkg toggle web");    EXPECT(saw(L"Usage: /pkg [add|remove <feature>]"), "bad subcommand");
        run(L"/pkg add web now");   EXPECT(saw(L"Usage: /pkg"), "strict args");
        run(L"pkg");                EXPECT(saw(L"/pkg") && !pkg_active(), "pkg needs its slash");

        // github lives in core mode: off means gone there too
        run(L"/pkg remove github");
        run(L"/m switch core");
        run(L"gh");                 EXPECT(saw(L"don't know 'gh'") && g_nopened == 0, "github off in core");
        run(L"/ver");               EXPECT(saw(L"[version ran]"), "core commands unaffected");
        run(L"/pkg add github");
        run(L"gh");                 EXPECT(g_nopened == 1, "github back");
        run(L"/m switch home");

        tab(L"/pk", L"/pkg ");
        tab(L"/pkg r", L"/pkg remove ");
        tab(L"/pkg remove sys", L"/pkg remove sysinfo ");
        tab(L"/pkg add sys", L"/pkg add sys");                 // already on: nothing to add
        run(L"/pkg remove net");
        tab(L"/pkg add n", L"/pkg add net ");
        tab(L"/pkg remove n", L"/pkg remove n");               // already off: nothing to remove
        run(L"/pkg add net");

        // the toggle list
        pkg_open();
        pkg_key(VK_DOWN); pkg_key(VK_DOWN);                    // web, lp, net
        pkg_key(VK_RETURN);         EXPECT(!feature_on(FEAT_NET), "Enter toggles net off");
        pkg_key(VK_SPACE);          EXPECT(feature_on(FEAT_NET), "Space toggles it back");
        pkg_key(VK_RIGHT);          EXPECT(!feature_on(FEAT_NET), "Right toggles too");
        pkg_key(VK_LEFT);           EXPECT(feature_on(FEAT_NET), "Left toggles too");
        pkg_key(VK_END); pkg_key(VK_UP); pkg_key(VK_RETURN); EXPECT(!feature_on(FEAT_GITHUB), "End, Up = github");
        pkg_key(VK_RETURN);
        pkg_key(VK_HOME); pkg_key(VK_UP); pkg_key(VK_RETURN); EXPECT(!feature_on(FEAT_WEB), "Home/Up stay on web");
        pkg_key(VK_RETURN);
        pkg_char(L'x');             EXPECT(pkg_active(), "typing is swallowed");
        pkg_key(VK_ESCAPE);         EXPECT(!pkg_active() && g_cfg.featuresOff == 0, "all back on, closed");

        // config file: hand-edited, saved
        { FILE *f = fopen("/tmp/loomytest\\loomy.cfg", "w"); fprintf(f, "features_off = Net, banana, sysinfo\n"); fclose(f); }
        cfg_load();                 EXPECT(!feature_on(FEAT_NET) && !feature_on(FEAT_SYSINFO) && feature_on(FEAT_WEB), "hand-edited, unknown names ignored");
        cfg_save();
        { FILE *f = fopen("/tmp/loomytest\\loomy.cfg", "r"); char buf[4096] = {0}; fread(buf, 1, sizeof buf - 1, f); fclose(f);
          EXPECT(strstr(buf, "features_off = net, sysinfo\r\n") != 0, "saved as features_off = net, sysinfo"); }
        g_cfg.featuresOff = 0; cfg_save(); cfg_load();
        EXPECT(g_cfg.featuresOff == 0, "empty list saves and loads");
    }


    wprintf(L"\n== Pocket Loomy (phone) ==\n");
    {
        { FILE *f = fopen("/tmp/loomytest\\loomy.cfg", "w"); fprintf(f, "hotkey = ctrl+alt+space\n"); fclose(f); }
        remove("/tmp/loomytest\\phone.dat");
        cfg_load();
        run(L"/m switch home");
        EXPECT(!feature_on(FEAT_PHONE) && g_cfg.featuresOff == 0, "phone is opt-in: off on a fresh config");
        phone_startup();            EXPECT(!g_btListening, "off: nothing listens");
        run(L"/phone");             EXPECT(saw(L"don't know '/phone'") && !phone_active(), "off: /phone doesn't exist");
        run(L"help");               EXPECT(!saw(L"turned off"), "an opt-in feature being off isn't nagged about");
        run(L"/pkg add phone");     EXPECT(feature_on(FEAT_PHONE) && saw(L"Type /phone to pair"), "/pkg add phone");
        EXPECT(!g_btListening, "on, but nothing to listen for yet (no phone, no code)");
        { FILE *f = fopen("/tmp/loomytest\\loomy.cfg", "r"); char b[4096] = {0}; fread(b, 1, 4095, f); fclose(f);
          EXPECT(strstr(b, "features_on = phone\r\n") && strstr(b, "features_off = \r\n"), "saved as features_on = phone"); }
        cfg_load();                 EXPECT(feature_on(FEAT_PHONE), "survives a reload");
        run(L"/phone now");         EXPECT(saw(L"Usage: /phone [forget]"), "strict args");
        tab(L"/pho", L"/phone ");
        tab(L"/phone f", L"/phone forget ");

        // ---- pairing
        run(L"/phone");             EXPECT(phone_active() && g_btListening && g_phoneTimer == 1000, "/phone: code open, listening, ticking");
        PkConn spy; (void)spy;
        extern int phone_code_for_tests(char *out);
        char code[9] = {0}; EXPECT(phone_code_for_tests(code) && strlen(code) == 8 && strspn(code, "0123456789") == 8, "8-digit code");
        wprintf(L"   code: %s\n", code);
        char wrong[9]; strcpy(wrong, code); wrong[7] = wrong[7] == '9' ? '0' : wrong[7] + 1;
        last_out = g_nout;
        EXPECT(!phone_pair(wrong), "wrong code doesn't pair");
        EXPECT(ph.fail == PK_F_CODE, "the phone sees it was the wrong code");
        phone_on_closed();          // the phone gives up and hangs up
        EXPECT(saw(L"hung up before finishing") && !phone_code_for_tests(code) && !g_cfg.phoneLocked, "code burned by the try, no lockout for a hang-up");
        EXPECT(!g_btListening && !g_phoneTimer, "no code, no phone: listening stops, timer stops");
        phone_char(L'n');           EXPECT(phone_code_for_tests(code) && g_btListening, "N makes a new code");
        last_out = g_nout;
        EXPECT(phone_pair(code), "right code pairs");
        EXPECT(saw(L"Paired with \"Pixel 8\" (AA:BB:CC:DD:EE:01)"), "says who paired");
        EXPECT(!phone_code_for_tests(code) && g_btListening, "code gone, still listening for the trusted phone");
        EXPECT(!memcmp(ph.peer_name, "JIMMY-PC", 8), "phone got the PC's name");
        { FILE *f = fopen("/tmp/loomytest\\phone.dat", "rb"); unsigned char b[600]; int n = f ? (int)fread(b, 1, 600, f) : 0; if (f) fclose(f);
          EXPECT(n > 100 && b[0] == 'D' && b[1] == 'P' && !memmem(b, n, keep_pk, 32), "trust file written through DPAPI, keys not in the clear"); }
        phone_char(L'n');           EXPECT(!phone_code_for_tests(code), "no new codes while a phone is trusted");
        phone_key(VK_ESCAPE);       EXPECT(!phone_active(), "Esc closes");

        // ---- commands
        phone_startup();            // as if Loomy restarted: trust comes back from the file
        EXPECT(g_btListening && phone_connect(), "trusted phone connects after a restart");
        EXPECT(g_phoneTimer == 1000, "ticks while connected");
        g_nopened = 0;
        const char *a = ask("rb");  EXPECT(strstr(a, "Opening the recycle bin") && g_nopened == 1, "rb runs and answers");
        EXPECT(saw(L"From your phone:"), "Loomy shows it came from the phone");
        a = ask("  RB  ");          EXPECT(strstr(a, "recycle bin"), "trimmed, any case");
        a = ask("help");            EXPECT(!strcmp(a, "'help' only works on the PC."), "help isn't phone-safe");
        a = ask("lp");              EXPECT(strstr(a, "only works on the PC"), "lp isn't phone-safe");
        a = ask("/shutdown");       EXPECT(!strcmp(a, "Slash commands only work on the PC."), "no slash commands");
        a = ask("/pkg remove phone"); EXPECT(strstr(a, "Slash") && feature_on(FEAT_PHONE), "can't turn itself off");
        a = ask("/banana");         EXPECT(strstr(a, "don't know"), "unknown slash");
        a = ask("banana split");    EXPECT(!strcmp(a, "I don't know 'banana'.") && saw(L"banana split") && saw(L"I don't know 'banana'"), "unknown: answered AND shown on the PC");
        a = ask("gh");              EXPECT(strstr(a, "don't know"), "core-mode command doesn't exist in home");
        a = ask("rb now");          EXPECT(strstr(a, "Usage: rb"), "strict args still apply");
        int sh = g_shows; a = ask("open"); EXPECT(!strcmp(a, "Opening Loomy.") && g_shows == sh + 1, "open shows Loomy");
        g_hidden = 0; a = ask("hide"); EXPECT(!strcmp(a, "Hidden.") && g_hidden, "hide");
        a = ask("open sesame");     EXPECT(strstr(a, "don't know"), "open takes nothing");
        a = ask("display extend");  EXPECT(a[0] && strcmp(a, "Done."), "display answers with what it did");
        a = ask("rb\tx");           EXPECT(strstr(a, "control characters"), "control characters refused");
        { unsigned char bad[] = { 0xC3, 0x28 }; ph_nev = 0; pk_send_cmd(&ph, bad, 2); pump(); EXPECT(ph_saw(PK_EV_REPLY) && strstr((char *)ph.text, "couldn't read"), "invalid UTF-8 refused"); }
        run(L"/pkg remove web");
        a = ask("web");             EXPECT(strstr(a, "don't know"), "a /pkg-off feature is off for the phone too");
        run(L"/pkg add web");
        { ph_nev = 0; pk_send_ping(&ph); pump(); EXPECT(ph_saw(PK_EV_PONG), "ping, pong"); }
        { last_out = g_nout; pk_send_note(&ph, (const uint8_t *)"lumi displaay", 13); pump();
          EXPECT(saw(L"didn't catch a command (heard \"lumi displaay\")"), "a missed command shows on the PC");
          last_out = g_nout; pk_send_note(&ph, (const uint8_t *)"", 0); pump();
          EXPECT(saw(L"From your phone: didn't catch that."), "a miss with nothing heard too"); }
        EXPECT(!g_cfg.phoneLocked && g_balloons == 0, "all that was fine: no lockout");

        // second phone while busy
        EXPECT(!phone_on_accept(L"11:22:33:44:55:66"), "one phone at a time");
        // idle hang-up
        int drops = g_btDrops;
        g_now += 91; phone_tick(); EXPECT(g_btDrops == drops + 1 && !g_phoneTimer, "silent for 90 s: hung up, timer off");
        EXPECT(g_btListening && !g_cfg.phoneLocked, "still listening afterwards");

        // ---- lockouts
        EXPECT(phone_connect(), "reconnect");
        { unsigned char junk[] = { 0x01, 0xFF };              // length 511 then nothing: fine so far
          raw_to_pc(junk, 2); EXPECT(!g_cfg.phoneLocked, "a plausible length alone isn't an error"); }
        { unsigned char rest[600]; memset(rest, 0x41, sizeof rest); raw_to_pc(rest, 511); }
        EXPECT(g_cfg.phoneLocked && !g_btListening && g_balloons == 1, "a frame that fails its check: shut down + balloon");
        EXPECT(saw(L"Phone listener shut down") && saw(L"AA:BB:CC:DD:EE:01") && saw(L"Nothing got in"), "says what happened and from where");
        EXPECT(!phone_on_accept(L"AA:BB:CC:DD:EE:01"), "nothing gets in while shut down");
        cfg_load();                 EXPECT(g_cfg.phoneLocked, "shut down survives a restart");
        phone_startup();            EXPECT(!g_btListening, "and stays off after one");
        run(L"/phone");             EXPECT(phone_active() && !g_btListening, "/phone shows it, doesn't reopen by itself");
        phone_key(VK_RETURN);       EXPECT(!g_cfg.phoneLocked && g_btListening, "Enter turns listening back on");
        phone_key(VK_ESCAPE);

        EXPECT(phone_connect(), "back in");
        { unsigned char big[2] = { 0x02, 0x01 }; raw_to_pc(big, 2); }
        EXPECT(g_cfg.phoneLocked && saw(L"bad size"), "an oversized length shuts it down after 2 bytes");
        g_cfg.phoneLocked = 0; phone_open(); phone_close();

        // an unknown phone (other keys) trying to connect: it can't confirm, sends a made-up tag
        {
            uint8_t sk[32], pk[32]; os_random(sk, 32); crypto_x25519_public_key(pk, sk);
            ph_nev = 0; last_out = g_nout;
            pk_init_phone(&ph, 0, 1, sk, pk, keep_pcpk, (const uint8_t *)"Evil", 4);
            phone_on_accept(L"66:66:66:66:66:66"); pump();
            EXPECT(ph.fail == PK_F_AUTH, "the impostor can't verify the PC");
            unsigned char fake[35] = { 0, 33, 0x07 }; os_random(fake + 3, 32);
            raw_to_pc(fake, 35);
            EXPECT(g_cfg.phoneLocked && saw(L"isn't the trusted one") && saw(L"\"Evil\"") && saw(L"66:66:66:66:66:66"), "impostor: shut down, claimed name + address printed");
        }
        g_cfg.phoneLocked = 0; cfg_save(); phone_open(); phone_close();

        // random junk after connecting, many times: always a shutdown, never a command
        {
            int lock = 0, ran = 0;
            for (int i = 0; i < 300; i++) {
                g_nopened = 0; g_cfg.phoneLocked = 0;
                phone_open(); phone_close();
                phone_on_accept(L"77:77:77:77:77:77"); g_toPhoneN = 0;
                unsigned char j[700]; int n = 3 + (int)(i * 7 % 690);
                os_random(j, n); if (i % 2) { j[0] = 0; j[1] = (unsigned char)(n - 2 > 255 ? 255 : n - 2); }
                raw_to_pc(j, n);
                if (g_cfg.phoneLocked) lock++;
                if (g_nopened) ran++;
                if (!g_cfg.phoneLocked) phone_on_closed();
            }
            wprintf(L"   300 junk connections: %d shut it down, %d ran something\n", lock, ran);
            EXPECT(ran == 0 && lock >= 290, "junk never runs anything, nearly always trips the shutdown");
        }
        g_cfg.phoneLocked = 0; cfg_save(); phone_open(); phone_close();

        // handshake timeout: a phone that connects and says nothing
        { int d = g_btDrops; phone_on_accept(L"88:88:88:88:88:88"); g_toPhoneN = 0;
          g_now += 11; phone_tick(); EXPECT(g_btDrops == d + 1 && !g_cfg.phoneLocked, "slow handshake: cut, no shutdown"); }

        // random numbers fail: nothing is sent
        { g_rngFail = 1; g_toPhoneN = 0; EXPECT(!phone_on_accept(L"99:99:99:99:99:99") && g_toPhoneN == 0 && g_cfg.phoneLocked, "no secure random: nothing sent, shut down");
          g_rngFail = 0; g_cfg.phoneLocked = 0; cfg_save(); phone_open(); phone_close(); }

        // trust runs out after 7 days
        EXPECT(phone_connect(), "still trusted");
        phone_on_closed();
        g_now += 7ull * 24 * 3600;
        EXPECT(!phone_on_accept(L"AA:BB:CC:DD:EE:01") && !g_btListening && !g_cfg.phoneLocked, "after 7 days: turned away quietly, listening stops");
        { FILE *f = fopen("/tmp/loomytest\\phone.dat", "rb"); EXPECT(!f, "trust file deleted"); if (f) fclose(f); }
        run(L"/phone");             EXPECT(phone_code_for_tests(code), "/phone offers a new code");
        EXPECT(phone_pair(code), "pair again");
        phone_key(VK_ESCAPE);

        // the phone forgets: the PC follows right away
        EXPECT(phone_connect(), "connected before the phone forgets");
        { last_out = g_nout; pk_send_forget(&ph); pump();
          EXPECT(saw(L"\"Pixel 8\" unpaired from the phone, so Loomy forgot it too.") && !g_btListening, "phone's Forget reaches the PC");
          FILE *f = fopen("/tmp/loomytest\\phone.dat", "rb"); EXPECT(!f, "trust file gone"); if (f) fclose(f); }
        run(L"/phone"); phone_code_for_tests(code); EXPECT(phone_pair(code), "pair again after"); phone_key(VK_ESCAPE);
        // forget
        run(L"/phone forget");      EXPECT(saw(L"Forgot \"Pixel 8\"") && !g_btListening, "/phone forget");
        run(L"/phone forget");      EXPECT(saw(L"No phone is trusted"), "nothing to forget");
        run(L"/phone"); phone_code_for_tests(code); EXPECT(phone_pair(code), "pair again"); phone_key(VK_ESCAPE);
        run(L"/phone");             phone_char(L'f'); EXPECT(g_btListening, "F once only asks");
        phone_char(L'f');           EXPECT(!g_btListening, "F twice forgets");
        phone_key(VK_ESCAPE);

        // /pkg remove phone drops everything
        run(L"/phone"); phone_code_for_tests(code); EXPECT(phone_pair(code), "pair again"); phone_key(VK_ESCAPE);
        EXPECT(phone_connect(), "connected");
        { int d = g_btDrops; run(L"/pkg remove phone"); EXPECT(!feature_on(FEAT_PHONE) && !g_btListening && g_btDrops == d + 1, "/pkg remove phone: hung up, not listening"); }
        EXPECT(saw(L"paired phone is kept"), "says the pairing is kept");
        run(L"/pkg add phone");     EXPECT(g_btListening, "back on: listens for the kept phone");

        // no Bluetooth
        run(L"/phone forget"); g_btOk = 0;
        run(L"/phone");             EXPECT(!g_btListening, "no radio: not listening");
        g_btOk = 1; phone_key(VK_ESCAPE);
        run(L"/pkg remove phone");
    }

    wprintf(L"\n%d failure(s)\n", fails);
    return fails != 0;
}
