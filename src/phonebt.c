// phonebt.c - the Windows half of Pocket Loomy's plumbing (phone.c has the rules):
//   - an RFCOMM Bluetooth listener, announced with an SDP record under Pocket Loomy's service UUID,
//     with socket events arriving through WSAAsyncSelect, so waiting costs nothing (no thread, no timer)
//   - secure random (BCryptGenRandom), DPAPI for the trust file, the clock, this PC's name
// bcrypt.dll and crypt32.dll are loaded from System32 only when the phone feature first needs them.
// None of this runs unless the phone feature is on (/pkg add phone).
#include "loomy.h"
#include <winsock2.h>
#include <ws2bth.h>
#include "monocypher.h"

#ifndef SO_BTH_AUTHENTICATE
#define SO_BTH_AUTHENTICATE 0x80000001
#endif
#ifndef LOAD_LIBRARY_SEARCH_SYSTEM32
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800
#endif

// Pocket Loomy's service UUID 89609207-27e0-4126-90fe-40d16244b0cd (the Android app asks for the same)
static GUID SERVICE_ID = { 0x89609207, 0x27e0, 0x4126, { 0x90, 0xfe, 0x40, 0xd1, 0x62, 0x44, 0xb0, 0xcd } };

static SOCKET       g_listen = INVALID_SOCKET, g_sock = INVALID_SOCKET;
static int          g_wsa;
static SOCKADDR_BTH g_local;
static CSADDR_INFO  g_csa;
static WSAQUERYSETW g_qs;

static int wsa_up(void) {
    if (!g_wsa) { WSADATA d; if (WSAStartup(MAKEWORD(2, 2), &d) == 0) g_wsa = 1; }
    return g_wsa;
}

int bt_listening(void) { return g_listen != INVALID_SOCKET; }

int bt_listen(wchar_t *err, int cap) {
    if (g_listen != INVALID_SOCKET) return 1;
    if (!wsa_up()) { s_cpy(err, L"Windows networking didn't start.", cap); return 0; }
    SOCKET s = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
    if (s == INVALID_SOCKET) { s_cpy(err, L"No Bluetooth on this PC, or it's turned off.", cap); return 0; }

    // only devices paired with Windows (Settings > Bluetooth) get to connect at all; our own
    // pairing code sits on top of that and doesn't rely on it
    ULONG on = TRUE;
    setsockopt(s, SOL_RFCOMM, SO_BTH_AUTHENTICATE, (const char *)&on, sizeof on);

    SOCKADDR_BTH a;
    memset(&a, 0, sizeof a);
    a.addressFamily = AF_BTH;
    a.port = BT_PORT_ANY;
    int len = sizeof g_local;
    if (bind(s, (SOCKADDR *)&a, sizeof a) || getsockname(s, (SOCKADDR *)&g_local, &len) || listen(s, 1) ||
        WSAAsyncSelect(s, ui_hwnd(), WM_PHONE, FD_ACCEPT)) {
        closesocket(s);
        s_cpy(err, L"Bluetooth is off, or Windows wouldn't let Loomy listen.", cap);
        return 0;
    }

    memset(&g_csa, 0, sizeof g_csa);
    g_csa.LocalAddr.lpSockaddr = (LPSOCKADDR)&g_local;
    g_csa.LocalAddr.iSockaddrLength = sizeof g_local;
    g_csa.iSocketType = SOCK_STREAM;
    g_csa.iProtocol = BTHPROTO_RFCOMM;
    memset(&g_qs, 0, sizeof g_qs);
    g_qs.dwSize = sizeof g_qs;
    g_qs.lpszServiceInstanceName = (LPWSTR)L"Loomy";
    g_qs.lpszComment = (LPWSTR)L"Pocket Loomy";
    g_qs.lpServiceClassId = &SERVICE_ID;
    g_qs.dwNameSpace = NS_BTH;
    g_qs.dwNumberOfCsAddrs = 1;
    g_qs.lpcsaBuffer = &g_csa;
    if (WSASetServiceW(&g_qs, RNRSERVICE_REGISTER, 0)) {
        closesocket(s);
        s_cpy(err, L"Couldn't announce Loomy over Bluetooth (SDP record).", cap);
        return 0;
    }
    g_listen = s;
    return 1;
}

void bt_unlisten(void) {
    if (g_listen == INVALID_SOCKET) return;
    WSASetServiceW(&g_qs, RNRSERVICE_DELETE, 0);
    closesocket(g_listen);
    g_listen = INVALID_SOCKET;
}

void bt_drop(void) {
    if (g_sock == INVALID_SOCKET) return;
    closesocket(g_sock);                       // graceful: anything already sent still goes out
    g_sock = INVALID_SOCKET;
}

int bt_send(const unsigned char *p, int n) {
    int tries = 0;
    while (n > 0 && g_sock != INVALID_SOCKET) {
        int k = send(g_sock, (const char *)p, n, 0);
        if (k == SOCKET_ERROR) {
            // frames are tiny, so a full send buffer is rare: give the radio a moment, briefly
            if (WSAGetLastError() != WSAEWOULDBLOCK || ++tries > 40) return 0;
            Sleep(5);
            continue;
        }
        p += k; n -= k;
    }
    return n == 0;
}

static void addr_text(BTH_ADDR a, wchar_t *o) {
    wsprintfW(o, L"%02X:%02X:%02X:%02X:%02X:%02X", (int)(a >> 40) & 255, (int)(a >> 32) & 255,
              (int)(a >> 24) & 255, (int)(a >> 16) & 255, (int)(a >> 8) & 255, (int)a & 255);
}

void bt_event(WPARAM w, LPARAM lp) {
    SOCKET s = (SOCKET)w;
    int ev = WSAGETSELECTEVENT(lp);
    if (s == g_listen && g_listen != INVALID_SOCKET) {
        if (ev != FD_ACCEPT) return;
        SOCKADDR_BTH peer;
        int len = sizeof peer;
        memset(&peer, 0, sizeof peer);
        SOCKET c = accept(g_listen, (SOCKADDR *)&peer, &len);
        if (c == INVALID_SOCKET) return;
        if (g_sock != INVALID_SOCKET) { closesocket(c); return; }    // one phone at a time
        wchar_t addr[20];
        addr_text(peer.btAddr, addr);
        WSAAsyncSelect(c, ui_hwnd(), WM_PHONE, FD_READ | FD_CLOSE);
        g_sock = c;
        if (!phone_on_accept(addr) && g_sock == c) { closesocket(c); g_sock = INVALID_SOCKET; }
        return;
    }
    if (g_sock == INVALID_SOCKET || s != g_sock) return;             // a socket we already closed
    unsigned char buf[600];
    if (ev == FD_READ) {
        int n = recv(g_sock, (char *)buf, sizeof buf, 0);
        if (n > 0) { phone_on_data(buf, n); return; }
        if (n == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) return;
        ev = FD_CLOSE;
    }
    if (ev == FD_CLOSE) {
        for (int i = 0; i < 8 && g_sock != INVALID_SOCKET; i++) {   // whatever arrived just before the hang-up
            int n = recv(g_sock, (char *)buf, sizeof buf, 0);
            if (n <= 0) break;
            phone_on_data(buf, n);
        }
        bt_drop();
        phone_on_closed();
    }
}

void bt_shutdown(void) {
    bt_drop();
    bt_unlisten();
    if (g_wsa) { WSACleanup(); g_wsa = 0; }
}

// ---------------------------------------------------------------- OS services
static HMODULE sys_dll(const wchar_t *name) { return LoadLibraryExW(name, 0, LOAD_LIBRARY_SEARCH_SYSTEM32); }

typedef LONG (WINAPI *PFN_GenRandom)(void *, unsigned char *, ULONG, ULONG);
int os_random(void *p, int n) {
    static PFN_GenRandom gen;
    static int tried;
    if (!tried) {
        tried = 1;
        HMODULE b = sys_dll(L"bcrypt.dll");
        if (b) gen = (PFN_GenRandom)GetProcAddress(b, "BCryptGenRandom");
    }
    return gen && n >= 0 && gen(0, (unsigned char *)p, (ULONG)n, 2 /*BCRYPT_USE_SYSTEM_PREFERRED_RNG*/) == 0;
}

typedef struct { DWORD cb; BYTE *pb; } LmyBlob;
typedef BOOL (WINAPI *PFN_Protect)(LmyBlob *, LPCWSTR, LmyBlob *, void *, void *, DWORD, LmyBlob *);
typedef BOOL (WINAPI *PFN_Unprotect)(LmyBlob *, LPWSTR *, LmyBlob *, void *, void *, DWORD, LmyBlob *);

// DPAPI with your Windows login: only you, on this PC, can read the trust file back
int os_protect(const unsigned char *in, int n, unsigned char *out, int cap, int unprotect) {
    static PFN_Protect pro;
    static PFN_Unprotect unpro;
    static int tried;
    if (!tried) {
        tried = 1;
        HMODULE c = sys_dll(L"crypt32.dll");
        if (c) {
            pro = (PFN_Protect)GetProcAddress(c, "CryptProtectData");
            unpro = (PFN_Unprotect)GetProcAddress(c, "CryptUnprotectData");
        }
    }
    if (!pro || !unpro || n <= 0) return 0;
    LmyBlob a = { (DWORD)n, (BYTE *)in }, b = { 0, 0 }, ent = { 16, (BYTE *)"LoomyPocketTrust" };
    BOOL ok = unprotect ? unpro(&a, 0, &ent, 0, 0, 1 /*UI_FORBIDDEN*/, &b)
                        : pro(&a, L"Loomy phone", &ent, 0, 0, 1, &b);
    if (!ok || !b.pb) return 0;
    int k = (int)b.cb;
    if (k <= cap) memcpy(out, b.pb, k);
    crypto_wipe(b.pb, b.cb);
    LocalFree(b.pb);
    return k <= cap ? k : 0;
}

ULONGLONG os_now(void) {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return (((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime) / 10000000ull;
}

void os_fmt_time(ULONGLONG secs, wchar_t *buf, int cap, int withDay) {
    static const wchar_t *DAY[] = { L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat" };
    static const wchar_t *MON[] = { L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun", L"Jul", L"Aug",
                                    L"Sep", L"Oct", L"Nov", L"Dec" };
    ULONGLONG t = secs * 10000000ull;
    FILETIME ft = { (DWORD)t, (DWORD)(t >> 32) }, lt;
    SYSTEMTIME st;
    wchar_t s[48];
    if (!FileTimeToLocalFileTime(&ft, &lt) || !FileTimeToSystemTime(&lt, &st)) { s_cpy(buf, L"?", cap); return; }
    if (withDay) wsprintfW(s, L"%s %d %s, %02d:%02d", DAY[st.wDayOfWeek % 7], st.wDay, MON[(st.wMonth + 11) % 12],
                           st.wHour, st.wMinute);
    else wsprintfW(s, L"%02d:%02d", st.wHour, st.wMinute);
    s_cpy(buf, s, cap);
}

int os_pc_name(char *utf8, int cap) {
    wchar_t w[64];
    DWORD n = 64;
    if (!GetComputerNameW(w, &n)) return 0;
    int k = WideCharToMultiByte(CP_UTF8, 0, w, (int)n, utf8, cap, 0, 0);
    return k > 0 ? k : 0;
}
