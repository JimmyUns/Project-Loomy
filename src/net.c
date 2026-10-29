// net.c - the `net` screen: which apps have live internet connections right now.
//
// STEP 1 (this file): lists every process with an active TCP/UDP connection, grouped
// per app, with connection counts and remote hosts. No admin, no per-app speed, no kill.
// Delete is wired up but tells you speeds/kill arrive in step 2.
//
// STEP 2 (later): real per-app throughput via ETW (needs admin, once per session) and
// ending a task. The UI below already leaves room for a speed column.
#include "loomy.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <tlhelp32.h>
#include <shellapi.h>

// ---- bits of the IP Helper API we need, declared by hand so we don't drag in headers ----
#ifndef ANY_SIZE
#define ANY_SIZE 1
#endif
typedef enum { Lmy_TCP_CLOSED = 1, Lmy_TCP_LISTEN = 2, Lmy_TCP_ESTAB = 5 } LmyTcpState;

typedef struct {
    DWORD state, localAddr, localPort, remoteAddr, remotePort, pid;
} LMY_TCPROW;
typedef struct { DWORD num; LMY_TCPROW rows[ANY_SIZE]; } LMY_TCPTABLE;

typedef struct {
    DWORD localAddr, localScope, localPort, remoteAddr, remoteScope, remotePort, state, pid;
} LMY_TCP6ROW;
typedef struct { DWORD num; LMY_TCP6ROW rows[ANY_SIZE]; } LMY_TCP6TABLE;

typedef struct { DWORD localAddr, localPort, pid; } LMY_UDPROW;
typedef struct { DWORD num; LMY_UDPROW rows[ANY_SIZE]; } LMY_UDPTABLE;

#define LMY_TCP_BASIC_ALL 5     // TCP_TABLE_OWNER_PID_ALL
#define LMY_UDP_OWNER_PID 1     // UDP_TABLE_OWNER_PID

typedef DWORD (WINAPI *PFN_GetExtendedTcpTable)(void*, DWORD*, BOOL, ULONG, ULONG, ULONG);
typedef DWORD (WINAPI *PFN_GetExtendedUdpTable)(void*, DWORD*, BOOL, ULONG, ULONG, ULONG);

// ---- per-process I/O counters (for speed) ----
// GetProcessIoCounters gives total bytes read/written by a process. We can't split
// network from disk, but for an app that's clearly downloading it's basically its
// network traffic — fine for a quick glance. Your own apps read without admin;
// system processes need it.
typedef struct {
    LARGE_INTEGER ReadOps, WriteOps, OtherOps, ReadBytes, WriteBytes, OtherBytes;
} LMY_IO_COUNTERS;
typedef BOOL (WINAPI *PFN_GetIo)(HANDLE, LMY_IO_COUNTERS*);
static PFN_GetIo pGetIo;


// toolhelp + psapi, also loaded dynamically
typedef HANDLE (WINAPI *PFN_CreateSnap)(DWORD, DWORD);
typedef BOOL   (WINAPI *PFN_Proc32First)(HANDLE, LPPROCESSENTRY32W);
typedef BOOL   (WINAPI *PFN_Proc32Next)(HANDLE, LPPROCESSENTRY32W);

static PFN_GetExtendedTcpTable pGetTcp;
static PFN_GetExtendedUdpTable pGetUdp;
static PFN_CreateSnap   pSnap;
static PFN_Proc32First  pFirst;
static PFN_Proc32Next   pNext;
static int g_loaded;

static void load_apis(void) {
    if (g_loaded) return;
    g_loaded = 1;
    HMODULE ip = LoadLibraryW(L"iphlpapi.dll");
    if (ip) {
        pGetTcp = (PFN_GetExtendedTcpTable)GetProcAddress(ip, "GetExtendedTcpTable");
        pGetUdp = (PFN_GetExtendedUdpTable)GetProcAddress(ip, "GetExtendedUdpTable");
    }
    HMODULE k = GetModuleHandleW(L"kernel32.dll");
    pSnap  = (PFN_CreateSnap)GetProcAddress(k, "CreateToolhelp32Snapshot");
    pFirst = (PFN_Proc32First)GetProcAddress(k, "Process32FirstW");
    pNext  = (PFN_Proc32Next)GetProcAddress(k, "Process32NextW");
    pGetIo = (PFN_GetIo)GetProcAddress(k, "GetProcessIoCounters");
}

#ifndef PROCESS_QUERY_LIMITED_INFORMATION
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#endif

// read one process's total in/out bytes; returns 1 ok, 0 denied (needs admin)
static int read_io(DWORD pid, unsigned __int64 *rd, unsigned __int64 *wr) {
    if (!pGetIo) return 0;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!h) return 0;
    LMY_IO_COUNTERS io;
    int ok = pGetIo(h, &io) ? 1 : 0;
    CloseHandle(h);
    if (ok) { *rd = (unsigned __int64)io.ReadBytes.QuadPart; *wr = (unsigned __int64)io.WriteBytes.QuadPart; }
    return ok;
}

// ---------------------------------------------------------------- data model
#define MAX_PROCS 256
#define MAX_APPS  128
#define GROUP_PIDS 64            // how many pids we track per app row (for killing the whole app)
#define NAME_LEN  64

// one process that has a live connection
typedef struct {
    DWORD    pid;
    wchar_t  name[NAME_LEN];
    int      conns;
    DWORD    lastRemote;
    int      remoteCount;
    double   downRate, upRate;
    int      haveIo;
} Proc;

// one app = all processes sharing a name, totalled up
typedef struct {
    wchar_t  name[NAME_LEN];
    int      conns;
    DWORD    lastRemote;
    int      remoteCount;
    double   downRate, upRate;
    int      haveIo;            // 1 if we could read at least one of its processes
    int      procCount;         // how many processes are in this group
    DWORD    pids[GROUP_PIDS];  // their pids (for ending the whole app)
    int      npids;
} App;

static Proc g_procs[MAX_PROCS];
static int  g_nprocs;

// remembers last sample's totals per pid, so we can turn deltas into a rate
typedef struct { DWORD pid; unsigned __int64 rd, wr; DWORD tick; } IoPrev;
static IoPrev g_prev[MAX_APPS * 2];
static int    g_nprev;

static IoPrev *prev_for(DWORD pid) {
    for (int i = 0; i < g_nprev; i++) if (g_prev[i].pid == pid) return &g_prev[i];
    return 0;
}
static void prev_store(DWORD pid, unsigned __int64 rd, unsigned __int64 wr, DWORD tick) {
    IoPrev *p = prev_for(pid);
    if (!p) { if (g_nprev >= MAX_APPS * 2) return; p = &g_prev[g_nprev++]; p->pid = pid; }
    p->rd = rd; p->wr = wr; p->tick = tick;
}

static App  g_apps[MAX_APPS];
static int  g_napps, g_sel, g_top, g_vis = 8;
static int  g_listTop, g_rowStep;
static int  g_countdown = 5;
static int  g_confirm = -1;      // index awaiting Delete confirmation, -1 = none
static wchar_t g_note[160];
static int  g_noteKind;

// ---- process names + parents (for tree killing) ----
typedef struct { DWORD pid, parent; wchar_t name[NAME_LEN]; } PName;
static PName g_pnames[1024];
static int   g_npnames;

static void nice_name(wchar_t *exe) {
    int n = s_len(exe);
    if (n > 4 && s_ieq(exe + n - 4, L".exe")) exe[n - 4] = 0;
}

static void snapshot_processes(void) {
    g_npnames = 0;
    if (!pSnap) return;
    HANDLE snap = pSnap(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe; pe.dwSize = sizeof(pe);
    if (pFirst(snap, &pe)) {
        do {
            if (g_npnames >= 1024) break;
            g_pnames[g_npnames].pid = pe.th32ProcessID;
            g_pnames[g_npnames].parent = pe.th32ParentProcessID;
            s_cpy(g_pnames[g_npnames].name, pe.szExeFile, NAME_LEN);
            nice_name(g_pnames[g_npnames].name);
            g_npnames++;
        } while (pNext(snap, &pe));
    }
    CloseHandle(snap);
}

static const wchar_t *name_for(DWORD pid) {
    for (int i = 0; i < g_npnames; i++) if (g_pnames[i].pid == pid) return g_pnames[i].name;
    return L"(unknown)";
}

// per-process accumulator (one entry per pid that has a connection)
static Proc *proc_for(DWORD pid) {
    if (!pid || pid == 4) return 0;
    for (int i = 0; i < g_nprocs; i++) if (g_procs[i].pid == pid) return &g_procs[i];
    if (g_nprocs >= MAX_PROCS) return 0;
    Proc *p = &g_procs[g_nprocs++];
    p->pid = pid; p->conns = 0; p->lastRemote = 0; p->remoteCount = 0;
    p->downRate = p->upRate = 0; p->haveIo = 0;
    s_cpy(p->name, name_for(pid), NAME_LEN);
    return p;
}

// collapse g_procs (per process) into g_apps (per app, by name)
static App *app_for(const wchar_t *name) {
    for (int i = 0; i < g_napps; i++) if (s_ieq(g_apps[i].name, name)) return &g_apps[i];
    if (g_napps >= MAX_APPS) return 0;
    App *a = &g_apps[g_napps++];
    s_cpy(a->name, name, NAME_LEN);
    a->conns = 0; a->lastRemote = 0; a->remoteCount = 0;
    a->downRate = a->upRate = 0; a->haveIo = 0; a->procCount = 0; a->npids = 0;
    return a;
}

static void group_procs(void) {
    g_napps = 0;
    for (int i = 0; i < g_nprocs; i++) {
        Proc *p = &g_procs[i];
        if (p->conns <= 0) continue;
        App *a = app_for(p->name);
        if (!a) continue;
        a->conns += p->conns;
        a->downRate += p->downRate;
        a->upRate += p->upRate;
        a->remoteCount += p->remoteCount;
        if (p->lastRemote && !a->lastRemote) a->lastRemote = p->lastRemote;
        if (p->haveIo) a->haveIo = 1;
        a->procCount++;
        if (a->npids < GROUP_PIDS) a->pids[a->npids++] = p->pid;
    }
}

// busier first: most download, then most upload, then most connections
static int busier(const App *a, const App *b) {
    double at = a->downRate + a->upRate, bt = b->downRate + b->upRate;
    if (at != bt) return at > bt;
    return a->conns > b->conns;
}

static void sort_apps(void) {
    for (int i = 1; i < g_napps; i++) {
        App tmp = g_apps[i]; int j = i - 1;
        while (j >= 0 && busier(&tmp, &g_apps[j])) { g_apps[j + 1] = g_apps[j]; j--; }
        g_apps[j + 1] = tmp;
    }
}

// A generous cap. Even busy machines rarely top a few thousand rows; we bound it so a
// bogus size from the API can never make us read out of bounds.
#define NET_BUF_MAX (1024 * 1024)
static char g_buf[NET_BUF_MAX];

// Fill g_buf for one table type and return how many usable bytes it holds, or 0.
// The correct pattern: first call learns the size, second call fills it. We also
// cap the request so a wild size never overflows the buffer.
static DWORD fetch(PFN_GetExtendedTcpTable fn, ULONG family, ULONG cls) {
    DWORD sz = 0;
    DWORD rc = fn(0, &sz, FALSE, family, cls, 0);          // ask for the needed size
    if (sz == 0) return 0;
    if (sz > NET_BUF_MAX) sz = NET_BUF_MAX;                // never exceed our buffer
    rc = fn(g_buf, &sz, FALSE, family, cls, 0);
    return rc == NO_ERROR ? sz : 0;
}

// how many rows actually fit in `bytes`, given the header + row stride
static DWORD row_cap(DWORD bytes, DWORD headerAndFirst, DWORD rowSize, DWORD claimed) {
    if (bytes < headerAndFirst) return 0;
    DWORD fits = 1 + (bytes - headerAndFirst) / rowSize;   // first row is inside headerAndFirst
    return claimed < fits ? claimed : fits;
}

static void scan(void) {
    g_nprocs = 0;
    g_napps = 0;
    load_apis();
    snapshot_processes();
    if (!pGetTcp) return;

    DWORD bytes;

    // IPv4 TCP
    bytes = fetch(pGetTcp, AF_INET, LMY_TCP_BASIC_ALL);
    if (bytes) {
        LMY_TCPTABLE *t = (LMY_TCPTABLE *)g_buf;
        DWORD n = row_cap(bytes, sizeof(DWORD) + sizeof(LMY_TCPROW), sizeof(LMY_TCPROW), t->num);
        for (DWORD i = 0; i < n; i++) {
            LMY_TCPROW *r = &t->rows[i];
            if (r->state != Lmy_TCP_ESTAB) continue;
            if (!r->remoteAddr || r->remoteAddr == 0x0100007F) continue;   // skip localhost
            Proc *p = proc_for(r->pid);
            if (!p) continue;
            p->conns++; p->remoteCount++; p->lastRemote = r->remoteAddr;
        }
    }
    // IPv6 TCP (counts only)
    bytes = fetch(pGetTcp, AF_INET6, LMY_TCP_BASIC_ALL);
    if (bytes) {
        LMY_TCP6TABLE *t = (LMY_TCP6TABLE *)g_buf;
        DWORD n = row_cap(bytes, sizeof(DWORD) + sizeof(LMY_TCP6ROW), sizeof(LMY_TCP6ROW), t->num);
        for (DWORD i = 0; i < n; i++) {
            LMY_TCP6ROW *r = &t->rows[i];
            if (r->state != Lmy_TCP_ESTAB) continue;
            Proc *p = proc_for(r->pid);
            if (p) { p->conns++; p->remoteCount++; }
        }
    }

    // per-process speed from the byte-count delta since last sample
    DWORD now = GetTickCount();
    for (int i = 0; i < g_nprocs; i++) {
        Proc *p = &g_procs[i];
        if (p->conns <= 0) continue;
        unsigned __int64 rd = 0, wr = 0;
        if (read_io(p->pid, &rd, &wr)) {
            p->haveIo = 1;
            IoPrev *pv = prev_for(p->pid);
            if (pv && pv->tick && now > pv->tick) {
                double secs = (now - pv->tick) / 1000.0;
                if (secs > 0.05) {
                    p->downRate = rd >= pv->rd ? (rd - pv->rd) / secs : 0;
                    p->upRate   = wr >= pv->wr ? (wr - pv->wr) / secs : 0;
                }
            }
            prev_store(p->pid, rd, wr, now);
        }
    }

    group_procs();

#ifdef LOOMY_NET_DEMO
    if (g_napps == 0) {    // fake data so the layout can be seen where real enumeration isn't available
        struct { const wchar_t *n; int c; DWORD ip; int rc; double dn, up; int io; int pc; } d[] = {
            { L"chrome", 24, 0x8EFAA0D1, 9, 7.9e6, 180e3, 1, 6 }, { L"steam", 4, 0x17F0A8C0, 2, 4.2e6, 22e3, 1, 3 },
            { L"Discord", 6, 0xB9C2D688, 3, 240e3, 95e3, 1, 4 }, { L"Spotify", 3, 0x2E3FD923, 1, 160e3, 3e3, 1, 2 },
            { L"svchost (Windows Update)", 2, 0x0C1FA8C0, 1, 0, 0, 0, 1 }, { L"OneDrive", 1, 0x2A5BD1A0, 1, 12e3, 48e3, 1, 1 },
        };
        for (int i = 0; i < 6; i++) {
            App *a = &g_apps[g_napps++];
            s_cpy(a->name, d[i].n, NAME_LEN);
            a->conns = d[i].c; a->lastRemote = d[i].ip; a->remoteCount = d[i].rc;
            a->downRate = d[i].dn; a->upRate = d[i].up; a->haveIo = d[i].io;
            a->procCount = d[i].pc;
            a->npids = d[i].pc < GROUP_PIDS ? d[i].pc : 1;
            for (int k = 0; k < a->npids; k++) a->pids[k] = 90000 + i * 10 + k;  // fake, unkillable
        }
    }
#endif
    sort_apps();
    if (g_sel >= g_napps) g_sel = g_napps > 0 ? g_napps - 1 : 0;
}

// ---------------------------------------------------------------- lifecycle
static int g_active;

int net_active(void) { return g_active; }

void net_open(void) {
    settings_close();
    g_active = 1; g_sel = 0; g_top = 0; g_countdown = 5; g_confirm = -1; g_note[0] = 0;
    scan();
    ui_set_input(L"");
    ui_set_timer_1s(1);          // ask the UI to tick me once a second
    ui_redraw();
}

void net_close(void) {
    if (!g_active) return;
    g_active = 0;
    ui_set_timer_1s(0);
    ui_redraw();
}

void net_tick(void) {            // called once a second by the UI while open
    if (!g_active) return;
    if (--g_countdown <= 0) { scan(); g_countdown = 5; }
    ui_redraw();
}

// ---------------------------------------------------------------- ending a task
#ifndef PROCESS_TERMINATE
#define PROCESS_TERMINATE 0x0001
#endif

// Expand an app's processes into the FULL set to kill: the group's own pids plus every
// descendant (children, grandchildren...). That's what closes a whole app like Discord,
// which runs a main process plus renderer/helper children. Returns the count.
static int collect_tree(const App *a, DWORD *out, int cap) {
    int n = 0;
    for (int i = 0; i < a->npids && n < cap; i++) out[n++] = a->pids[i];
    // keep sweeping the process table, pulling in anything whose parent is already in `out`
    for (int pass = 0; pass < 8; pass++) {
        int added = 0;
        for (int i = 0; i < g_npnames; i++) {
            DWORD pid = g_pnames[i].pid, par = g_pnames[i].parent;
            if (!pid) continue;
            int already = 0, parentIn = 0;
            for (int k = 0; k < n; k++) { if (out[k] == pid) already = 1; if (out[k] == par) parentIn = 1; }
            if (parentIn && !already && n < cap) { out[n++] = pid; added = 1; }
        }
        if (!added) break;
    }
    return n;
}

static int terminate_pid(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!h) return 0;
    int ok = TerminateProcess(h, 1);
    CloseHandle(h);
    return ok;
}

// Launch a second Loomy, elevated, told to kill a list of pids: "--kill p1,p2,p3".
// One UAC prompt; that elevated copy kills them and exits. main() handles --kill (see ui.c).
static int kill_elevated(DWORD *pids, int n) {
    wchar_t exe[MAX_PATH], args[700];
    GetModuleFileNameW(0, exe, MAX_PATH);
    s_cpy(args, L"--kill ", 700);
    for (int i = 0; i < n; i++) {
        wchar_t num[16]; wsprintfW(num, i ? L",%u" : L"%u", pids[i]);
        s_cat(args, num, 700);
    }
    SHELLEXECUTEINFOW si = { sizeof(si) };
    si.lpVerb = L"runas";               // "runas" = ask for admin
    si.lpFile = exe;
    si.lpParameters = args;
    si.nShow = SW_HIDE;
    si.fMask = SEE_MASK_NOCLOSEPROCESS;
    if (!ShellExecuteExW(&si)) return 0; // user said no to UAC, or it failed
    if (si.hProcess) {
        WaitForSingleObject(si.hProcess, 5000);
        DWORD code = 1;
        GetExitCodeProcess(si.hProcess, &code);
        CloseHandle(si.hProcess);
        return code == 0;
    }
    return 1;
}

static void end_task(App *a) {
    DWORD pids[GROUP_PIDS * 4];
    int n = collect_tree(a, pids, GROUP_PIDS * 4);

    // try to end them all as a normal user first; note any we couldn't touch
    int killed = 0, denied = 0;
    DWORD need[GROUP_PIDS * 4]; int nneed = 0;
    for (int i = 0; i < n; i++) {
        if (terminate_pid(pids[i])) killed++;
        else { denied++; if (nneed < GROUP_PIDS * 4) need[nneed++] = pids[i]; }
    }

    if (!denied) {
        g_noteKind = OUT_OK;
        wsprintfW(g_note, L"Ended %s (%d process%s).", a->name, killed, killed == 1 ? L"" : L"es");
        return;
    }

    // some pieces are protected (system-level) — elevate once to finish them
    g_noteKind = OUT_DIM;
    wsprintfW(g_note, L"%s needs admin. Approve the prompt...", a->name);
    ui_redraw();
    if (kill_elevated(need, nneed)) {
        g_noteKind = OUT_OK;
        wsprintfW(g_note, L"Ended %s.", a->name);
    } else {
        g_noteKind = OUT_ERR;
        wsprintfW(g_note, L"Ended part of %s, but couldn't finish it.", a->name);
    }
}

// ---------------------------------------------------------------- input
static void keep_visible(void) {
    if (g_sel < g_top) g_top = g_sel;
    if (g_sel >= g_top + g_vis) g_top = g_sel - g_vis + 1;
    if (g_top < 0) g_top = 0;
}

int net_key(WPARAM vk) {
    switch (vk) {
    case VK_UP:    if (g_sel > 0) g_sel--; g_confirm = -1; keep_visible(); ui_redraw(); break;
    case VK_DOWN:  if (g_sel < g_napps - 1) g_sel++; g_confirm = -1; keep_visible(); ui_redraw(); break;
    case 'R':      scan(); g_countdown = 5; g_note[0] = 0; ui_redraw(); break;
    case VK_DELETE:
        if (!g_napps) break;
        if (g_confirm == g_sel) {
            end_task(&g_apps[g_sel]);
            g_confirm = -1;
            scan(); g_countdown = 5;
        } else {
            g_confirm = g_sel;
            g_noteKind = OUT_ERR;
            int pc = g_apps[g_sel].procCount;
            if (pc > 1)
                wsprintfW(g_note, L"End %s? (%d processes) Delete again to confirm, any key cancels.", g_apps[g_sel].name, pc);
            else
                wsprintfW(g_note, L"End %s? Delete again to confirm, any key cancels.", g_apps[g_sel].name);
        }
        ui_redraw();
        break;
    case VK_ESCAPE:
        if (g_confirm >= 0) { g_confirm = -1; g_note[0] = 0; ui_redraw(); }
        else net_close();
        break;
    default:
        if (g_confirm >= 0) { g_confirm = -1; g_note[0] = 0; ui_redraw(); }
        break;
    }
    return 1;
}

int net_char(WPARAM ch) { return 1; }

static int row_at(int y) {
    if (!g_rowStep || y < g_listTop) return -1;
    int i = g_top + (y - g_listTop) / g_rowStep;
    return (i < g_napps && i < g_top + g_vis) ? i : -1;
}

void net_click(int x, int y) {
    int i = row_at(y);
    if (i < 0) return;
    g_sel = i; g_confirm = -1; ui_redraw();
}

void net_wheel(int notches) {
    g_sel -= notches;
    if (g_sel < 0) g_sel = 0;
    if (g_sel > g_napps - 1) g_sel = g_napps > 0 ? g_napps - 1 : 0;
    keep_visible(); ui_redraw();
}

// ---------------------------------------------------------------- drawing
// bytes/sec -> "7.9 MB/s" / "240 KB/s" / "0". Keeps it short for the column.
static void rate_str(double bps, wchar_t *buf) {
    if (bps < 1024) { s_cpy(buf, L"0", 24); return; }
    if (bps < 1024.0 * 1024.0) {
        int kb = (int)(bps / 1024.0 + 0.5);
        wsprintfW(buf, L"%d KB/s", kb);
    } else {
        double mb = bps / (1024.0 * 1024.0);
        int whole = (int)mb;
        int frac = (int)((mb - whole) * 10 + 0.5);
        if (frac == 10) { whole++; frac = 0; }
        wsprintfW(buf, L"%d.%d MB/s", whole, frac);
    }
}

void net_paint(HDC dc, RECT a) {
    SelectObject(dc, g_font);
    int w = ui_text(dc, a.left, a.top, L"Network", g_accent);

    // totals across everything we could measure
    double totDn = 0, totUp = 0;
    for (int i = 0; i < g_napps; i++) { totDn += g_apps[i].downRate; totUp += g_apps[i].upRate; }
    wchar_t dn[24], up[24];
    rate_str(totDn, dn); rate_str(totUp, up);
    wchar_t head[96];
    wsprintfW(head, L"\x2193 %s   \x2191 %s   \x00B7 refresh in %ds", dn, up, g_countdown);
    ui_text(dc, a.left + w + S(16), a.top, head, C_DIM);

    int y = a.top + g_lineH + S(14);

    int colDown = S(248), colUp = S(350), colConn = S(452);

    // column header
    SelectObject(dc, g_fontSmall);
    ui_text(dc, a.left + S(28), y, L"App", C_DIM);
    ui_text(dc, a.left + colDown, y, L"\x2193 Down", C_DIM);
    ui_text(dc, a.left + colUp, y, L"\x2191 Up", C_DIM);
    ui_text(dc, a.left + colConn, y, L"Conns", C_DIM);
    y += g_lineH + S(6);
    SelectObject(dc, g_font);

    int rowH = g_lineH + S(10);
    g_rowStep = rowH + S(2);
    g_listTop = y;
    int bottom = a.bottom - S(26);
    g_vis = (bottom - y) / g_rowStep;
    if (g_vis < 1) g_vis = 1;
    keep_visible();

    if (!g_napps) {
        ui_text(dc, a.left + S(10), y + S(4), L"Nothing's talking to the internet right now.", C_TEXT);
        ui_text(dc, a.left + S(10), y + S(4) + g_lineH + S(6), L"Press R to re-check.", C_DIM);
    }

    for (int i = g_top; i < g_napps && i < g_top + g_vis; i++) {
        App *ap = &g_apps[i];
        int sel = i == g_sel;
        int warn = i == g_confirm;
        if (warn)      ui_fill(dc, a.left, y, a.right, y + rowH, ui_mix(C_ERR, C_BG, 16));
        else if (sel)  ui_sel_row(dc, a.left, y, a.right, y + rowH);
        int ty = y + (rowH - g_lineH) / 2;
        if (sel) ui_text(dc, a.left + S(10), ty, L"\x203A", warn ? C_ERR : g_accent);
        ui_text(dc, a.left + S(28), ty, ap->name, sel ? C_BRIGHT : C_TEXT);

        wchar_t r[24];
        if (!ap->haveIo) {
            ui_text(dc, a.left + colDown, ty, L"needs admin", C_DIM);
        } else {
            rate_str(ap->downRate, r);
            ui_text(dc, a.left + colDown, ty, r, ap->downRate >= 1024 ? (sel ? C_BRIGHT : C_TEXT) : C_DIM);
            rate_str(ap->upRate, r);
            ui_text(dc, a.left + colUp, ty, r, ap->upRate >= 1024 ? (sel ? C_BRIGHT : C_TEXT) : C_DIM);
        }

        wchar_t c[32];
        if (ap->procCount > 1) wsprintfW(c, L"%d  (%d proc)", ap->conns, ap->procCount);
        else                   wsprintfW(c, L"%d", ap->conns);
        ui_text(dc, a.left + colConn, ty, c, sel ? C_BRIGHT : C_TEXT);

        y += g_rowStep;
    }

    if (g_napps > g_vis) {
        wchar_t pos[32]; wsprintfW(pos, L"%d/%d", g_sel + 1, g_napps);
        SelectObject(dc, g_fontSmall);
        SIZE sz; GetTextExtentPoint32W(dc, pos, s_len(pos), &sz);
        ui_text(dc, a.right - sz.cx, a.bottom - S(16), pos, C_DIM);
        SelectObject(dc, g_font);
    }

    if (g_note[0]) {
        COLORREF col = g_noteKind == OUT_ERR ? C_ERR : g_noteKind == OUT_OK ? C_OK : C_DIM;
        SelectObject(dc, g_fontSmall);
        ui_text(dc, a.left, a.bottom - S(16) - g_lineH, g_note, col);
        SelectObject(dc, g_font);
    }

    SelectObject(dc, g_fontSmall);
    ui_text(dc, a.left, a.bottom - S(16),
            L"\x2191\x2193 choose    R refresh now    Del end task    Esc close", C_DIM);
    SelectObject(dc, g_font);
}
