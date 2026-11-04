// phonenet.c - remote through Ellune (2.22), the Windows half (ellune.c has the portable half):
//   - PHONE_NET, the second PhoneLink (see loomy.h): the PC connects OUT to Ellune's relay room
//     (wss://ellune.loomybot.workers.dev/pc?room=R, a WebSocket) and waits there for its phone. No open ports.
//     The room passes Pocket Loomy's own end-to-end encrypted frames untouched, so to phone.c this is just
//     another wire: "phone" -> phone_on_accept, bytes -> phone_on_data, "gone" -> phone_on_closed.
//   - this PC's own Ellune key (ECDSA P-256 in Windows' CNG), so Ellune's guest list lets it in. The private
//     half is kept DPAPI-protected in %APPDATA%\Loomy\ellune.dat. /remote shows its ID; C copies the line
//     to paste into Ellune's DEVICES list (once).
// One worker thread holds the connection (WinHTTP blocks); it only posts WM_PHONENET to the UI thread, where
// everything else happens. It reconnects by itself: 5 s, 15 s, 1 min, then every 2 min; 5 min if Ellune says
// this PC isn't on her guest list. WinHTTP pings every 60 s so Cloudflare keeps the line open.
#include "loomy.h"
#include <winhttp.h>
#include "monocypher.h"

#ifndef LOAD_LIBRARY_SEARCH_SYSTEM32
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800
#endif
#ifndef WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET
#define WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET 114
#endif
#ifndef WINHTTP_OPTION_WEB_SOCKET_KEEPALIVE_INTERVAL
#define WINHTTP_OPTION_WEB_SOCKET_KEEPALIVE_INTERVAL 116
#endif

#define ELLUNE_HOST L"ellune.loomybot.workers.dev"

// WinHTTP's WebSocket calls (Windows 8+), loaded by name so older headers don't matter
typedef HINTERNET (WINAPI *PFN_WsUpgrade)(HINTERNET, DWORD_PTR);
typedef DWORD (WINAPI *PFN_WsSend)(HINTERNET, int, PVOID, DWORD);
typedef DWORD (WINAPI *PFN_WsRecv)(HINTERNET, PVOID, DWORD, DWORD *, int *);
typedef DWORD (WINAPI *PFN_WsClose)(HINTERNET, USHORT, PVOID, DWORD);
static PFN_WsUpgrade wsUpgrade;
static PFN_WsSend    wsSend;
static PFN_WsRecv    wsRecv;
static PFN_WsClose   wsClose;
enum { WS_BIN = 0, WS_BIN_FRAG = 1, WS_UTF8 = 2, WS_UTF8_FRAG = 3, WS_CLOSE = 4 };   // WINHTTP_WEB_SOCKET_BUFFER_TYPE

// CNG (bcrypt.dll), loaded by name like phonebt.c does
typedef LONG (WINAPI *PFN_Open)(void **, LPCWSTR, LPCWSTR, ULONG);
typedef LONG (WINAPI *PFN_Gen)(void *, void **, ULONG, ULONG);
typedef LONG (WINAPI *PFN_Fin)(void *, ULONG);
typedef LONG (WINAPI *PFN_Export)(void *, void *, LPCWSTR, PUCHAR, ULONG, ULONG *, ULONG);
typedef LONG (WINAPI *PFN_Import)(void *, void *, LPCWSTR, void **, PUCHAR, ULONG, ULONG);
typedef LONG (WINAPI *PFN_Sign)(void *, void *, PUCHAR, ULONG, PUCHAR, ULONG, ULONG *, ULONG);
static PFN_Open bOpen; static PFN_Gen bGen; static PFN_Fin bFin; static PFN_Export bExport;
static PFN_Import bImport; static PFN_Sign bSign;

static void *g_alg, *g_key;                 // the algorithm and this PC's key (UI thread loads it)
static unsigned char g_pub[65];             // 04 || X || Y
static int  g_keyTried, g_haveKey;
static int  net_listen(wchar_t *err, int cap);
static void net_unlisten(void);

// the connection (shared with the worker thread under g_cs)
static CRITICAL_SECTION g_cs;
static int       g_csInit;
static HANDLE    g_thread, g_stop;
static HINTERNET g_ws;
static volatile LONG g_state;               // NS_*
static volatile LONG g_want;
static volatile LONG g_gen;                 // bumped by each listen: an old worker still finishing up just stops
static char      g_room[33];
static int       g_session;                 // a phone session is running through here (UI thread)
enum { NS_OFF, NS_CONNECTING, NS_CONNECTED, NS_NOT_GUEST, NS_UNREACHABLE, NS_NO_KEY };
enum { NE_PHONE = 1, NE_DATA, NE_GONE, NE_LOST, NE_STATE };

static int s_len_a(const char *s) { int n = 0; while (s[n]) n++; return n; }
static int same(const unsigned char *a, const char *b, int n) { for (int i = 0; i < n; i++) if (a[i] != (unsigned char)b[i]) return 0; return 1; }

// ---------------------------------------------------------------- the key
static const wchar_t *key_path(void) {
    static wchar_t p[MAX_PATH];
    s_cpy(p, cfg_dir(), MAX_PATH);
    s_cat(p, L"\\ellune.dat", MAX_PATH);
    return p;
}

static int cng_load(void) {
    static int tried, ok;
    if (tried) return ok;
    tried = 1;
    HMODULE b = LoadLibraryExW(L"bcrypt.dll", 0, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!b) return 0;
    bOpen = (PFN_Open)GetProcAddress(b, "BCryptOpenAlgorithmProvider");
    bGen = (PFN_Gen)GetProcAddress(b, "BCryptGenerateKeyPair");
    bFin = (PFN_Fin)GetProcAddress(b, "BCryptFinalizeKeyPair");
    bExport = (PFN_Export)GetProcAddress(b, "BCryptExportKey");
    bImport = (PFN_Import)GetProcAddress(b, "BCryptImportKeyPair");
    bSign = (PFN_Sign)GetProcAddress(b, "BCryptSignHash");
    ok = bOpen && bGen && bFin && bExport && bImport && bSign && bOpen(&g_alg, L"ECDSA_P256", 0, 0) == 0;
    return ok;
}

// the public half -> g_pub (CNG's public blob: 8-byte header, then X and Y)
static int pub_from_key(void) {
    unsigned char blob[8 + 64];
    ULONG got = 0;
    if (bExport(g_key, 0, L"ECCPUBLICBLOB", blob, sizeof blob, &got, 0) || got != sizeof blob) return 0;
    g_pub[0] = 4;
    memcpy(g_pub + 1, blob + 8, 64);
    return 1;
}

// load this PC's key, or make one the first time (saved DPAPI-protected)
static int key_ready(void) {
    if (g_haveKey) return 1;
    if (g_keyTried) return 0;
    g_keyTried = 1;
    if (!cng_load()) return 0;
    unsigned char enc[1024], plain[8 + 96];
    HANDLE f = CreateFileW(key_path(), GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD n = 0;
        BOOL r = ReadFile(f, enc, sizeof enc, &n, 0);
        CloseHandle(f);
        int k = r && n > 0 ? os_protect(enc, (int)n, plain, sizeof plain, 1) : 0;
        if (k == (int)sizeof plain && bImport(g_alg, 0, L"ECCPRIVATEBLOB", &g_key, plain, k, 0) == 0 && pub_from_key()) {
            crypto_wipe(plain, sizeof plain);
            return g_haveKey = 1;
        }
        crypto_wipe(plain, sizeof plain);
    }
    // a new key for this PC (it then needs adding to Ellune's guest list once)
    ULONG got = 0;
    if (bGen(g_alg, &g_key, 256, 0) || bFin(g_key, 0) || !pub_from_key()) return 0;
    if (bExport(g_key, 0, L"ECCPRIVATEBLOB", plain, sizeof plain, &got, 0) || got != sizeof plain) return 0;
    int k = os_protect(plain, sizeof plain, enc, sizeof enc, 0);
    crypto_wipe(plain, sizeof plain);
    if (k <= 0) return 0;
    f = CreateFileW(key_path(), GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD w = 0;
    WriteFile(f, enc, (DWORD)k, &w, 0);
    CloseHandle(f);
    return g_haveKey = (w == (DWORD)k);
}

int net_id(char out[10]) {
    if (!key_ready()) return 0;
    el_device_id(g_pub, out);
    return 1;
}

int net_copy_line(void) {
    char line[128];
    if (!key_ready() || !el_guest_line(g_pub, line, sizeof line)) return 0;
    if (!OpenClipboard(ui_hwnd())) return 0;
    EmptyClipboard();
    int n = s_len_a(line);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)(n + 1) * sizeof(wchar_t));
    int ok = 0;
    if (h) {
        wchar_t *w = (wchar_t *)GlobalLock(h);
        for (int i = 0; i <= n; i++) w[i] = (wchar_t)(unsigned char)line[i];
        GlobalUnlock(h);
        ok = SetClipboardData(CF_UNICODETEXT, h) != 0;
        if (!ok) GlobalFree(h);
    }
    CloseClipboard();
    return ok;
}

// ---------------------------------------------------------------- talking to the UI thread
static void post(int kind, const unsigned char *p, int n) {
    unsigned char *copy = 0;
    if (p && n > 0) {
        copy = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)n + 4);
        if (!copy) return;
        copy[0] = (unsigned char)(n >> 8); copy[1] = (unsigned char)n;      // length first
        memcpy(copy + 2, p, n);
    }
    if (!PostMessageW(ui_hwnd(), WM_PHONENET, (WPARAM)kind, (LPARAM)copy) && copy) HeapFree(GetProcessHeap(), 0, copy);
}

static void set_state(LONG s) { InterlockedExchange(&g_state, s); post(NE_STATE, 0, 0); }

// ---------------------------------------------------------------- the worker thread
static unsigned long long unix_ms(void) {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    unsigned long long t = (unsigned long long)ft.dwHighDateTime << 32 | ft.dwLowDateTime;
    return (t - 116444736000000000ull) / 10000;
}

// the three guest-list headers for a request (see ellune/src/index.js)
static int signed_headers_for(const char *method, const char *path, const unsigned char *body, int bn, wchar_t *out, int cap) {
    char text[300], sig64[100], key64[100], tbuf[24];
    unsigned char h[32], sig[64];
    unsigned long long ms = unix_ms();
    if (!el_signed_text_body(method, path, ms, body, bn, text, sizeof text)) return 0;
    el_sha256((const unsigned char *)text, s_len_a(text), h);
    ULONG got = 0;
    if (bSign(g_key, 0, h, 32, sig, sizeof sig, &got, 0) || got != 64) return 0;   // CNG gives raw r || s
    el_b64url(sig, 64, sig64, sizeof sig64);
    el_b64url(g_pub, 65, key64, sizeof key64);
    int tn = 0; unsigned long long v = ms; char rev[24];
    do { rev[tn++] = (char)('0' + v % 10); v /= 10; } while (v);
    for (int i = 0; i < tn; i++) tbuf[i] = rev[tn - 1 - i];
    tbuf[tn] = 0;
    char a[400];
    wsprintfA(a, "x-ellune-key: %s\r\nx-ellune-time: %s\r\nx-ellune-sig: %s\r\n", key64, tbuf, sig64);
    return MultiByteToWideChar(CP_UTF8, 0, a, -1, out, cap) > 0;
}

static int signed_headers(const char *path, wchar_t *out, int cap) { return signed_headers_for("GET", path, (const unsigned char *)"", 0, out, cap); }

// /remote add <code>: this PC puts itself on Ellune's guest list with the current Authy code (0.5). Blocking,
// a few seconds at most (like /ellune). The request is signed by this PC's own key, so only it can add itself.
int net_register(const wchar_t *code, wchar_t *why, int cap) {
    why[0] = 0;
    int n = 0;
    while (code[n]) { if (code[n] < L'0' || code[n] > L'9' || n > 6) { s_cpy(why, L"The Authy code is 6 digits.", cap); return 0; } n++; }
    if (n != 6) { s_cpy(why, L"The Authy code is 6 digits.", cap); return 0; }
    char id[10];
    if (!key_ready() || !net_id(id)) { s_cpy(why, L"Couldn't make this PC's Ellune key.", cap); return 0; }
    char body[96], c8[8];
    for (int i = 0; i < 6; i++) c8[i] = (char)code[i];
    c8[6] = 0;
    wsprintfA(body, "{\"code\":\"%s\",\"name\":\"pc-%s\"}", c8, id);
    int bn = s_len_a(body);
    wchar_t hdr[700];
    if (!signed_headers_for("POST", "/register", (const unsigned char *)body, bn, hdr, 600)) { s_cpy(why, L"Couldn't sign the request.", cap); return 0; }
    s_cat(hdr, L"Content-Type: application/json\r\n", 700);
    HINTERNET s = WinHttpOpen(L"Loomy", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, 0, 0, 0);
    if (!s) s = WinHttpOpen(L"Loomy", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, 0, 0, 0);
    HINTERNET c = s ? WinHttpConnect(s, ELLUNE_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0) : 0;
    HINTERNET r = c ? WinHttpOpenRequest(c, L"POST", L"/register", 0, 0, 0, WINHTTP_FLAG_SECURE) : 0;
    DWORD status = 0;
    char ans[300];
    DWORD got = 0;
    ans[0] = 0;
    if (r) WinHttpSetTimeouts(r, 8000, 8000, 8000, 8000);
    if (r && WinHttpSendRequest(r, hdr, (DWORD)-1, body, (DWORD)bn, (DWORD)bn, 0) && WinHttpReceiveResponse(r, 0)) {
        DWORD sz = sizeof status;
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, 0, &status, &sz, 0);
        WinHttpReadData(r, ans, sizeof ans - 1, &got);
        ans[got] = 0;
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    if (s) WinHttpCloseHandle(s);
    if (status == 200) {                               // in: connect now instead of waiting out the 5 minutes
        if (g_want) { net_unlisten(); wchar_t e[120]; net_listen(e, 120); }
        return 1;
    }
    switch (status) {
    case 403: s_cpy(why, L"Wrong Authy code (or one already used). Wait for the next one.", cap); break;
    case 429: s_cpy(why, L"Locked for an hour after 5 wrong codes.", cap); break;
    case 503: s_cpy(why, L"Ellune's registration isn't set up yet (TOTP_SECRET, see ellune/README.md).", cap); break;
    case 0:   s_cpy(why, L"Couldn't reach Ellune.", cap); break;
    case 404: s_cpy(why, L"Ellune doesn't have /register: she needs the 0.5 update (npx wrangler deploy).", cap); break;
    default: {
        wchar_t w[160];
        MultiByteToWideChar(CP_UTF8, 0, ans, -1, w, 160);
        wsprintfW(why, L"Ellune said %u: %s", (unsigned)status, w);
        break;
    }
    }
    return 0;
}

// one connection: 1 = it was up (reconnect soon), 0 = it never got up; *status gets the HTTP status
static int connect_once(DWORD *status) {
    *status = 0;
    char path[64];
    wsprintfA(path, "/pc?room=%s", g_room);
    wchar_t wpath[64], hdr[600];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 64);
    if (!signed_headers(path, hdr, 600)) return 0;

    HINTERNET s = WinHttpOpen(L"Loomy", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, 0, 0, 0);
    if (!s) s = WinHttpOpen(L"Loomy", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, 0, 0, 0);
    if (!s) return 0;
    DWORD keep = 60000;
    WinHttpSetOption(s, WINHTTP_OPTION_WEB_SOCKET_KEEPALIVE_INTERVAL, &keep, sizeof keep);
    WinHttpSetTimeouts(s, 10000, 10000, 10000, 0);       // receiving waits forever (pings keep it honest)
    HINTERNET c = WinHttpConnect(s, ELLUNE_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", wpath, 0, 0, 0, WINHTTP_FLAG_SECURE) : 0;
    int up = 0;
    if (r && WinHttpSetOption(r, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, 0, 0) &&
        WinHttpSendRequest(r, hdr, (DWORD)-1, 0, 0, 0, 0) && WinHttpReceiveResponse(r, 0)) {
        DWORD sz = sizeof *status;
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, 0, status, &sz, 0);
        if (*status == 101) {
            HINTERNET ws = wsUpgrade(r, 0);
            if (ws) {
                EnterCriticalSection(&g_cs);
                g_ws = ws;
                LeaveCriticalSection(&g_cs);
                up = 1;
                set_state(NS_CONNECTED);
                unsigned char buf[2048];
                for (;;) {
                    DWORD n = 0; int type = 0;
                    if (wsRecv(ws, buf, sizeof buf, &n, &type)) break;     // closed by us, by Ellune, or lost
                    if (type == WS_CLOSE) break;
                    if (type == WS_UTF8 || type == WS_UTF8_FRAG) {
                        if (n == 5 && same(buf, "phone", 5)) post(NE_PHONE, 0, 0);
                        else if (n == 4 && same(buf, "gone", 4)) post(NE_GONE, 0, 0);
                        continue;                                           // anything else: ignored
                    }
                    if (n > 0 && n <= 1024) post(NE_DATA, buf, (int)n);
                }
                EnterCriticalSection(&g_cs);
                if (g_ws) { WinHttpCloseHandle(g_ws); g_ws = 0; }
                LeaveCriticalSection(&g_cs);
                post(NE_LOST, 0, 0);
            }
        }
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return up;
}

static DWORD WINAPI worker(void *arg) {
    LONG gen = (LONG)(LONG_PTR)arg;
    int tries = 0;
    static const DWORD WAIT[] = { 5000, 15000, 60000, 120000 };
    while (g_want && gen == g_gen) {
        set_state(NS_CONNECTING);
        DWORD status = 0;
        if (connect_once(&status)) tries = 0;
        if (!g_want || gen != g_gen) break;
        DWORD wait;
        if (status == 401) { set_state(NS_NOT_GUEST); wait = 300000; }
        else { set_state(NS_UNREACHABLE); wait = WAIT[tries < 3 ? tries : 3]; tries++; }
        if (WaitForSingleObject(g_stop, wait) == WAIT_OBJECT_0) break;
    }
    return 0;
}

// ---------------------------------------------------------------- the link
static int ws_load(void) {
    static int tried, ok;
    if (tried) return ok;
    tried = 1;
    HMODULE w = GetModuleHandleW(L"winhttp.dll");
    if (!w) w = LoadLibraryExW(L"winhttp.dll", 0, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!w) return 0;
    wsUpgrade = (PFN_WsUpgrade)GetProcAddress(w, "WinHttpWebSocketCompleteUpgrade");
    wsSend = (PFN_WsSend)GetProcAddress(w, "WinHttpWebSocketSend");
    wsRecv = (PFN_WsRecv)GetProcAddress(w, "WinHttpWebSocketReceive");
    wsClose = (PFN_WsClose)GetProcAddress(w, "WinHttpWebSocketClose");
    return ok = wsUpgrade && wsSend && wsRecv && wsClose;
}

static int net_listening(void) { return g_want != 0; }
int net_in_room(const char room[33]) { for (int i = 0; i < 33; i++) if (g_room[i] != room[i]) return 0; return 1; }

static int net_listen(wchar_t *err, int cap) {
    if (g_want) return 1;
    if (!g_csInit) { InitializeCriticalSection(&g_cs); g_csInit = 1; }
    char room[33];
    if (!phone_net_room(room)) { s_cpy(err, L"Remote needs a paired phone or an open pairing code.", cap); return 0; }
    if (!ws_load()) { set_state(NS_UNREACHABLE); s_cpy(err, L"This Windows can't do WebSockets (needs Windows 8 or newer).", cap); return 0; }
    if (!key_ready()) { InterlockedExchange(&g_state, NS_NO_KEY); s_cpy(err, L"Couldn't make this PC's Ellune key.", cap); return 0; }
    for (int i = 0; i < 33; i++) g_room[i] = room[i];
    if (!g_stop) g_stop = CreateEventW(0, TRUE, FALSE, 0);
    ResetEvent(g_stop);
    InterlockedExchange(&g_want, 1);
    LONG gen = InterlockedIncrement(&g_gen);
    g_thread = CreateThread(0, 0, worker, (void *)(LONG_PTR)gen, 0, 0);
    if (!g_thread) { InterlockedExchange(&g_want, 0); s_cpy(err, L"Couldn't start the remote connection.", cap); return 0; }
    return 1;
}

static void net_unlisten(void) {
    if (!g_want) return;
    InterlockedExchange(&g_want, 0);
    SetEvent(g_stop);
    // Take the socket out under the lock, then close it outside it. (It used to send a polite close while holding
    // the lock: that waits for Ellune's answer, which the worker thread receives, and the worker was waiting for
    // the lock: Loomy hung on /shutdown and on F in /remote.) Closing the handle cancels the waiting receive.
    EnterCriticalSection(&g_cs);
    HINTERNET ws = g_ws;
    g_ws = 0;
    LeaveCriticalSection(&g_cs);
    if (ws) WinHttpCloseHandle(ws);
    InterlockedIncrement(&g_gen);
    if (g_thread) { WaitForSingleObject(g_thread, 3000); CloseHandle(g_thread); g_thread = 0; }
    if (g_session) { g_session = 0; phone_on_closed(); }
    InterlockedExchange(&g_state, NS_OFF);
}

static int send_raw(int type, const void *p, int n) {
    EnterCriticalSection(&g_cs);
    HINTERNET ws = g_ws;
    int ok = ws && wsSend(ws, type, (PVOID)p, (DWORD)n) == 0;   // frames are small: the send returns at once
    LeaveCriticalSection(&g_cs);
    return ok;
}

static int  net_send(const unsigned char *p, int n) { return g_session && send_raw(WS_BIN, p, n); }
static void net_drop(void) { if (g_session) { g_session = 0; send_raw(WS_UTF8, "drop", 4); } }

const PhoneLink PHONE_NET = { L"Ellune", net_listen, net_unlisten, net_listening, net_send, net_drop };

// on the UI thread
void net_event(WPARAM kind, LPARAM data) {
    unsigned char *p = (unsigned char *)data;
    switch ((int)kind) {
    case NE_PHONE:                                     // a phone joined the room: a fresh session
        if (g_session) { g_session = 0; phone_on_closed(); }
        g_session = 1;
        if (!phone_on_accept(&PHONE_NET, L"through Ellune")) { g_session = 0; send_raw(WS_UTF8, "drop", 4); }
        break;
    case NE_DATA:
        if (p && g_session) phone_on_data(p + 2, p[0] << 8 | p[1]);
        break;
    case NE_GONE:
    case NE_LOST:
        if (g_session) { g_session = 0; phone_on_closed(); }
        break;
    }
    if (p) HeapFree(GetProcessHeap(), 0, p);
    ui_redraw();
}

const wchar_t *net_status(int *bad) {
    *bad = 0;
    if (!g_cfg.remoteNet) return L"Ellune: remote is off";
    switch (g_state) {
    case NS_CONNECTED:   return g_session ? L"Ellune: connected \x00B7 your phone is talking through her" : L"Ellune: connected";
    case NS_CONNECTING:  return L"Ellune: connecting...";
    case NS_NOT_GUEST:   *bad = 1; return L"Ellune doesn't know this PC: type /remote add <Authy code>";
    case NS_UNREACHABLE: *bad = 1; return L"Ellune: can't reach her (no internet?), trying again";
    case NS_NO_KEY:      *bad = 1; return L"Ellune: couldn't make this PC's key";
    default:             return g_want ? L"Ellune: starting..." : L"Ellune: waiting for a paired phone";
    }
}

void net_shutdown(void) { net_unlisten(); }
