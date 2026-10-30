// phone.c - Pocket Loomy, PC side. Your Android phone hears "Loomy, display extend" and sends it
// here over Bluetooth; Loomy runs it like you typed it and answers. Opt-in: /pkg add phone.
//
//   /phone            the connection screen: a pairing code (no phone trusted yet) or the trusted
//                     phone and whether it's around. N = new code, F = forget phone, Enter = turn
//                     listening back on after a shutdown, Esc closes.
//   /phone forget     drop the trusted phone
//
// The rules (POCKET-HANDOVER.md has the why):
//   - Listening only happens with the feature on, not shut down, and either a trusted phone or an
//     open pairing code. No timers run while it just waits.
//   - A code is 8 random digits, lives 2 minutes, and is used up by the first try, right or wrong.
//     A new one only comes from you (opening /phone, or N).
//   - A paired phone is trusted for 7 days from pairing, then it has to pair again.
//   - Any failed check, bad frame or unknown phone shuts the listener down completely, prints who
//     tried (the name is only what the phone claimed) and pops a tray note. Only /phone reopens it.
//   - Only plain commands marked .phone in commands.c run, through the normal cmd_run.
// The protocol is pocket.c; the Windows Bluetooth and OS bits are phonebt.c.
#include "loomy.h"
#include "pocket.h"
#include "monocypher.h"

#define CODE_SECS      120
#define TRUST_SECS     (7ull * 24 * 3600)
#define HANDSHAKE_SECS 10          // a connection has this long to finish its handshake (or to hang up after pairing)
#define IDLE_SECS      90          // a connected phone that says nothing for this long gets hung up on
#define LOGN           5

// ---------------------------------------------------------------- state
typedef struct {
    int have;
    uint8_t pc_sk[32], pc_pk[32], phone_pk[32];
    uint8_t name[PK_NAMEMAX]; int namelen;
    char addr[18];
    ULONGLONG paired, expires;
} Trust;
static Trust g_trust;
static int   g_trustLoaded;

static char      g_code[PK_CODELEN];
static int       g_codeLive, g_codeGone;          // gone: 1 = used, 2 = ran out
static ULONGLONG g_codeEnds;

static wchar_t g_lockWhy[220];
static wchar_t g_btErr[120];
static int     g_rngBad;

static PkConn    g_pk;
static int       g_conn, g_wasReady;
static wchar_t   g_connAddr[20];
static ULONGLONG g_connStart, g_lastSeen, g_lastSeenEver;

static wchar_t g_log[LOGN][200];
static int     g_nlog;

static int     g_capturing;
static wchar_t g_cap[600];

static ULONGLONG now(void) { return os_now(); }

// pocket.c asks for randomness here. If Windows ever refuses, nothing generated from it may leave
// the PC: the flag is checked after every engine call, before anything is sent.
void pk_random(uint8_t *p, int n) {
    if (!os_random(p, n)) { g_rngBad = 1; for (int i = 0; i < n; i++) p[i] = 0; }
}

// ---------------------------------------------------------------- small helpers
// a name the phone sent (UTF-8, unauthenticated until the handshake is done): printable or '?'
static void name_w(const uint8_t *u8, int n, wchar_t *out, int cap) {
    int k = n > 0 ? MultiByteToWideChar(CP_UTF8, 0, (const char *)u8, n, out, cap - 1) : 0;
    if (k < 0) k = 0;
    out[k] = 0;
    for (int i = 0; i < k; i++) if (out[i] < 0x20 || out[i] == 0x7F || out[i] == L'"') out[i] = L'?';
    if (!k) s_cpy(out, L"(no name)", cap);
}

static void trust_name(wchar_t *out, int cap) { name_w(g_trust.name, g_trust.namelen, out, cap); }

static void log_add(const wchar_t *fmt, ...) {
    wchar_t msg[1025], t[24];
    va_list ap; va_start(ap, fmt); wvsprintfW(msg, fmt, ap); va_end(ap);
    os_fmt_time(now(), t, 24, 0);
    if (g_nlog == LOGN) { for (int i = 1; i < LOGN; i++) s_cpy(g_log[i - 1], g_log[i], 200); g_nlog--; }
    s_cpy(g_log[g_nlog], t, 200);
    s_cat(g_log[g_nlog], L"  ", 200);
    s_cat(g_log[g_nlog], msg, 200);
    g_nlog++;
}

static void timer_update(void) {
    ui_set_timer(TIMER_PHONE, (g_codeLive || g_conn) ? 1000 : 0);
}

static const wchar_t *trust_path(void) {
    static wchar_t p[MAX_PATH];
    s_cpy(p, cfg_dir(), MAX_PATH);
    s_cat(p, L"\\phone.dat", MAX_PATH);
    return p;
}

// ---------------------------------------------------------------- the trusted phone (DPAPI file)
// plain layout before DPAPI: "LPT1" | pc_sk | pc_pk | phone_pk | paired(8) | expires(8) | namelen | name[48] | addr[17]
#define TRUST_BYTES (4 + 32 * 3 + 16 + 1 + PK_NAMEMAX + 17)

static void put64(uint8_t *p, ULONGLONG v) { for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static ULONGLONG get64(const uint8_t *p) { ULONGLONG v = 0; for (int i = 7; i >= 0; i--) v = v << 8 | p[i]; return v; }

static int trust_save(void) {
    uint8_t plain[TRUST_BYTES], sealed[1024];
    memset(plain, 0, sizeof plain);
    uint8_t *p = plain;
    p[0] = 'L'; p[1] = 'P'; p[2] = 'T'; p[3] = '1'; p += 4;
    memcpy(p, g_trust.pc_sk, 32); p += 32;
    memcpy(p, g_trust.pc_pk, 32); p += 32;
    memcpy(p, g_trust.phone_pk, 32); p += 32;
    put64(p, g_trust.paired); p += 8;
    put64(p, g_trust.expires); p += 8;
    *p++ = (uint8_t)g_trust.namelen;
    memcpy(p, g_trust.name, g_trust.namelen); p += PK_NAMEMAX;
    memcpy(p, g_trust.addr, 17);
    int n = os_protect(plain, TRUST_BYTES, sealed, sizeof sealed, 0);
    crypto_wipe(plain, sizeof plain);
    if (n <= 0) return 0;
    HANDLE f = CreateFileW(trust_path(), GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD w = 0;
    int ok = WriteFile(f, sealed, n, &w, 0) && (int)w == n;
    CloseHandle(f);
    return ok;
}

static void trust_load(void) {
    g_trustLoaded = 1;
    crypto_wipe(&g_trust, sizeof g_trust);
    uint8_t sealed[1024], plain[TRUST_BYTES + 16];
    HANDLE f = CreateFileW(trust_path(), GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD n = 0;
    ReadFile(f, sealed, sizeof sealed, &n, 0);
    CloseHandle(f);
    int k = os_protect(sealed, (int)n, plain, sizeof plain, 1);
    const uint8_t *p = plain;
    if (k == TRUST_BYTES && p[0] == 'L' && p[1] == 'P' && p[2] == 'T' && p[3] == '1') {
        p += 4;
        memcpy(g_trust.pc_sk, p, 32); p += 32;
        memcpy(g_trust.pc_pk, p, 32); p += 32;
        memcpy(g_trust.phone_pk, p, 32); p += 32;
        g_trust.paired = get64(p); p += 8;
        g_trust.expires = get64(p); p += 8;
        g_trust.namelen = *p++;
        if (g_trust.namelen <= PK_NAMEMAX && g_trust.expires > g_trust.paired &&
            g_trust.expires - g_trust.paired <= TRUST_SECS) {
            memcpy(g_trust.name, p, g_trust.namelen); p += PK_NAMEMAX;
            memcpy(g_trust.addr, p, 17); g_trust.addr[17] = 0;
            g_trust.have = 1;
        }
    }
    crypto_wipe(plain, sizeof plain);
    if (!g_trust.have) crypto_wipe(&g_trust, sizeof g_trust);
}

static void forget_trust(void) {
    crypto_wipe(&g_trust, sizeof g_trust);
    DeleteFileW(trust_path());
}

// trusted until 7 days after pairing. A clock that jumped back more than an hour also ends it.
static int trust_valid(void) {
    ULONGLONG t = now();
    return g_trust.have && t < g_trust.expires && t + 3600 >= g_trust.paired;
}

// ---------------------------------------------------------------- listener on / off
static void burn_code(int why) {
    if (!g_codeLive) return;
    crypto_wipe(g_code, sizeof g_code);
    g_codeLive = 0;
    g_codeGone = why;
}

static void end_conn(void) {
    pk_wipe(&g_pk);
    g_conn = g_wasReady = 0;
    g_connAddr[0] = 0;
    timer_update();
    ui_redraw();
}

static void drop_conn(void) {
    if (!g_conn) return;
    bt_drop();
    end_conn();
}

static void update_listener(void) {
    if (g_trust.have && !trust_valid()) {
        wchar_t nm[64]; trust_name(nm, 64);
        log_add(L"Trust for \"%s\" ran out. Pair again.", nm);
        drop_conn();
        forget_trust();
    }
    if (g_codeLive && now() >= g_codeEnds) { burn_code(2); log_add(L"The pairing code ran out."); }
    int want = feature_on(FEAT_PHONE) && !g_cfg.phoneLocked && (g_trust.have || g_codeLive);
    if (want && !bt_listening()) { if (bt_listen(g_btErr, 120)) g_btErr[0] = 0; }
    if (!want && bt_listening()) bt_unlisten();
    if (!want) g_btErr[0] = 0;
    timer_update();
    ui_redraw();
}

// ---------------------------------------------------------------- lockout
static void lockout(const wchar_t *what) {
    drop_conn();
    burn_code(1);
    g_cfg.phoneLocked = 1;
    cfg_save();
    s_cpy(g_lockWhy, what, 220);
    update_listener();
    log_add(L"Shut down: %s", what);
    outf(OUT_ERR, L"Phone listener shut down: %s", what);
    out(OUT_DIM, L"Nothing got in. /phone turns listening back on.");
    ui_balloon(L"Loomy: phone listener shut down", what);
}

// the engine failed: say what, who claimed to be calling and from which address, then shut down
static void engine_failed(void) {
    wchar_t why[96], nm[64], msg[220];
    const char *t = pk_fail_text(g_pk.fail);
    int i = 0;
    for (; t[i] && i < 95; i++) why[i] = (wchar_t)t[i];
    why[i] = 0;
    if (g_pk.peer_namelen) {
        name_w(g_pk.peer_name, g_pk.peer_namelen, nm, 64);
        wsprintfW(msg, L"%s, from a phone calling itself \"%s\" (%s).", why, nm, g_connAddr);
    } else wsprintfW(msg, L"%s, from %s.", why, g_connAddr);
    msg[0] = (wchar_t)(msg[0] >= L'a' && msg[0] <= L'z' ? msg[0] - 32 : msg[0]);
    lockout(msg);
}

// ---------------------------------------------------------------- pairing codes
static void new_code(void) {
    if (g_cfg.phoneLocked || g_trust.have) return;
    unsigned v = 0;
    do {
        if (!os_random(&v, sizeof v)) { lockout(L"Windows wouldn't give Loomy secure random numbers."); return; }
    } while (v >= 4000000000u);                          // no bias: 4e9 is a multiple of 1e8
    v %= 100000000u;
    for (int i = PK_CODELEN - 1; i >= 0; i--) { g_code[i] = (char)('0' + v % 10); v /= 10; }
    g_codeLive = 1;
    g_codeGone = 0;
    g_codeEnds = now() + CODE_SECS;
    update_listener();
}

// ---------------------------------------------------------------- the connection
static void flush(void) {
    if (g_rngBad) { lockout(L"Windows wouldn't give Loomy secure random numbers."); return; }
    if (!g_pk.outlen) return;
    int ok = bt_send(g_pk.out, g_pk.outlen);
    g_pk.outlen = 0;
    if (!ok) { log_add(L"Couldn't send to %s; hung up.", g_connAddr); drop_conn(); }
}

int phone_on_accept(const wchar_t *addr) {
    if (g_conn) { log_add(L"Turned away %s: already talking to a phone.", addr); return 0; }
    update_listener();
    if (!bt_listening()) return 0;                       // nothing to listen for any more
    uint8_t name[64];
    int nl = os_pc_name((char *)name, PK_NAMEMAX);
    g_rngBad = 0;
    s_cpy(g_connAddr, addr, 20);
    g_conn = 1;
    g_wasReady = 0;
    g_connStart = g_lastSeen = now();
    pk_init_pc(&g_pk, g_codeLive ? (const uint8_t *)g_code : 0, g_trust.have,
               g_trust.pc_sk, g_trust.pc_pk, g_trust.phone_pk, name, nl);
    flush();                                             // HELLO
    if (!g_conn) return 0;
    timer_update();
    ui_redraw();
    return 1;
}

static void paired(void) {
    crypto_wipe(&g_trust, sizeof g_trust);
    memcpy(g_trust.pc_sk, g_pk.my_sk, 32);
    memcpy(g_trust.pc_pk, g_pk.my_pk, 32);
    memcpy(g_trust.phone_pk, g_pk.peer_pk, 32);
    g_trust.namelen = g_pk.peer_namelen;
    memcpy(g_trust.name, g_pk.peer_name, g_pk.peer_namelen);
    for (int i = 0; i < 17 && g_connAddr[i]; i++) g_trust.addr[i] = (char)g_connAddr[i];
    g_trust.paired = now();
    g_trust.expires = g_trust.paired + TRUST_SECS;
    g_trust.have = 1;
    wchar_t nm[64], until[40];
    trust_name(nm, 64);
    os_fmt_time(g_trust.expires, until, 40, 1);
    log_add(L"Paired with \"%s\".", nm);
    outf(OUT_OK, L"Paired with \"%s\" (%s). Trusted until %s.", nm, g_connAddr, until);
    if (!trust_save()) out(OUT_ERR, L"Couldn't save the pairing, so it only lasts until Loomy closes.");
}

static void reply(const wchar_t *w) {
    wchar_t buf[320];
    char u8[PK_TEXTMAX + 1];
    s_cpy(buf, w, 320);
    int len = s_len(buf);
    for (;;) {
        int need = len ? WideCharToMultiByte(CP_UTF8, 0, buf, len, 0, 0, 0, 0) : 0;
        if (need >= 0 && need <= PK_TEXTMAX) {
            if (len) WideCharToMultiByte(CP_UTF8, 0, buf, len, u8, PK_TEXTMAX, 0, 0);
            pk_send_reply(&g_pk, (const uint8_t *)u8, need);
            return;
        }
        len -= 8;                                        // too long for one frame: trim the end
        if (len > 0 && buf[len - 1] >= 0xD800 && buf[len - 1] <= 0xDBFF) len--;   // never split a pair
        if (len < 0) len = 0;
    }
}

void phone_out_tap(int kind, const wchar_t *text) {
    if (!g_capturing || kind == OUT_ECHO) return;
    if (g_cap[0]) s_cat(g_cap, L"\n", 600);
    s_cat(g_cap, text, 600);
}

// what the phone may do: open/show and hide Loomy, and the plain commands marked .phone
static void run_from_phone(void) {
    wchar_t line[PK_TEXTMAX + 1];
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (const char *)g_pk.text, g_pk.textlen, line, PK_TEXTMAX);
    if (n <= 0) { reply(L"I couldn't read that."); return; }
    line[n] = 0;
    for (int i = 0; i < n; i++)
        if (line[i] < 0x20 || line[i] == 0x7F) { reply(L"That had control characters in it."); return; }
    wchar_t *t = s_trim(line);
    if (!*t) { reply(L"Say a command."); return; }

    wchar_t first[48];
    int k = 0;
    while (t[k] && t[k] != L' ' && k < 47) { first[k] = t[k]; k++; }
    first[k] = 0;
    int alone = !t[k] || !*s_trim(t + k);
    if (alone && (s_ieq(first, L"open") || s_ieq(first, L"show"))) { ui_show(); reply(L"Opening Loomy."); return; }
    if (alone && s_ieq(first, L"hide")) { ui_hide(); reply(L"Hidden."); return; }
    wchar_t msg[160];
    int ok = cmd_phone_ok(first);
    if (first[0] == L'/' && ok) { reply(L"Slash commands only work on the PC."); return; }
    if (ok == 0) { wsprintfW(msg, L"I don't know '%s'.", first); reply(msg); return; }
    if (ok < 0)  { wsprintfW(msg, L"'%s' only works on the PC.", first); reply(msg); return; }

    out(OUT_DIM, L"From your phone:");
    g_cap[0] = 0;
    g_capturing = 1;
    cmd_run(t);
    g_capturing = 0;
    reply(g_cap[0] ? g_cap : L"Done.");
    ui_show_if_panel();
}

void phone_on_data(const unsigned char *p, int n) {
    while (n > 0 && g_conn) {
        int used = 0, ev = pk_feed(&g_pk, p, n, &used);
        p += used; n -= used;
        if (g_rngBad) { lockout(L"Windows wouldn't give Loomy secure random numbers."); return; }
        wchar_t nm[64];
        switch (ev) {
        case PK_EV_ATTEMPT:
            burn_code(1);
            name_w(g_pk.peer_name, g_pk.peer_namelen, nm, 64);
            log_add(L"\"%s\" (%s) used the pairing code.", nm, g_connAddr);
            flush();
            update_listener();
            break;
        case PK_EV_PAIRED:
            paired();
            flush();
            g_connStart = now();                         // it gets a moment to hang up by itself
            update_listener();
            break;
        case PK_EV_READY:
            g_wasReady = 1;
            g_lastSeen = g_lastSeenEver = now();
            flush();
            ui_redraw();
            break;
        case PK_EV_CMD:
            g_lastSeen = g_lastSeenEver = now();
            run_from_phone();
            flush();
            break;
        case PK_EV_PING:
            g_lastSeen = g_lastSeenEver = now();
            flush();
            ui_redraw();
            break;
        case PK_EV_FAIL:
            engine_failed();
            return;
        default:
            flush();
        }
    }
}

void phone_on_closed(void) {
    if (!g_conn) return;
    if (pk_mid_pairing(&g_pk)) {
        log_add(L"Pairing from %s stopped halfway. That code is used up.", g_connAddr);
        out(OUT_ERR, L"A phone used the pairing code but hung up before finishing. /phone, N for a new code.");
    } else if (!g_wasReady && !pk_done(&g_pk)) {
        log_add(L"%s hung up during the handshake.", g_connAddr);
    }
    end_conn();
    update_listener();
}

void phone_tick(void) {
    ULONGLONG t = now();
    if (g_codeLive && t >= g_codeEnds) { burn_code(2); log_add(L"The pairing code ran out."); update_listener(); }
    if (g_conn) {
        int ready = pk_ready(&g_pk);
        if (!ready && t - g_connStart >= HANDSHAKE_SECS) {
            if (pk_mid_pairing(&g_pk)) out(OUT_ERR, L"A phone used the pairing code but stalled. /phone, N for a new code.");
            log_add(L"%s took too long; hung up.", g_connAddr);
            drop_conn();
            update_listener();
        } else if (ready && t - g_lastSeen >= IDLE_SECS) {
            drop_conn();
        }
    }
    timer_update();
    if (phone_active()) ui_redraw();
}

// ---------------------------------------------------------------- startup, /pkg
void phone_startup(void) {
    if (!feature_on(FEAT_PHONE)) return;
    trust_load();
    update_listener();
}

void phone_feature_changed(void) {
    if (feature_on(FEAT_PHONE)) {
        if (!g_trustLoaded) trust_load();
    } else {
        drop_conn();
        burn_code(0);
        phone_close();
    }
    update_listener();
}

// ---------------------------------------------------------------- /phone
static void do_forget(void) {
    if (!g_trust.have) { out(OUT_DIM, L"No phone is trusted."); return; }
    wchar_t nm[64]; trust_name(nm, 64);
    drop_conn();
    forget_trust();
    log_add(L"Forgot \"%s\".", nm);
    outf(OUT_OK, L"Forgot \"%s\". It can't connect any more; /phone to pair again.", nm);
    update_listener();
}

void cmd_phone(wchar_t *args) {
    wchar_t *w[2];
    int n = s_words(args, w, 2);
    if (n == 0) { phone_open(); return; }
    if (n == 1 && s_ieq(w[0], L"forget")) { do_forget(); return; }
    cmd_usage(L"[forget]");
}

void phone_complete(const wchar_t *partial) { complete_add(L"forget", partial); }

// ---------------------------------------------------------------- the screen
static int   g_active, g_confirmForget;
static HFONT g_big;

int phone_active(void) { return g_active; }

void phone_open(void) {
    settings_close();
    if (!g_trustLoaded) trust_load();
    g_active = 1;
    g_confirmForget = 0;
    if (!g_big) g_big = CreateFontW(-S(40), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS,
                                    CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Cascadia Mono");
    if (!g_cfg.phoneLocked && !g_trust.have && !g_codeLive) new_code();   // opening it is the deliberate action
    update_listener();
    ui_set_input(L"");
    ui_redraw();
}

void phone_close(void) {
    if (!g_active) return;
    g_active = 0;
    if (g_big) { DeleteObject(g_big); g_big = 0; }
    ui_redraw();
}

static void unlock(void) {
    g_cfg.phoneLocked = 0;
    cfg_save();
    g_lockWhy[0] = 0;
    log_add(L"Listening turned back on.");
    if (!g_trust.have) new_code();
    update_listener();
}

int phone_key(WPARAM vk) {
    if (vk == VK_ESCAPE) { phone_close(); return 1; }
    if (vk == VK_RETURN && g_cfg.phoneLocked) { unlock(); return 1; }
    return 1;
}

int phone_char(WPARAM ch) {
    wchar_t c = s_low((wchar_t)ch);
    if (c == L'n' && !g_cfg.phoneLocked && !g_trust.have) { burn_code(0); new_code(); }
    else if (c == L'f' && g_trust.have) {
        if (g_confirmForget) { g_confirmForget = 0; do_forget(); }
        else g_confirmForget = 1;
        ui_redraw();
        return 1;
    }
    g_confirmForget = 0;
    ui_redraw();
    return 1;
}

void phone_click(int x, int y, int dbl) {}
void phone_wheel(int notches) {}

void phone_paint(HDC dc, RECT a) {
    SelectObject(dc, g_font);
    int y = a.top, gap = g_lineH + S(6);
    int tw = ui_text(dc, a.left, y, L"Phone", g_accent);
    ui_text(dc, a.left + tw + S(12), y, L"Pocket Loomy over Bluetooth", C_DIM);
    const wchar_t *st = g_cfg.phoneLocked ? L"shut down" : bt_listening() ? L"listening" : L"not listening";
    SIZE sz; GetTextExtentPoint32W(dc, st, s_len(st), &sz);
    ui_text(dc, a.right - sz.cx, y, st, g_cfg.phoneLocked ? C_ERR : bt_listening() ? C_OK : C_DIM);
    y += g_lineH + S(18);

    wchar_t line[260], t[48];
    if (g_cfg.phoneLocked) {
        ui_text(dc, a.left, y, L"Listening is off: the last connection failed a check.", C_ERR); y += gap;
        if (g_lockWhy[0]) {
            RECT r = { a.left, y, a.right, y + 3 * g_lineH };
            SetTextColor(dc, C_TEXT);
            DrawTextW(dc, g_lockWhy, -1, &r, DT_WORDBREAK | DT_NOPREFIX);
            y += 2 * g_lineH + S(6);
        }
        ui_text(dc, a.left, y, L"Nothing got in. Enter turns listening back on.", C_DIM); y += gap;
    } else if (g_trust.have) {
        wchar_t nm[64]; trust_name(nm, 64);
        int x = a.left + ui_text(dc, a.left, y, L"Trusted phone  ", C_DIM);
        x += ui_text(dc, x, y, nm, C_BRIGHT);
        line[0] = line[1] = L' ';
        for (int i = 0; i <= 17; i++) line[2 + i] = (wchar_t)(unsigned char)g_trust.addr[i];
        ui_text(dc, x, y, line, C_DIM);
        y += gap;
        os_fmt_time(g_trust.expires, t, 48, 1);
        wsprintfW(line, L"Trusted until %s, then it pairs again", t);
        ui_text(dc, a.left, y, line, C_DIM); y += gap + S(6);
        if (g_conn && g_wasReady) ui_text(dc, a.left, y, L"Connected", C_OK);
        else if (g_conn) ui_text(dc, a.left, y, L"A phone is connecting...", C_TEXT);
        else if (g_lastSeenEver) {
            os_fmt_time(g_lastSeenEver, t, 48, 0);
            wsprintfW(line, L"Awaiting trusted device ping \x00B7 last seen %s", t);
            ui_text(dc, a.left, y, line, C_DIM);
        } else ui_text(dc, a.left, y, L"Awaiting trusted device ping", C_DIM);
        y += gap;
        if (g_confirmForget) {
            wsprintfW(line, L"Press F again to forget \"%s\".", nm);
            ui_text(dc, a.left, y, line, C_ERR);
            y += gap;
        }
    } else if (g_codeLive) {
        ui_text(dc, a.left, y, L"Say this code on your phone", C_DIM); y += g_lineH + S(4);
        wchar_t code[12];
        for (int i = 0, k = 0; i < PK_CODELEN; i++) { if (i == 4) code[k++] = L' '; code[k++] = (wchar_t)g_code[i]; code[k] = 0; }
        if (g_big) SelectObject(dc, g_big);
        ui_text(dc, a.left, y, code, g_accent);
        SIZE cs; GetTextExtentPoint32W(dc, code, s_len(code), &cs);
        SelectObject(dc, g_font);
        y += (cs.cy > g_lineH ? cs.cy : g_lineH) + S(6);
        ULONGLONG left = g_codeEnds > now() ? g_codeEnds - now() : 0;
        wsprintfW(line, L"Expires in %d:%02d \x00B7 one try, then it's used up", (int)(left / 60), (int)(left % 60));
        ui_text(dc, a.left, y, line, C_DIM); y += gap;
        ui_text(dc, a.left, y, g_conn ? L"A phone is connecting..." : L"Waiting for phone...", C_TEXT); y += gap;
    } else {
        ui_text(dc, a.left, y, g_codeGone == 1 ? L"That pairing code is used up." :
                               g_codeGone == 2 ? L"That pairing code ran out." : L"No pairing code open.", C_TEXT);
        y += gap;
        ui_text(dc, a.left, y, L"Press N for a new one.", C_DIM); y += gap;
    }
    if (g_btErr[0]) { ui_text(dc, a.left, y, g_btErr, C_ERR); y += gap; }

    if (g_nlog) {
        y += S(8);
        ui_text(dc, a.left, y, L"Recent", C_DIM); y += g_lineH + S(4);
        SelectObject(dc, g_fontSmall);
        for (int i = g_nlog - 1; i >= 0 && y < a.bottom - S(40); i--) {
            ui_text(dc, a.left, y, g_log[i], C_TEXT);
            y += S(18);
        }
    }

    SelectObject(dc, g_fontSmall);
    const wchar_t *keys = g_cfg.phoneLocked ? L"Enter turn listening back on    Esc close" :
                          g_trust.have ? L"F forget this phone    Esc close" :
                          L"N new code    Esc close";
    ui_text(dc, a.left, a.bottom - S(16), keys, C_DIM);
    const wchar_t *hint = L"names are what the phone claims";
    GetTextExtentPoint32W(dc, hint, s_len(hint), &sz);
    ui_text(dc, a.right - sz.cx, a.bottom - S(16), hint, C_DIM);
    SelectObject(dc, g_font);
}
