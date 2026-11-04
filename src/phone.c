// phone.c - Pocket Loomy, PC side. Your Android phone hears "Loomy, display extend" and sends it
// here over Bluetooth; Loomy runs it like you typed it and answers. Opt-in: /pkg add phone.
//
//   /remote            the connection screen: a pairing code (no phone trusted yet) or the trusted
//                     phone and whether it's around. N = new code, F = forget phone, Enter = turn
//                     listening back on after a shutdown, Esc closes.
//   /remote forget     drop the trusted phone
//
// The rules (POCKET-HANDOVER.md has the why):
//   - Listening only happens with the feature on, not shut down, and either a trusted phone or an
//     open pairing code. No timers run while it just waits.
//   - A code is 8 random digits, lives 2 minutes, and is used up by the first try, right or wrong.
//     A new one only comes from you (opening /remote, or N).
//   - A paired phone is trusted for 7 days from pairing, then it has to pair again.
//   - Any failed check, bad frame or unknown phone shuts the listener down completely, prints who
//     tried (the name is only what the phone claimed) and pops a tray note. Only /remote reopens it.
//   - Only plain commands marked .phone in commands.c run, through the normal cmd_run.
// The protocol is pocket.c; the Windows Bluetooth and OS bits are phonebt.c.
#include "loomy.h"
#include "qr/qrcodegen.h"
#include "pocket.h"
#include "monocypher.h"

#define CODE_SECS      120
#define TRUST_SECS     (7ull * 24 * 3600)
#define HANDSHAKE_SECS 10          // a connection has this long to finish its handshake (or to hang up after pairing)
#define IDLE_SECS      90          // a connected phone that says nothing for this long gets hung up on
#define LOGN           5
#define CONFIRM_SECS   60          // after the right code: this long to press Enter on the PC

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
static char      g_pairRoom[33];      // 2.22: the relay room for pairing through Ellune (random per code, in the QR)
static int       g_codeLive, g_codeGone;          // gone: 1 = used, 2 = ran out
static ULONGLONG g_codeEnds;

static wchar_t g_lockWhy[220];
static wchar_t g_btErr[120];
static int     g_rngBad;

// every pipe a phone can come through (only Bluetooth today; a relay would be added here)
static const PhoneLink *LINKS[] = { &PHONE_BT, &PHONE_NET };   // 2.22: + remote through Ellune
#define NLINKS ((int)(sizeof LINKS / sizeof LINKS[0]))
static const PhoneLink *g_via;                // the link the connected phone came through

static int any_listening(void) {
    for (int i = 0; i < NLINKS; i++) if (LINKS[i]->listening()) return 1;
    return 0;
}

static PkConn    g_pk;
static int       g_conn, g_wasReady;
// 2.19: the right code isn't enough on its own. Loomy shows the phone's name and ID (the phone shows the
// same ID) and holds back its last pairing message until you press Enter. Esc or a minute = no.
static int       g_confirm;
static ULONGLONG g_confirmUntil;
static char      g_confirmId[10];
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
    g_via = 0;
    g_connAddr[0] = 0;
    timer_update();
    ui_redraw();
}

static void drop_conn(void) {
    if (!g_conn) return;
    if (g_via) g_via->drop();
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
    // 2.22 /remote: Bluetooth and remote are each allowed or not (his checkboxes). Pairing is Bluetooth only, in
    // person; remote (through Ellune) needs a trusted phone, since the relay room comes from the pairing.
    int base = feature_on(FEAT_PHONE) && !g_cfg.phoneLocked;
    for (int i = 0; i < NLINKS; i++) {
        const PhoneLink *l = LINKS[i];
        int net = l == &PHONE_NET;
        int want = base && (net ? g_cfg.remoteNet && (g_trust.have || g_codeLive) : g_cfg.remoteBt && (g_trust.have || g_codeLive));
        if (net && want && l->listening() && !(g_conn && g_via == l)) {   // the room changed (a code opened / closed,
            char room[33];                                                // a new pairing); never mid-session: the
            if (phone_net_room(room) && !net_in_room(room)) l->unlisten(); // pairing's last message must get out first
        }
        wchar_t err[120] = { 0 };
        if (want && !l->listening()) { if (!l->listen(err, 120) && !net) s_cpy(g_btErr, err, 120); else if (!net) g_btErr[0] = 0; }
        if (!want && l->listening()) l->unlisten();
        if (!want && !net) g_btErr[0] = 0;
    }
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
    out(OUT_DIM, L"Nothing got in. /remote turns listening back on.");
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
    unsigned char rr[16];
    if (!os_random(rr, sizeof rr)) { lockout(L"Windows wouldn't give Loomy secure random numbers."); return; }
    for (int i = 0; i < 16; i++) { g_pairRoom[2 * i] = "0123456789abcdef"[rr[i] >> 4]; g_pairRoom[2 * i + 1] = "0123456789abcdef"[rr[i] & 15]; }
    g_pairRoom[32] = 0;
    g_codeLive = 1;
    g_codeGone = 0;
    g_codeEnds = now() + CODE_SECS;
    update_listener();
}

// ---------------------------------------------------------------- the connection
static void flush(void) {
    if (g_rngBad) { lockout(L"Windows wouldn't give Loomy secure random numbers."); return; }
    if (!g_pk.outlen) return;
    int ok = g_via && g_via->send(g_pk.out, g_pk.outlen);
    g_pk.outlen = 0;
    if (!ok) { log_add(L"Couldn't send to %s; hung up.", g_connAddr); drop_conn(); }
}

const unsigned char *phone_pc_pub(void) { return g_trust.have ? g_trust.pc_pk : 0; }

// 2.22: which relay room PHONE_NET sits in: the pairing room while a code is open (QR pairing), else the trusted
// phone's room (from the pairing). 0 = none.
int phone_net_room(char out[33]) {
    if (g_codeLive && g_pairRoom[0]) { for (int i = 0; i < 33; i++) out[i] = g_pairRoom[i]; return 1; }
    if (g_trust.have) { el_room(g_trust.pc_pk, out); return 1; }
    return 0;
}

// the QR's text: pocketloomy://pair?c=<code>&r=<room>&n=<this PC's name, only safe characters>
static int qr_text(char *out, int cap) {
    char name[PK_NAMEMAX + 1];
    int nl = os_pc_name(name, PK_NAMEMAX);
    int o = 0;
    const char *head = "pocketloomy://pair?c=";
    for (const char *c = head; *c; c++) out[o++] = *c;
    for (int i = 0; i < PK_CODELEN; i++) out[o++] = g_code[i];
    const char *r = "&r=";
    for (const char *c = r; *c; c++) out[o++] = *c;
    for (int i = 0; i < 32; i++) out[o++] = g_pairRoom[i];
    out[o++] = '&'; out[o++] = 'n'; out[o++] = '=';
    for (int i = 0; i < nl && o < cap - 2; i++) {
        char ch = name[i];
        int ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
        out[o++] = ok ? ch : '-';
    }
    out[o] = 0;
    return o;
}

int phone_on_accept(const PhoneLink *via, const wchar_t *addr) {
    if (g_conn) { log_add(L"Turned away %s: already talking to a phone.", addr); return 0; }
    update_listener();
    if (!via->listening()) return 0;                     // nothing to listen for any more
    g_via = via;
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
    wchar_t buf[PK_TEXTMAX + 1];
    char u8[PK_TEXTMAX + 1];
    s_cpy(buf, w, PK_TEXTMAX + 1);
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

// every attempt from the phone shows in Loomy, run or not, so you can see what it heard.
// The answer for the phone builds up in g_rep (several commands can come in one go: "si and input next tab").
static wchar_t g_rep[600];
static int     g_lastPart;            // only the last key of a chain describes the screen (keeps the answer short)

static void rep(const wchar_t *t) {
    if (!t || !*t) return;
    if (g_rep[0]) s_cat(g_rep, L"\n", 600);
    s_cat(g_rep, t, 600);
}

static void heard(const wchar_t *t) { out(OUT_DIM, L"From your phone:"); out(OUT_ECHO, t); }

static void refuse(const wchar_t *t, const wchar_t *msg) {
    out(OUT_ERR, msg);
    log_add(L"Phone: \"%s\" (%s)", t, msg);
    rep(msg);
}

// "input back", "input next tab", "input down 3", "input n": keys into Loomy's open screen, nothing else
static const struct { const wchar_t *say; WPARAM vk; int shift; } KEYS[] = {
    { L"back", VK_ESCAPE, 0 }, { L"escape", VK_ESCAPE, 0 }, { L"esc", VK_ESCAPE, 0 }, { L"close", VK_ESCAPE, 0 },
    { L"exit", VK_ESCAPE, 0 }, { L"cancel", VK_ESCAPE, 0 },
    { L"next tab", VK_TAB, 0 }, { L"next", VK_TAB, 0 }, { L"tab", VK_TAB, 0 }, { L"next view", VK_TAB, 0 },
    { L"previous tab", VK_TAB, 1 }, { L"previous", VK_TAB, 1 }, { L"prev", VK_TAB, 1 }, { L"last tab", VK_TAB, 1 },
    { L"back tab", VK_TAB, 1 }, { L"shift tab", VK_TAB, 1 },
    { L"up", VK_UP, 0 }, { L"down", VK_DOWN, 0 }, { L"left", VK_LEFT, 0 }, { L"right", VK_RIGHT, 0 },
    { L"enter", VK_RETURN, 0 }, { L"select", VK_RETURN, 0 }, { L"ok", VK_RETURN, 0 }, { L"okay", VK_RETURN, 0 },
    { L"open", VK_RETURN, 0 }, { L"confirm", VK_RETURN, 0 }, { L"return", VK_RETURN, 0 }, { L"yes", VK_RETURN, 0 },
    { L"page up", VK_PRIOR, 0 }, { L"page down", VK_NEXT, 0 }, { L"home", VK_HOME, 0 }, { L"top", VK_HOME, 0 },
    { L"end", VK_END, 0 }, { L"bottom", VK_END, 0 }, { L"delete", VK_DELETE, 0 }, { L"space", 0, 0 },
};
static const wchar_t *NUMW[] = { L"one", L"two", L"three", L"four", L"five", L"six", L"seven", L"eight", L"nine" };

static void phone_input(const wchar_t *t, wchar_t *what) {
    wchar_t k[64];
    s_cpy(k, what, 64);
    s_lower(k);
    // a trailing count: "down 3", "down three", "down 3 times"
    int times = 1, n = s_len(k);
    if (n > 6 && s_ieq(k + n - 6, L" times")) { k[n - 6] = 0; n -= 6; }
    wchar_t *sp = 0;
    for (int i = n - 1; i > 0; i--) if (k[i] == L' ') { sp = k + i; break; }
    if (sp) {
        if (sp[1] >= L'1' && sp[1] <= L'9' && !sp[2]) { times = sp[1] - L'0'; *sp = 0; }
        else for (int i = 0; i < 9; i++) if (s_ieq(sp + 1, NUMW[i])) { times = i + 1; *sp = 0; break; }
    }
    heard(t);
    if (!ui_panel_open()) { refuse(t, L"Nothing is open on the PC to press keys in."); return; }
    int found = -1;
    for (int i = 0; i < (int)(sizeof KEYS / sizeof KEYS[0]); i++) if (s_ieq(k, KEYS[i].say)) { found = i; break; }
    int ch = 0;
    if (found < 0 && s_len(k) == 1 && ((k[0] >= L'a' && k[0] <= L'z') || (k[0] >= L'0' && k[0] <= L'9'))) ch = k[0];
    if (found >= 0 && !KEYS[found].vk) ch = L' ';
    if (found < 0 && !ch) {
        wchar_t msg[120];
        wsprintfW(msg, L"No key called '%s'. Try back, next tab, up, down, enter.", k);
        refuse(t, msg);
        return;
    }
    for (int i = 0; i < times && ui_panel_open(); i++) {
        if (ch) ui_panel_char((wchar_t)ch);
        else ui_panel_key(KEYS[found].vk, KEYS[found].shift);
    }
    log_add(L"Phone: input %s", k);
    if (!g_lastPart && ui_panel_open()) return;      // "up up right ok": say what's on screen once, at the end
    wchar_t c[400];
    ui_panel_choices(c, 400);
    rep(c[0] ? c : L"Closed.");
}

// one command from the phone
static void run_one(wchar_t *t) {
    wchar_t first[48];
    int k = 0;
    while (t[k] && t[k] != L' ' && k < 47) { first[k] = t[k]; k++; }
    first[k] = 0;
    wchar_t *rest = s_trim(t + k);
    int alone = !*rest;
    if ((s_ieq(first, L"input") || s_ieq(first, L"press")) && !alone) { phone_input(t, rest); return; }
    if (alone && (s_ieq(first, L"open") || s_ieq(first, L"show"))) {
        heard(t); log_add(L"Phone: open"); ui_show(); rep(L"Opening Loomy."); return;
    }
    if (alone && s_ieq(first, L"hide")) {
        heard(t); log_add(L"Phone: hide"); ui_hide(); rep(L"Hidden."); return;
    }
    wchar_t msg[160];
    int ok = cmd_phone_ok(first);
    if (ok != 1) {
        heard(t);
        if (first[0] == L'/' && ok) s_cpy(msg, L"Slash commands only work on the PC.", 160);
        else if (ok == 0) wsprintfW(msg, L"I don't know '%s'.", first);
        else wsprintfW(msg, L"'%s' only works on the PC.", first);
        refuse(t, msg);
        return;
    }
    if (ui_panel_open()) ui_close_panels();          // a new command starts from the main screen
    log_add(L"Phone: %s", t);
    out(OUT_DIM, L"From your phone:");
    g_cap[0] = 0;
    g_capturing = 1;
    cmd_run(t);
    g_capturing = 0;
    if (s_ieq(first, L"help")) {                     // the PC's table is too wide for a phone: a short list instead
        wchar_t h[400];
        cmd_phone_help(h, 400);
        rep(h);
    } else rep(g_cap[0] ? g_cap : L"Done.");
    if (ui_panel_open() && g_lastPart) {             // it opened a screen: say what's in it (once, at the end)
        wchar_t c[400];
        ui_panel_choices(c, 400);
        rep(c);
    }
    ui_show_if_panel();
}

// what came from the phone: one command, or several joined by "and" ("si and input next tab").
// "and" only splits where a command (or input/open/hide) follows, so "lp salt and pepper" stays whole.
static void run_from_phone(void) {
    wchar_t line[PK_TEXTMAX + 1];
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (const char *)g_pk.text, g_pk.textlen, line, PK_TEXTMAX);
    int bad = n <= 0;
    for (int i = 0; i < n && !bad; i++) if (line[i] < 0x20 || line[i] == 0x7F) bad = 1;
    g_rep[0] = 0;
    if (bad) {
        out(OUT_ERR, L"From your phone: something unreadable (bad characters). Ignored.");
        log_add(L"Phone sent something unreadable.");
        reply(n <= 0 ? L"I couldn't read that." : L"That had control characters in it.");
        return;
    }
    line[n] = 0;
    wchar_t *t = s_trim(line);
    if (!*t) { reply(L"Say a command."); return; }

    wchar_t *parts[16];               // "up up right confirm" from input mode arrives as 4 chained inputs
    int np = 0;
    parts[np++] = t;
    for (wchar_t *p = t; *p && np < 16; p++) {
        if (p[0] != L' ' || !((p[1] | 32) == L'a' && (p[2] | 32) == L'n' && (p[3] | 32) == L'd' && p[4] == L' ')) continue;
        wchar_t next[48];
        int k = 0;
        const wchar_t *q = p + 5;
        while (*q == L' ') q++;
        while (q[k] && q[k] != L' ' && k < 47) { next[k] = q[k]; k++; }
        next[k] = 0;
        int cmd = s_ieq(next, L"input") || s_ieq(next, L"press") || s_ieq(next, L"open") || s_ieq(next, L"show") ||
                  s_ieq(next, L"hide") || (next[0] != L'/' && cmd_phone_ok(next) == 1);
        if (!cmd) continue;
        *p = 0;
        parts[np++] = (wchar_t *)q;
        p = (wchar_t *)q - 1;
    }
    for (int i = 0; i < np; i++) {
        wchar_t *one = s_trim(parts[i]);
        g_lastPart = i == np - 1;
        if (*one) run_one(one);
    }
    reply(g_rep[0] ? g_rep : L"Done.");
}

// the phone heard something it couldn't turn into a command (or nothing at all)
static void phone_note(void) {
    wchar_t w[PK_TEXTMAX + 1];
    int n = g_pk.textlen ? MultiByteToWideChar(CP_UTF8, 0, (const char *)g_pk.text, g_pk.textlen, w, PK_TEXTMAX) : 0;
    if (n < 0) n = 0;
    w[n] = 0;
    for (int i = 0; i < n; i++) if (w[i] < 0x20 || w[i] == 0x7F) w[i] = L'?';
    if (n) {
        outf(OUT_ERR, L"From your phone: didn't catch a command (heard \"%s\").", w);
        log_add(L"Phone didn't catch it (heard \"%s\")", w);
    } else {
        out(OUT_ERR, L"From your phone: didn't catch that.");
        log_add(L"Phone didn't catch anything.");
    }
}

// the phone pressed Forget: drop it here too, right away
static void phone_forgot(void) {
    wchar_t nm[64];
    trust_name(nm, 64);
    forget_trust();
    log_add(L"\"%s\" unpaired itself.", nm);
    outf(OUT_OK, L"\"%s\" unpaired from the phone, so Loomy forgot it too.", nm);
    update_listener();
}

// ---------------------------------------------------------------- confirming a pairing on the PC
static void ask_confirm(void) {
    wchar_t nm[64], id[10];
    g_confirm = 1;
    g_confirmUntil = now() + CONFIRM_SECS;
    pk_fingerprint(g_pk.peer_pk, g_confirmId);
    for (int i = 0; i < 10; i++) id[i] = (wchar_t)g_confirmId[i];
    name_w(g_pk.peer_name, g_pk.peer_namelen, nm, 64);
    log_add(L"\"%s\" (%s) has the right code. Waiting for Enter.", nm, g_connAddr);
    outf(OUT_OK, L"\"%s\" (%s) has the right code. Its ID is %s: if your phone shows the same, press Enter in /remote.",
         nm, g_connAddr, id);
    if (!phone_active()) phone_open();                   // the question has to be on screen
    ui_show();
    timer_update();
    ui_redraw();
}

static void accept_pairing(void) {
    g_confirm = 0;
    paired();                                            // saves the trust
    flush();                                             // the PC's key goes out only now: the phone is paired
    g_connStart = now();                                 // it gets a moment to hang up by itself
    update_listener();                                   // a new pairing = a new relay room (moved once this session ends)
}

static void refuse_pairing(const wchar_t *why) {
    wchar_t nm[64];
    name_w(g_pk.peer_name, g_pk.peer_namelen, nm, 64);
    g_confirm = 0;
    log_add(L"Didn't pair \"%s\": %s.", nm, why);
    outf(OUT_ERR, L"Didn't pair \"%s\": %s. /remote, N for a new code.", nm, why);
    drop_conn();                                         // its keys are wiped with the connection
    update_listener();
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
        case PK_EV_PAIRED:                               // right code: now YOU confirm it's your phone
            ask_confirm();
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
        case PK_EV_NOTE:
            g_lastSeen = g_lastSeenEver = now();
            phone_note();
            break;
        case PK_EV_FORGET:
            g_lastSeenEver = 0;
            drop_conn();
            phone_forgot();
            return;
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
    if (g_confirm) { refuse_pairing(L"the phone hung up before you confirmed it"); return; }
    if (pk_mid_pairing(&g_pk)) {
        log_add(L"Pairing from %s stopped halfway. That code is used up.", g_connAddr);
        out(OUT_ERR, L"A phone used the pairing code but hung up before finishing. /remote, N for a new code.");
    } else if (!g_wasReady && !pk_done(&g_pk)) {
        log_add(L"%s hung up during the handshake.", g_connAddr);
    }
    end_conn();
    update_listener();
}

void phone_tick(void) {
    ULONGLONG t = now();
    if (g_codeLive && t >= g_codeEnds) { burn_code(2); log_add(L"The pairing code ran out."); update_listener(); }
    if (g_confirm && t >= g_confirmUntil) refuse_pairing(L"nobody confirmed it within a minute");
    else if (g_conn && !g_confirm) {
        int ready = pk_ready(&g_pk);
        if (!ready && t - g_connStart >= HANDSHAKE_SECS) {
            if (pk_mid_pairing(&g_pk)) out(OUT_ERR, L"A phone used the pairing code but stalled. /remote, N for a new code.");
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

// ---------------------------------------------------------------- /remote
static void do_forget(void) {
    if (!g_trust.have) { out(OUT_DIM, L"No phone is trusted."); return; }
    wchar_t nm[64]; trust_name(nm, 64);
    drop_conn();
    forget_trust();
    log_add(L"Forgot \"%s\".", nm);
    outf(OUT_OK, L"Forgot \"%s\". It can't connect any more; /remote to pair again.", nm);
    update_listener();
}

void cmd_phone(wchar_t *args) {
    wchar_t *w[2];
    int n = s_words(args, w, 2);
    if (n == 0) { phone_open(); return; }
    if (n == 1 && s_ieq(w[0], L"forget")) { do_forget(); return; }
    if (n == 2 && s_ieq(w[0], L"add")) {                 // 2.22: this PC onto Ellune's guest list with an Authy code
        wchar_t why[160];
        out(OUT_DIM, L"Asking Ellune...");
        ui_flush();
        if (net_register(w[1], why, 160)) { out(OUT_OK, L"This PC is on Ellune's guest list now."); log_add(L"Added this PC to Ellune's guest list."); }
        else out(OUT_ERR, why);
        return;
    }
    cmd_usage(L"[forget | add <Authy code>]");
}

void phone_complete(const wchar_t *partial) { complete_add(L"forget", partial); complete_add(L"add", partial); }

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
    if (!g_cfg.phoneLocked && !g_trust.have && !g_codeLive && !g_confirm) new_code();   // opening it is the deliberate action
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
    if (g_confirm) {                                     // the pairing question has the keys
        if (vk == VK_RETURN) accept_pairing();
        else if (vk == VK_ESCAPE) refuse_pairing(L"you said no");
        return 1;
    }
    if (vk == VK_ESCAPE) { phone_close(); return 1; }
    if (vk == VK_RETURN && g_cfg.phoneLocked) { unlock(); return 1; }
    return 1;
}

int phone_char(WPARAM ch) {
    wchar_t c = s_low((wchar_t)ch);
    if (!g_confirm && (c == L'b' || c == L'r')) {         // 2.22: allow Bluetooth / remote
        if (c == L'b') g_cfg.remoteBt = !g_cfg.remoteBt; else g_cfg.remoteNet = !g_cfg.remoteNet;
        cfg_save();
        log_add(L"%s %s.", c == L'b' ? L"Bluetooth" : L"Remote", (c == L'b' ? g_cfg.remoteBt : g_cfg.remoteNet) ? L"allowed" : L"not allowed");
        g_confirmForget = 0;
        update_listener();
        return 1;
    }
    if (!g_confirm && c == L'c') {                       // this PC's line for Ellune's guest list
        log_add(net_copy_line() ? L"Copied this PC's guest line: add it to Ellune's DEVICES (after a comma)." : L"Couldn't copy the guest line.");
        ui_redraw();
        return 1;
    }
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

// 2.22: redrawn simpler (his ask: "a lot of confusing text"). Top: what's allowed + Ellune in one short line each.
// Middle: one thing at a time (confirm / shut down / the paired phone / the QR + code / nothing yet). Bottom: keys.
static void draw_qr(HDC dc, int x, int y, int maxSide, int *sideOut) {
    static uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(10)], tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
    char txt[160];
    *sideOut = 0;
    qr_text(txt, sizeof txt);
    if (!qrcodegen_encodeText(txt, tmp, qr, qrcodegen_Ecc_MEDIUM, 1, 10, qrcodegen_Mask_AUTO, true)) return;
    int n = qrcodegen_getSize(qr), m = maxSide / (n + 6);
    if (m > S(5)) m = S(5);
    if (m < 2) m = 2;
    int quiet = 3 * m, side = n * m + 2 * quiet;
    RECT bg = { x, y, x + side, y + side };
    HBRUSH white = CreateSolidBrush(RGB(255, 255, 255)), black = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(dc, &bg, white);
    for (int qy = 0; qy < n; qy++)
        for (int qx = 0; qx < n; qx++)
            if (qrcodegen_getModule(qr, qx, qy)) {
                RECT r = { x + quiet + qx * m, y + quiet + qy * m, x + quiet + (qx + 1) * m, y + quiet + (qy + 1) * m };
                FillRect(dc, &r, black);
            }
    DeleteObject(white); DeleteObject(black);
    *sideOut = side;
}

static int big_text(HDC dc, int x, int y, const wchar_t *t, COLORREF c) {
    if (g_big) SelectObject(dc, g_big);
    ui_text(dc, x, y, t, c);
    SIZE cs; GetTextExtentPoint32W(dc, t, s_len(t), &cs);
    SelectObject(dc, g_font);
    return cs.cy > g_lineH ? cs.cy : g_lineH;
}

void phone_paint(HDC dc, RECT a) {
    SelectObject(dc, g_font);
    int y = a.top, gap = g_lineH + S(6);
    wchar_t line[260], t[48];
    SIZE sz;

    // title + whether anything listens
    ui_text(dc, a.left, y, L"Remote", g_accent);
    const wchar_t *st = g_cfg.phoneLocked ? L"shut down" : any_listening() ? L"listening" : L"off";
    GetTextExtentPoint32W(dc, st, s_len(st), &sz);
    ui_text(dc, a.right - sz.cx, y, st, g_cfg.phoneLocked ? C_ERR : any_listening() ? C_OK : C_DIM);
    y += g_lineH + S(12);

    // what's allowed, and this PC's ID on the right
    int x = a.left + ui_text(dc, a.left, y, g_cfg.remoteBt ? L"[x] " : L"[ ] ", g_cfg.remoteBt ? g_accent : C_DIM);
    x += ui_text(dc, x, y, L"Bluetooth", C_TEXT);
    x += S(28);
    x += ui_text(dc, x, y, g_cfg.remoteNet ? L"[x] " : L"[ ] ", g_cfg.remoteNet ? g_accent : C_DIM);
    ui_text(dc, x, y, L"Remote (Ellune)", C_TEXT);
    char id[10];
    if (net_id(id)) {
        wchar_t wid[16] = L"PC ";
        for (int i = 0; i < 10; i++) wid[3 + i] = (wchar_t)id[i];
        GetTextExtentPoint32W(dc, wid, s_len(wid), &sz);
        ui_text(dc, a.right - sz.cx, y, wid, C_DIM);
    }
    y += g_lineH + S(4);
    int bad = 0;
    const wchar_t *ns = net_status(&bad);
    ui_text(dc, a.left, y, ns, bad ? C_ERR : C_DIM);
    y += gap + S(8);

    if (g_confirm) {                                     // the right code came in: is it really your phone?
        wchar_t nm[64], cid[10];
        name_w(g_pk.peer_name, g_pk.peer_namelen, nm, 64);
        for (int i = 0; i < 10; i++) cid[i] = (wchar_t)g_confirmId[i];
        x = a.left + ui_text(dc, a.left, y, L"Pair with ", C_TEXT);
        x += ui_text(dc, x, y, nm, C_BRIGHT);
        ui_text(dc, x, y, L"?", C_TEXT);
        y += gap;
        y += big_text(dc, a.left, y, cid, g_accent) + S(6);
        ULONGLONG left = g_confirmUntil > now() ? g_confirmUntil - now() : 0;
        wsprintfW(line, L"Same ID on your phone? Enter = yes, Esc = no (%d s)", (int)left);
        ui_text(dc, a.left, y, line, C_TEXT); y += gap;
        ui_text(dc, a.left, y, L"(the name is only what the phone says it is: the ID is what counts)", C_DIM); y += gap;
    } else if (g_cfg.phoneLocked) {
        ui_text(dc, a.left, y, L"Shut down after a bad connection. Nothing got in.", C_ERR); y += gap;
        if (g_lockWhy[0]) {
            RECT r = { a.left, y, a.right, y + 3 * g_lineH };
            SetTextColor(dc, C_DIM);
            DrawTextW(dc, g_lockWhy, -1, &r, DT_WORDBREAK | DT_NOPREFIX);
            y += 2 * g_lineH + S(6);
        }
    } else if (g_trust.have) {
        wchar_t nm[64]; trust_name(nm, 64);
        x = a.left + ui_text(dc, a.left, y, L"Phone  ", C_DIM);
        ui_text(dc, x, y, nm, C_BRIGHT);
        y += gap;
        os_fmt_time(g_trust.expires, t, 48, 1);
        if (g_conn && g_wasReady) wsprintfW(line, L"connected \x00B7 paired until %s", t);
        else if (g_conn) wsprintfW(line, L"connecting... \x00B7 paired until %s", t);
        else if (g_lastSeenEver) {
            wchar_t seen[48];
            os_fmt_time(g_lastSeenEver, seen, 48, 0);
            wsprintfW(line, L"last seen %s \x00B7 paired until %s", seen, t);
        } else wsprintfW(line, L"paired until %s", t);
        ui_text(dc, a.left, y, line, g_conn && g_wasReady ? C_OK : C_DIM); y += gap;
        if (g_confirmForget) {
            wsprintfW(line, L"Press F again to forget \"%s\".", nm);
            ui_text(dc, a.left, y, line, C_ERR); y += gap;
        }
    } else if (g_codeLive) {
        // the QR on the left (Ellune), the code and the time on the right
        int side = 0, colX = a.left;
        if (g_cfg.remoteNet) {
            int room = a.bottom - S(40) - y;
            draw_qr(dc, a.left, y, room < S(240) ? room : S(240), &side);
            if (side) colX = a.left + side + S(20);
        }
        int cy = y;
        ui_text(dc, colX, cy, side ? L"Scan with your phone's camera" : L"Say this code on your phone", C_TEXT); cy += gap;
        wchar_t code[12];
        for (int i = 0, k = 0; i < PK_CODELEN; i++) { if (i == 4) code[k++] = L' '; code[k++] = (wchar_t)g_code[i]; code[k] = 0; }
        cy += big_text(dc, colX, cy, code, g_accent) + S(6);
        ULONGLONG left = g_codeEnds > now() ? g_codeEnds - now() : 0;
        wsprintfW(line, L"%d:%02d left \x00B7 works once", (int)(left / 60), (int)(left % 60));
        ui_text(dc, colX, cy, line, C_DIM); cy += gap;
        ui_text(dc, colX, cy, g_conn ? L"A phone is connecting..." : L"Waiting for your phone...", g_conn ? C_OK : C_DIM); cy += gap;
        if (side && g_cfg.remoteBt) { ui_text(dc, colX, cy, L"(or say the code over Bluetooth)", C_DIM); cy += gap; }
        y = (side && y + side > cy ? y + side : cy) + S(6);
    } else {
        const wchar_t *msg = g_codeGone == 1 ? L"That code is used up. N for a new one." :
                             g_codeGone == 2 ? L"That code ran out. N for a new one." :
                             !g_cfg.remoteBt && !g_cfg.remoteNet ? L"Allow Bluetooth (B) or Remote (R) to pair a phone." :
                             L"No phone paired. N to pair one.";
        ui_text(dc, a.left, y, msg, C_TEXT); y += gap;
    }
    if (g_btErr[0]) { ui_text(dc, a.left, y, g_btErr, C_ERR); y += gap; }

    // the last few things that happened, if there's room
    if (g_nlog && y < a.bottom - S(80)) {
        y += S(8);
        SelectObject(dc, g_fontSmall);
        for (int i = g_nlog - 1; i >= 0 && y < a.bottom - S(40); i--) {
            ui_text(dc, a.left, y, g_log[i], C_DIM);
            y += S(18);
        }
    }

    SelectObject(dc, g_fontSmall);
    const wchar_t *keys = g_confirm ? L"Enter yes    Esc no" :
                          g_cfg.phoneLocked ? L"Enter turn it back on    Esc close" :
                          g_trust.have ? L"B Bluetooth    R Remote    F forget    Esc close" :
                          L"N pair a phone    B Bluetooth    R Remote    Esc close";
    ui_text(dc, a.left, a.bottom - S(16), keys, C_DIM);
    SelectObject(dc, g_font);
}

#ifdef LOOMY_TESTS
int phone_code_for_tests(char *out) {       // the Linux test harness reads the open code
    if (!g_codeLive) return 0;
    memcpy(out, g_code, PK_CODELEN); out[PK_CODELEN] = 0;
    return 1;
}
#endif
