// sysdata.c - reads the machine for `sysinfo`: CPU, every GPU, memory, disk, network, top apps.
//
// All of it works without admin, and nothing here runs unless the sysinfo panel is open:
// sysdata_open() loads what it needs, sysdata_sample() runs once a second, sysdata_close()
// frees it all again. The DLLs (pdh, ntdll bits, the D3DKMT calls in gdi32) are loaded by
// hand so they cost nothing at startup.
//
// Where the numbers come from:
//   CPU load, speed, waiting threads, driver time, hard faults, disk, network, GPU load and
//     GPU memory:   performance counters (PDH, the same ones Task Manager reads)
//   per-thread load + per-app CPU/memory:  NtQuerySystemInformation
//   GPU list, names, VRAM size, temperature, clock:  D3DKMT (the graphics kernel, WDDM 2.4+
//     for temperature/clock; older drivers just show n/a)
#include "loomy.h"
#include <pdh.h>
#include <pdhmsg.h>

// ---------------------------------------------------------------- PDH (loaded by hand)
typedef PDH_STATUS (WINAPI *PFN_OpenQuery)(LPCWSTR, DWORD_PTR, PDH_HQUERY *);
typedef PDH_STATUS (WINAPI *PFN_AddCounter)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER *);
typedef PDH_STATUS (WINAPI *PFN_Collect)(PDH_HQUERY);
typedef PDH_STATUS (WINAPI *PFN_GetValue)(PDH_HCOUNTER, DWORD, LPDWORD, PPDH_FMT_COUNTERVALUE);
typedef PDH_STATUS (WINAPI *PFN_GetArray)(PDH_HCOUNTER, DWORD, LPDWORD, LPDWORD, PPDH_FMT_COUNTERVALUE_ITEM_W);
typedef PDH_STATUS (WINAPI *PFN_CloseQuery)(PDH_HQUERY);

static HMODULE        g_pdh;
static PFN_OpenQuery  pOpen;
static PFN_AddCounter pAdd;
static PFN_Collect    pCollect;
static PFN_GetValue   pValue;
static PFN_GetArray   pArray;
static PFN_CloseQuery pCloseQ;
static PDH_HQUERY     g_q;

enum { K_UTIL, K_PERF, K_QUEUE, K_DPC, K_INT, K_FAULTS, K_DISKIDLE, K_DISKR, K_DISKW,
       K_NETD, K_NETU, K_GPUENG, K_GPUDED, K_GPUSHR, NK };
static const wchar_t *COUNTER[NK] = {
    L"\\Processor Information(_Total)\\% Processor Utility",
    L"\\Processor Information(_Total)\\% Processor Performance",
    L"\\System\\Processor Queue Length",
    L"\\Processor(_Total)\\% DPC Time",
    L"\\Processor(_Total)\\% Interrupt Time",
    L"\\Memory\\Pages Input/sec",
    L"\\PhysicalDisk(_Total)\\% Idle Time",
    L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec",
    L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec",
    L"\\Network Interface(*)\\Bytes Received/sec",
    L"\\Network Interface(*)\\Bytes Sent/sec",
    L"\\GPU Engine(*)\\Utilization Percentage",
    L"\\GPU Adapter Memory(*)\\Dedicated Usage",
    L"\\GPU Adapter Memory(*)\\Shared Usage",
};
static PDH_HCOUNTER g_k[NK];

#define FMT (PDH_FMT_DOUBLE | PDH_FMT_NOCAP100)

static int pdh_value(int k, double *v) {
    if (!g_k[k]) return 0;
    PDH_FMT_COUNTERVALUE cv;
    if (pValue(g_k[k], FMT, 0, &cv) != ERROR_SUCCESS) return 0;
    if (cv.CStatus != PDH_CSTATUS_VALID_DATA && cv.CStatus != PDH_CSTATUS_NEW_DATA) return 0;
    *v = cv.doubleValue;
    return 1;
}

// wildcard counters come back as an array (one item per instance). The buffer grows as needed.
static PDH_FMT_COUNTERVALUE_ITEM_W *g_arr;
static DWORD g_arrCap;

static int pdh_array(int k, DWORD *count) {
    *count = 0;
    if (!g_k[k]) return 0;
    DWORD sz = 0, n = 0;
    if (pArray(g_k[k], FMT, &sz, &n, 0) != (PDH_STATUS)PDH_MORE_DATA || !sz) return 0;
    if (sz > 16 * 1024 * 1024) return 0;                     // something's off, don't chase it
    if (sz > g_arrCap) {
        HANDLE heap = GetProcessHeap();
        if (g_arr) HeapFree(heap, 0, g_arr);
        g_arrCap = sz + 16 * 1024;
        g_arr = HeapAlloc(heap, 0, g_arrCap);
        if (!g_arr) { g_arrCap = 0; return 0; }
    }
    sz = g_arrCap;
    if (pArray(g_k[k], FMT, &sz, &n, g_arr) != ERROR_SUCCESS) return 0;
    *count = n;
    return 1;
}

static int item_ok(const PDH_FMT_COUNTERVALUE_ITEM_W *it) {
    return it->FmtValue.CStatus == PDH_CSTATUS_VALID_DATA || it->FmtValue.CStatus == PDH_CSTATUS_NEW_DATA;
}

static double pdh_sum(int k) {
    DWORD n; double t = 0;
    if (!pdh_array(k, &n)) return 0;
    for (DWORD i = 0; i < n; i++) if (item_ok(&g_arr[i]) && g_arr[i].FmtValue.doubleValue > 0) t += g_arr[i].FmtValue.doubleValue;
    return t;
}

// ---------------------------------------------------------------- ntdll + kernel32 extras
typedef LONG (WINAPI *PFN_NtQSI)(ULONG, void *, ULONG, ULONG *);
static PFN_NtQSI pNtQSI;

typedef struct {
    DWORD  cb;
    SIZE_T CommitTotal, CommitLimit, CommitPeak, PhysicalTotal, PhysicalAvailable, SystemCache,
           KernelTotal, KernelPaged, KernelNonpaged, PageSize;
    DWORD  HandleCount, ProcessCount, ThreadCount;
} LMY_PERFINFO;
typedef BOOL (WINAPI *PFN_PerfInfo)(LMY_PERFINFO *, DWORD);
static PFN_PerfInfo pPerfInfo;

typedef struct { LARGE_INTEGER Idle, Kernel, User, Dpc, Interrupt; ULONG InterruptCount; } LMY_CPUTIMES;

typedef struct {                     // SYSTEM_PROCESS_INFORMATION, the part we read
    ULONG NextEntryOffset, NumberOfThreads;
    LARGE_INTEGER WorkingSetPrivateSize;
    ULONG HardFaultCount, NumberOfThreadsHighWatermark;
    ULONGLONG CycleTime;
    LARGE_INTEGER CreateTime, UserTime, KernelTime;
    USHORT NameLength, NameMax; wchar_t *NameBuffer;
    LONG BasePriority;
    HANDLE UniqueProcessId;
} LMY_PROC;

#define SPI_PROCESSES 5
#define SPI_CPUTIMES  8
#define STATUS_LENGTH_MISMATCH ((LONG)0xC0000004)

// ---------------------------------------------------------------- D3DKMT (gdi32, by hand)
typedef UINT KMT_HANDLE;
typedef struct { KMT_HANDLE hAdapter; LUID luid; ULONG sources; BOOL precise; } KMT_ADAPTERINFO;
typedef struct { ULONG count; KMT_ADAPTERINFO *list; } KMT_ENUM2;
typedef struct { KMT_HANDLE hAdapter; int type; void *data; UINT size; } KMT_QUERY;
typedef struct { KMT_HANDLE hAdapter; } KMT_CLOSE;
typedef struct { wchar_t adapter[MAX_PATH], chip[MAX_PATH], dac[MAX_PATH], bios[MAX_PATH]; } KMT_REGINFO;
typedef struct { ULONGLONG dedicated, dedicatedSys, shared; } KMT_SEGSIZE;
typedef struct { UINT32 node, phys; ULONGLONG freq, maxFreq, maxFreqOC; ULONG volt, voltMax, voltMaxOC; ULONGLONG latency; } KMT_NODEPERF;
typedef struct { UINT32 phys; ULONGLONG memFreq, memFreqMax, memFreqMaxOC, memBw, pcieBw; ULONG fanRPM, power, temp; UCHAR override; } KMT_ADAPTERPERF;
typedef struct { UINT32 phys; ULONGLONG maxMemBw, maxPcieBw; ULONG maxFan, tempMax, tempWarn; } KMT_ADAPTERPERFCAPS;

enum { QAI_SEGSIZE = 3, QAI_REGINFO = 8, QAI_TYPE = 15, QAI_NODEPERF = 61, QAI_ADAPTERPERF = 62, QAI_PERFCAPS = 63 };
#define ATYPE_RENDER      0x01
#define ATYPE_SOFTWARE    0x04
#define ATYPE_HYBRID_IGPU 0x20

typedef LONG (APIENTRY *PFN_KmtEnum2)(KMT_ENUM2 *);
typedef LONG (APIENTRY *PFN_KmtQuery)(const KMT_QUERY *);
typedef LONG (APIENTRY *PFN_KmtClose)(const KMT_CLOSE *);
static PFN_KmtEnum2 pKEnum;
static PFN_KmtQuery pKQuery;
static PFN_KmtClose pKClose;

static int kmt_query(KMT_HANDLE h, int type, void *data, UINT size) {
    if (!pKQuery) return 0;
    KMT_QUERY q = { h, type, data, size };
    return pKQuery(&q) == 0;
}

// ---------------------------------------------------------------- state
#define MAXKMT 16
static KMT_HANDLE g_open[MAXKMT];            // every adapter handle we hold (closed on close)
static int        g_nopen;
static KMT_HANDLE g_gh[SI_MAXGPU];           // per shown GPU
static LUID       g_gluid[SI_MAXGPU];

static LMY_CPUTIMES g_cpuPrev[SI_MAXCPU], g_cpuNow[SI_MAXCPU];
static int    g_havePrevCpu, g_priming;
static ULONGLONG g_lastTick;

static BYTE  *g_pbuf;                        // process list from the kernel
static ULONG  g_pcap;

// per-process CPU time last sample, sorted by pid so we can binary-search it
#define MAXP 4096
typedef struct { ULONG_PTR pid; ULONGLONG t; } PrevT;
static PrevT g_prevT[MAXP], g_curT[MAXP];
static int   g_nprevT, g_ncurT;

typedef struct { wchar_t name[40]; ULONGLONG t; ULONGLONG mem; } Group;
#define MAXG 512
static Group g_grp[MAXG];
static int   g_ngrp;

// ---------------------------------------------------------------- small helpers
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void push_hist(unsigned char *h, int v) {
    for (int i = 0; i < SI_HIST - 1; i++) h[i] = h[i + 1];
    h[SI_HIST - 1] = (unsigned char)clampi(v, 0, 100);
}

// "Intel(R) Core(TM) i7-1165G7 @ 2.80GHz" -> "Intel Core i7-1165G7"
static void tidy_name(wchar_t *s) {
    wchar_t *r = s, *w = s;
    while (*r) {
        if (s_istarts(r, L"(R)")) { r += 3; continue; }
        if (s_istarts(r, L"(TM)")) { r += 4; continue; }
        if (s_istarts(r, L" @ ") || s_istarts(r, L" with ")) break;
        if (*r == L' ' && (w == s || w[-1] == L' ')) { r++; continue; }
        *w++ = *r++;
    }
    while (w > s && w[-1] == L' ') w--;
    *w = 0;
    int n = s_len(s);
    if (n > 4 && s_ieq(s + n - 4, L" CPU")) s[n - 4] = 0;
}

static int hexval(wchar_t c) {
    if (c >= L'0' && c <= L'9') return c - L'0';
    c = s_low(c);
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    return -1;
}

// counter instance names carry the adapter: "...luid_0x00000000_0x0000C3A1_phys_0..."
static int parse_luid(const wchar_t *s, LUID *out) {
    const wchar_t *p = s;
    while (*p && !s_istarts(p, L"luid_0x")) p++;
    if (!*p) return 0;
    p += 7;
    unsigned hi = 0, lo = 0; int d;
    while ((d = hexval(*p)) >= 0) { hi = hi * 16 + d; p++; }
    if (!s_istarts(p, L"_0x")) return 0;
    p += 3;
    while ((d = hexval(*p)) >= 0) { lo = lo * 16 + d; p++; }
    out->HighPart = (LONG)hi; out->LowPart = lo;
    return 1;
}

static int gpu_for_luid(const SiSnap *s, LUID l) {
    for (int i = 0; i < s->ngpu; i++)
        if (g_gluid[i].LowPart == l.LowPart && g_gluid[i].HighPart == l.HighPart) return i;
    return -1;
}

// "VideoDecode" -> "Video decode", "Compute_0" -> "Compute 0"
static void engine_label(const wchar_t *raw, wchar_t *out, int cap) {
    int n = 0;
    for (int i = 0; raw[i] && n < cap - 1; i++) {
        wchar_t c = raw[i];
        if (c == L'_') c = L' ';
        if (i > 0 && c >= L'A' && c <= L'Z' && raw[i - 1] >= L'a' && raw[i - 1] <= L'z') {
            if (n < cap - 2) { out[n++] = L' '; c = s_low(c); }
        }
        out[n++] = c;
    }
    out[n] = 0;
}

// ---------------------------------------------------------------- setup
static void read_cpu_info(SiSnap *s) {
    DWORD cb = sizeof(s->cpuName);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                     L"ProcessorNameString", RRF_RT_REG_SZ, 0, s->cpuName, &cb) == ERROR_SUCCESS)
        tidy_name(s->cpuName);
    else s_cpy(s->cpuName, L"CPU", 64);
    DWORD mhz = 0; cb = sizeof(mhz);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                     L"~MHz", RRF_RT_REG_DWORD, 0, &mhz, &cb) == ERROR_SUCCESS) s->baseMHz = (int)mhz;

    SYSTEM_INFO si; GetSystemInfo(&si);
    s->logical = (int)si.dwNumberOfProcessors;

    static SYSTEM_LOGICAL_PROCESSOR_INFORMATION lpi[256];
    DWORD len = sizeof(lpi);
    s->cores = 0;
    if (GetLogicalProcessorInformation(lpi, &len))
        for (DWORD i = 0; i < len / sizeof(lpi[0]); i++)
            if (lpi[i].Relationship == RelationProcessorCore) s->cores++;
    if (!s->cores) s->cores = s->logical;
}

static void open_gpus(SiSnap *s) {
    HMODULE gdi = GetModuleHandleW(L"gdi32.dll");
    if (!gdi) return;
    pKEnum  = (PFN_KmtEnum2)GetProcAddress(gdi, "D3DKMTEnumAdapters2");
    pKQuery = (PFN_KmtQuery)GetProcAddress(gdi, "D3DKMTQueryAdapterInfo");
    pKClose = (PFN_KmtClose)GetProcAddress(gdi, "D3DKMTCloseAdapter");
    if (!pKEnum || !pKQuery) return;

    KMT_ADAPTERINFO list[MAXKMT];
    KMT_ENUM2 e = { 0, 0 };
    if (pKEnum(&e) != 0) return;               // first call: how many
    if (e.count > MAXKMT) e.count = MAXKMT;
    e.list = list;
    if (pKEnum(&e) != 0) return;
    g_nopen = (int)e.count;

    // discrete GPUs first (they're the ones you care about in a game), then integrated
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < g_nopen; i++) {
            g_open[i] = list[i].hAdapter;
            UINT type = 0;
            int haveType = kmt_query(list[i].hAdapter, QAI_TYPE, &type, sizeof(type));
            if (haveType && ((type & ATYPE_SOFTWARE) || !(type & ATYPE_RENDER))) continue;
            static KMT_REGINFO reg;
            if (!kmt_query(list[i].hAdapter, QAI_REGINFO, &reg, sizeof(reg))) reg.adapter[0] = 0;
            if (s_has(reg.adapter, L"Microsoft Basic Render")) continue;
            KMT_SEGSIZE seg = { 0, 0, 0 };
            kmt_query(list[i].hAdapter, QAI_SEGSIZE, &seg, sizeof(seg));
            int igpu = (type & ATYPE_HYBRID_IGPU) || seg.dedicated < 512ull * 1024 * 1024;
            if (igpu != pass) continue;
            if (s->ngpu >= SI_MAXGPU) continue;

            SiGpu *g = &s->gpu[s->ngpu];
            s_cpy(g->name, reg.adapter[0] ? reg.adapter : L"GPU", 80);
            tidy_name(g->name);
            g->integrated = igpu;
            g->memTotal = igpu ? seg.shared : seg.dedicated;
            g->tempC = g->tempWarnC = g->clkMHz = g->clkMaxMHz = -1;
            KMT_ADAPTERPERFCAPS caps = { 0 };
            if (kmt_query(list[i].hAdapter, QAI_PERFCAPS, &caps, sizeof(caps)) && caps.tempWarn)
                g->tempWarnC = (int)(caps.tempWarn / 10);
            g_gh[s->ngpu] = list[i].hAdapter;
            g_gluid[s->ngpu] = list[i].luid;
            s->ngpu++;
        }
    }
}

int sysdata_open(SiSnap *s) {
    sysdata_close();
    memset(s, 0, sizeof(*s));
    s->speedMHz = s->perfPct = s->dpc = s->hardFaults = s->diskActive = -1;

    read_cpu_info(s);
    if (s->logical > SI_MAXCPU) s->nthread = SI_MAXCPU; else s->nthread = s->logical;

    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    pNtQSI = nt ? (PFN_NtQSI)GetProcAddress(nt, "NtQuerySystemInformation") : 0;
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    pPerfInfo = (PFN_PerfInfo)GetProcAddress(k32, "K32GetPerformanceInfo");

    g_pdh = LoadLibraryW(L"pdh.dll");
    if (g_pdh) {
        pOpen    = (PFN_OpenQuery)GetProcAddress(g_pdh, "PdhOpenQueryW");
        pAdd     = (PFN_AddCounter)GetProcAddress(g_pdh, "PdhAddEnglishCounterW");
        pCollect = (PFN_Collect)GetProcAddress(g_pdh, "PdhCollectQueryData");
        pValue   = (PFN_GetValue)GetProcAddress(g_pdh, "PdhGetFormattedCounterValue");
        pArray   = (PFN_GetArray)GetProcAddress(g_pdh, "PdhGetFormattedCounterArrayW");
        pCloseQ  = (PFN_CloseQuery)GetProcAddress(g_pdh, "PdhCloseQuery");
        if (pOpen && pAdd && pCollect && pValue && pArray && pCloseQ && pOpen(0, 0, &g_q) == ERROR_SUCCESS) {
            for (int k = 0; k < NK; k++)
                if (pAdd(g_q, COUNTER[k], 0, &g_k[k]) != ERROR_SUCCESS) g_k[k] = 0;   // missing on this PC: n/a
            pCollect(g_q);                       // rates need two readings: this is the first
        } else g_q = 0;
    }

    open_gpus(s);

    g_pcap = 1024 * 1024;
    g_pbuf = VirtualAlloc(0, g_pcap, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!g_pbuf) g_pcap = 0;

    g_priming = 1;                              // first readings only: every number is a difference,
    sysdata_sample(s);                          // so they show up on the next tick, a second from now
    g_priming = 0;
    s->ready = 0;
    return 1;
}

void sysdata_close(void) {
    if (g_q && pCloseQ) pCloseQ(g_q);
    g_q = 0;
    for (int k = 0; k < NK; k++) g_k[k] = 0;
    if (g_pdh) { FreeLibrary(g_pdh); g_pdh = 0; }
    if (g_arr) { HeapFree(GetProcessHeap(), 0, g_arr); g_arr = 0; g_arrCap = 0; }
    for (int i = 0; i < g_nopen; i++) if (pKClose && g_open[i]) { KMT_CLOSE c = { g_open[i] }; pKClose(&c); }
    g_nopen = 0;
    if (g_pbuf) { VirtualFree(g_pbuf, 0, MEM_RELEASE); g_pbuf = 0; g_pcap = 0; }
    g_havePrevCpu = 0; g_nprevT = 0; g_lastTick = 0;
}

// ---------------------------------------------------------------- sampling
static void sample_cpu(SiSnap *s) {
    int n = s->nthread;
    ULONG got = 0;
    if (pNtQSI && n > 0 && pNtQSI(SPI_CPUTIMES, g_cpuNow, sizeof(LMY_CPUTIMES) * n, &got) == 0) {
        long long tIdle = 0, tAll = 0;
        if (g_havePrevCpu) {
            for (int i = 0; i < n; i++) {
                long long idle = g_cpuNow[i].Idle.QuadPart - g_cpuPrev[i].Idle.QuadPart;
                long long all  = (g_cpuNow[i].Kernel.QuadPart - g_cpuPrev[i].Kernel.QuadPart) +
                                 (g_cpuNow[i].User.QuadPart - g_cpuPrev[i].User.QuadPart);   // kernel includes idle
                s->thread[i] = (unsigned char)(all > 0 ? clampi((int)(100 - idle * 100 / all), 0, 100) : 0);
                tIdle += idle; tAll += all;
            }
            s->cpuUtil = tAll > 0 ? clampi((int)(100 - tIdle * 100 / tAll), 0, 100) : 0;
        }
        for (int i = 0; i < n; i++) g_cpuPrev[i] = g_cpuNow[i];
        g_havePrevCpu = 1;
    }

    double v;
    if (pdh_value(K_UTIL, &v)) s->cpuUtil = clampi((int)(v + 0.5), 0, 100);   // matches Task Manager
    if (pdh_value(K_PERF, &v) && v > 1) {
        s->perfPct = (int)(v + 0.5);
        if (s->baseMHz) s->speedMHz = (int)(s->baseMHz * v / 100.0 + 0.5);
    }
    if (pdh_value(K_QUEUE, &v)) {               // jumpy by nature: smooth it a bit
        int q = (int)(v * 10 + 0.5);
        s->queue10 = s->ready ? (s->queue10 * 2 + q) / 3 : q;
    }
    double dpc, irq;
    if (pdh_value(K_DPC, &dpc) && pdh_value(K_INT, &irq)) s->dpc = clampi((int)(dpc + irq + 0.5), 0, 100);
    push_hist(s->cpuHist, s->cpuUtil);
}

static void sample_memory(SiSnap *s) {
    MEMORYSTATUSEX ms; ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        s->memTotal = ms.ullTotalPhys;
        s->memUsed = ms.ullTotalPhys - ms.ullAvailPhys;
    }
    LMY_PERFINFO pi; pi.cb = sizeof(pi);
    if (pPerfInfo && pPerfInfo(&pi, sizeof(pi))) {
        s->commit = (unsigned __int64)pi.CommitTotal * pi.PageSize;
        s->commitLimit = (unsigned __int64)pi.CommitLimit * pi.PageSize;
        s->procs = (int)pi.ProcessCount; s->threads = (int)pi.ThreadCount; s->handles = (int)pi.HandleCount;
    }
    double v;
    if (pdh_value(K_FAULTS, &v)) s->hardFaults = (int)(v + 0.5);
    push_hist(s->memHist, s->memTotal ? (int)(s->memUsed * 100 / s->memTotal) : 0);
    s->uptimeMs = GetTickCount64();
}

static void sample_io(SiSnap *s) {
    double v;
    if (pdh_value(K_DISKIDLE, &v)) s->diskActive = clampi(100 - (int)(v + 0.5), 0, 100);
    if (pdh_value(K_DISKR, &v)) s->diskRead = (unsigned __int64)v;
    if (pdh_value(K_DISKW, &v)) s->diskWrite = (unsigned __int64)v;
    s->netDown = (unsigned __int64)pdh_sum(K_NETD);
    s->netUp   = (unsigned __int64)pdh_sum(K_NETU);
}

// engine totals per GPU this second: "3D" = sum over every app using 3D on that GPU
typedef struct { wchar_t type[24]; double pct; } EngSum;
static EngSum g_eng[SI_MAXGPU][16];
static int    g_neng[SI_MAXGPU];

static void sample_gpus(SiSnap *s) {
    if (!s->ngpu) return;
    for (int i = 0; i < s->ngpu; i++) g_neng[i] = 0;

    DWORD n;
    if (pdh_array(K_GPUENG, &n)) {
        for (DWORD k = 0; k < n; k++) {
            PDH_FMT_COUNTERVALUE_ITEM_W *it = &g_arr[k];
            if (!item_ok(it) || !it->szName) continue;
            LUID l; if (!parse_luid(it->szName, &l)) continue;
            int gi = gpu_for_luid(s, l); if (gi < 0) continue;
            const wchar_t *t = it->szName;
            while (*t && !s_istarts(t, L"engtype_")) t++;
            if (!*t) continue;
            t += 8;
            int e = 0;
            while (e < g_neng[gi] && !s_ieq(g_eng[gi][e].type, t)) e++;
            if (e == g_neng[gi]) {
                if (e >= 16) continue;
                s_cpy(g_eng[gi][e].type, t, 24);
                g_eng[gi][e].pct = 0;
                g_neng[gi]++;
            }
            g_eng[gi][e].pct += it->FmtValue.doubleValue;
        }
    }
    for (int i = 0; i < s->ngpu; i++) {
        SiGpu *g = &s->gpu[i];
        // busiest engines first; the GPU's load is its busiest engine (what Task Manager shows)
        for (int a = 1; a < g_neng[i]; a++) {
            EngSum tmp = g_eng[i][a]; int b = a - 1;
            while (b >= 0 && g_eng[i][b].pct < tmp.pct) { g_eng[i][b + 1] = g_eng[i][b]; b--; }
            g_eng[i][b + 1] = tmp;
        }
        g->util = g_neng[i] ? clampi((int)(g_eng[i][0].pct + 0.5), 0, 100) : 0;
        g->neng = g_neng[i] < SI_MAXENG ? g_neng[i] : SI_MAXENG;
        for (int e = 0; e < g->neng; e++) {
            engine_label(g_eng[i][e].type, g->eng[e].name, 24);
            g->eng[e].pct = clampi((int)(g_eng[i][e].pct + 0.5), 0, 100);
        }
        push_hist(g->hist, g->util);
        g->memUsed = 0;
    }

    int memK = K_GPUDED;
    for (int pass = 0; pass < 2; pass++, memK = K_GPUSHR) {
        if (!pdh_array(memK, &n)) continue;
        for (DWORD k = 0; k < n; k++) {
            PDH_FMT_COUNTERVALUE_ITEM_W *it = &g_arr[k];
            if (!item_ok(it) || !it->szName) continue;
            LUID l; if (!parse_luid(it->szName, &l)) continue;
            int gi = gpu_for_luid(s, l); if (gi < 0) continue;
            if ((memK == K_GPUSHR) == (s->gpu[gi].integrated != 0))   // dedicated for real GPUs, shared for iGPUs
                s->gpu[gi].memUsed += (unsigned __int64)it->FmtValue.doubleValue;
        }
    }

    for (int i = 0; i < s->ngpu; i++) {
        SiGpu *g = &s->gpu[i];
        KMT_ADAPTERPERF ap; memset(&ap, 0, sizeof(ap));
        g->tempC = kmt_query(g_gh[i], QAI_ADAPTERPERF, &ap, sizeof(ap)) && ap.temp ? (int)(ap.temp / 10) : -1;
        KMT_NODEPERF np; memset(&np, 0, sizeof(np));          // node 0 = the 3D engine on every driver we know
        if (kmt_query(g_gh[i], QAI_NODEPERF, &np, sizeof(np)) && np.freq) {
            g->clkMHz = (int)(np.freq / 1000000);
            g->clkMaxMHz = np.maxFreq ? (int)(np.maxFreq / 1000000) : -1;
        } else g->clkMHz = g->clkMaxMHz = -1;
    }
}

static int find_prev(ULONG_PTR pid, ULONGLONG *t) {
    int lo = 0, hi = g_nprevT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (g_prevT[mid].pid == pid) { *t = g_prevT[mid].t; return 1; }
        if (g_prevT[mid].pid < pid) lo = mid + 1; else hi = mid - 1;
    }
    return 0;
}

static void sample_apps(SiSnap *s, ULONGLONG elapsedMs) {
    if (!pNtQSI || !g_pbuf) return;
    ULONG need = 0;
    LONG st = pNtQSI(SPI_PROCESSES, g_pbuf, g_pcap, &need);
    if (st == STATUS_LENGTH_MISMATCH && need < 16 * 1024 * 1024) {
        VirtualFree(g_pbuf, 0, MEM_RELEASE);
        g_pcap = need + 256 * 1024;
        g_pbuf = VirtualAlloc(0, g_pcap, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!g_pbuf) { g_pcap = 0; return; }
        st = pNtQSI(SPI_PROCESSES, g_pbuf, g_pcap, &need);
    }
    if (st != 0) return;

    ULONG_PTR self = GetCurrentProcessId();
    ULONGLONG span = elapsedMs * 10000ull * (ULONGLONG)(s->logical > 0 ? s->logical : 1);   // in 100 ns units
    g_ncurT = 0; g_ngrp = 0;
    s->selfCpu10 = 0; s->selfMem = 0;

    for (BYTE *p = g_pbuf;;) {
        LMY_PROC *pr = (LMY_PROC *)p;
        ULONG_PTR pid = (ULONG_PTR)pr->UniqueProcessId;
        ULONGLONG t = (ULONGLONG)pr->UserTime.QuadPart + (ULONGLONG)pr->KernelTime.QuadPart;
        if (pid && g_ncurT < MAXP) { g_curT[g_ncurT].pid = pid; g_curT[g_ncurT].t = t; g_ncurT++; }

        ULONGLONG prev, used = 0;
        if (pid && find_prev(pid, &prev) && t >= prev) used = t - prev;
        ULONGLONG mem = (ULONGLONG)pr->WorkingSetPrivateSize.QuadPart;

        if (pid == self) {
            s->selfCpu10 = span ? (int)(used * 1000 / span) : 0;
            s->selfMem = mem;
        }
        if (pid && pr->NameBuffer && pr->NameLength) {
            wchar_t name[40];
            int len = pr->NameLength / 2; if (len > 39) len = 39;
            for (int i = 0; i < len; i++) name[i] = pr->NameBuffer[i];
            name[len] = 0;
            if (len > 4 && s_ieq(name + len - 4, L".exe")) name[len - 4] = 0;
            int gi = 0;
            while (gi < g_ngrp && !s_ieq(g_grp[gi].name, name)) gi++;
            if (gi == g_ngrp && g_ngrp < MAXG) { s_cpy(g_grp[gi].name, name, 40); g_grp[gi].t = 0; g_grp[gi].mem = 0; g_ngrp++; }
            if (gi < g_ngrp) { g_grp[gi].t += used; g_grp[gi].mem += mem; }
        }
        if (!pr->NextEntryOffset) break;
        p += pr->NextEntryOffset;
        if (p >= g_pbuf + g_pcap) break;
    }

    // keep this sample's times (sorted by pid) for next second
    for (int gap = g_ncurT / 2; gap > 0; gap /= 2)
        for (int i = gap; i < g_ncurT; i++) {
            PrevT tmp = g_curT[i]; int j = i;
            while (j >= gap && g_curT[j - gap].pid > tmp.pid) { g_curT[j] = g_curT[j - gap]; j -= gap; }
            g_curT[j] = tmp;
        }
    for (int i = 0; i < g_ncurT; i++) g_prevT[i] = g_curT[i];
    g_nprevT = g_ncurT;

    // top apps: most CPU, then most memory
    s->ntop = 0;
    for (int k = 0; k < SI_TOP; k++) {
        int best = -1;
        for (int i = 0; i < g_ngrp; i++) {
            if (g_grp[i].t == (ULONGLONG)-1) continue;
            if (best < 0 || g_grp[i].t > g_grp[best].t || (g_grp[i].t == g_grp[best].t && g_grp[i].mem > g_grp[best].mem)) best = i;
        }
        if (best < 0) break;
        SiProc *o = &s->top[s->ntop++];
        s_cpy(o->name, g_grp[best].name, 40);
        o->cpu10 = span ? clampi((int)(g_grp[best].t * 1000 / span), 0, 1000) : 0;
        o->mem = g_grp[best].mem;
        g_grp[best].t = (ULONGLONG)-1;          // taken
    }
}

void sysdata_sample(SiSnap *s) {
    ULONGLONG now = GetTickCount64();
    ULONGLONG elapsed = g_lastTick && now > g_lastTick ? now - g_lastTick : 1000;
    g_lastTick = now;
    if (g_q && !g_priming) pCollect(g_q);       // (open already took the first PDH reading)
    sample_cpu(s);
    sample_memory(s);
    sample_io(s);
    sample_gpus(s);
    sample_apps(s, elapsed);
    s->ready = 1;
}
