// extwin.c - external packages, the Windows half (2.24). extpkg.c reads the release and picks the file; this
// downloads it on a background thread (Loomy stays usable), checks it, unpacks it and runs it.
//
// Where: %LOCALAPPDATA%\Loomy\Packages\<id>\  (local, not roaming: programs don't belong in a synced profile)
//   current\          the unpacked release (swapped in whole only after everything checked out)
//   installed.txt     tag, file name, the exe's path inside current\  (3 lines)
// The file he chose (E in /pkg) is %APPDATA%\Loomy\pkg-<id>.txt, and in Ellune ("pkg.<id>.file.<arch>") when this
// PC has its Ellune key, so his other PCs pick the same file without asking.
//
// Checks before anything is unpacked: the release is the catalog's repo, the file comes from that repo's own
// release URL, GitHub's sha256 for it matches what arrived, and the size is what GitHub said. Unpacking uses
// Windows' own tar.exe (Windows 10 1803+), which refuses paths that climb out of the folder.
#include "loomy.h"
#include <shellapi.h>

enum { PK_DONE = 1, PK_FAIL, PK_PICK, PK_RUNFAIL, PK_SAME };

typedef struct {
    volatile LONG busy, bytes;
    unsigned long long size;
    int quiet, ellune;
    wchar_t status[240];          // the line /pkg shows under the list
    wchar_t choices[400];         // the files on the latest release, when it couldn't pick
    wchar_t msg[240];             // for WM_PKG
    wchar_t tag[48];
} Job;
static Job g_job[NFEATURES];

static Job *job(int f) { return f > FEAT_CORE && f < NFEATURES ? &g_job[f] : 0; }
int ext_busy(int f) { Job *j = job(f); return j && j->busy; }
int ext_pct(int f) {
    Job *j = job(f);
    if (!j || !j->busy || !j->size) return -1;
    unsigned long long b = (unsigned long long)j->bytes;
    return (int)(b * 100 / j->size > 100 ? 100 : b * 100 / j->size);
}
const wchar_t *ext_status(int f) { Job *j = job(f); return j ? j->status : L""; }
void ext_choices(int f, wchar_t *b, int cap) { Job *j = job(f); s_cpy(b, j ? j->choices : L"", cap); }

int ext_arch(void) {
    typedef BOOL (WINAPI *Wow2)(HANDLE, USHORT *, USHORT *);
    Wow2 w = (Wow2)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2");
    USHORT proc = 0, native = 0;
    if (w && w(GetCurrentProcess(), &proc, &native)) {
        if (native == 0xAA64) return EXT_ARM64;
        if (native == 0x8664) return EXT_X64;
        if (native == 0x014C) return EXT_X86;
    }
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    if (si.wProcessorArchitecture == 12) return EXT_ARM64;            // PROCESSOR_ARCHITECTURE_ARM64
    if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL) return EXT_X86;
    return EXT_X64;
}

// ---------------------------------------------------------------- paths
static void base_dir(int f, wchar_t *out, int cap) {
    wchar_t la[MAX_PATH];
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH)) s_cpy(la, cfg_dir(), MAX_PATH);
    s_cpy(out, la, cap);
    s_cat(out, L"\\Loomy", cap);        CreateDirectoryW(out, 0);
    s_cat(out, L"\\Packages", cap);     CreateDirectoryW(out, 0);
    s_cat(out, L"\\", cap);
    s_cat(out, feature_id(f), cap);
}
static void sub(int f, const wchar_t *name, wchar_t *out, int cap) { base_dir(f, out, cap); s_cat(out, L"\\", cap); s_cat(out, name, cap); }

static void choice_file(int f, wchar_t *out, int cap) {
    s_cpy(out, cfg_dir(), cap);
    s_cat(out, L"\\pkg-", cap);
    s_cat(out, feature_id(f), cap);
    s_cat(out, L".txt", cap);
}

static int delete_tree(const wchar_t *dir) {
    if (GetFileAttributesW(dir) == INVALID_FILE_ATTRIBUTES) return 1;
    wchar_t from[MAX_PATH + 2];
    s_cpy(from, dir, MAX_PATH);
    from[s_len(from) + 1] = 0;                                         // double-null ended
    SHFILEOPSTRUCTW op;
    memset(&op, 0, sizeof op);
    op.wFunc = FO_DELETE;
    op.pFrom = from;
    op.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;      // no Recycle Bin: these are Loomy's copies
    return SHFileOperationW(&op) == 0 && GetFileAttributesW(dir) == INVALID_FILE_ATTRIBUTES;
}

// small UTF-8 text files
static int read_text(const wchar_t *path, char *buf, int cap) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD n = 0;
    ReadFile(h, buf, (DWORD)cap - 1, &n, 0);
    CloseHandle(h);
    buf[n] = 0;
    return 1;
}
static int write_text(const wchar_t *path, const char *t) {
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int n = 0; while (t[n]) n++;
    DWORD w = 0;
    WriteFile(h, t, (DWORD)n, &w, 0);
    CloseHandle(h);
    return w == (DWORD)n;
}
static void a2w(const char *a, wchar_t *w, int cap) { if (!MultiByteToWideChar(CP_UTF8, 0, a, -1, w, cap)) w[0] = 0; }
static void w2a(const wchar_t *w, char *a, int cap) { if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, a, cap, 0, 0)) a[0] = 0; }

// ---------------------------------------------------------------- the chosen file (E)
static int pattern_ok(const wchar_t *p) {         // a file name, maybe with *: nothing that could break JSON or a path
    for (int i = 0; p[i]; i++) {
        wchar_t c = p[i];
        if (!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') ||
              c == L'.' || c == L'-' || c == L'_' || c == L'*' || c == L'+' || c == L' ' || c == L'(' || c == L')')) return 0;
    }
    return s_len(p) < 96;
}

int ext_get_file(int f, wchar_t *out, int cap) {
    wchar_t path[MAX_PATH];
    char t[200];
    choice_file(f, path, MAX_PATH);
    out[0] = 0;
    if (!read_text(path, t, sizeof t)) return 0;
    for (int i = 0; t[i]; i++) if (t[i] == '\r' || t[i] == '\n') { t[i] = 0; break; }
    a2w(t, out, cap);
    return out[0] != 0;
}

static DWORD WINAPI pref_push(LPVOID p) {           // tell Ellune, off the UI thread (a few seconds at most)
    char *kv = (char *)p;
    char *val = kv; while (*val) val++; val++;
    net_pref_set(kv, val);
    HeapFree(GetProcessHeap(), 0, kv);
    return 0;
}

static void pref_key(int f, char *out) {
    char id[24];
    w2a(feature_id(f), id, 24);
    wsprintfA(out, "pkg.%s.file.%s", id, ext_arch_name(ext_arch()));
}

void ext_set_file(int f, const wchar_t *pattern) {
    wchar_t path[MAX_PATH];
    choice_file(f, path, MAX_PATH);
    if (!pattern_ok(pattern)) { out(OUT_ERR, L"That doesn't look like a file name (letters, digits, - _ . * only)."); return; }
    char a[200];
    w2a(pattern, a, sizeof a);
    if (a[0]) write_text(path, a); else DeleteFileW(path);
    Job *j = job(f);
    if (j) { j->status[0] = 0; j->choices[0] = 0; }
    if (net_pref_ready()) {                          // his other PCs get it too
        char *kv = (char *)HeapAlloc(GetProcessHeap(), 0, 400);
        if (kv) {
            pref_key(f, kv);
            int n = 0; while (kv[n]) n++;
            int m = 0; while (a[m] && n + 1 + m < 398) { kv[n + 1 + m] = a[m]; m++; }
            kv[n + 1 + m] = 0;
            HANDLE t = CreateThread(0, 0, pref_push, kv, 0, 0);
            if (t) CloseHandle(t); else HeapFree(GetProcessHeap(), 0, kv);
        }
    }
}

// ---------------------------------------------------------------- installed.txt
int ext_installed(int f, wchar_t *tag, int tcap, wchar_t *file, int fcap) {
    wchar_t p[MAX_PATH];
    char t[600];
    sub(f, L"installed.txt", p, MAX_PATH);
    if (!read_text(p, t, sizeof t)) return 0;
    char *l[3] = { t, 0, 0 };
    for (int i = 0, k = 1; t[i] && k < 3; i++) if (t[i] == '\n') { t[i] = 0; if (i && t[i - 1] == '\r') t[i - 1] = 0; l[k++] = t + i + 1; }
    if (tag) a2w(l[0], tag, tcap);
    if (file) { if (l[1]) a2w(l[1], file, fcap); else file[0] = 0; }
    return l[0][0] != 0;
}

static int installed_exe(int f, wchar_t *exe, int cap) {
    wchar_t p[MAX_PATH], rel[MAX_PATH];
    char t[600];
    sub(f, L"installed.txt", p, MAX_PATH);
    if (!read_text(p, t, sizeof t)) return 0;
    int line = 0; char *r = 0;
    for (int i = 0; t[i]; i++) if (t[i] == '\n') { t[i] = 0; if (i && t[i - 1] == '\r') t[i - 1] = 0; if (++line == 2) r = t + i + 1; }
    if (!r || !*r) return 0;
    for (int i = 0; r[i]; i++) if (r[i] == '\r' || r[i] == '\n') r[i] = 0;
    a2w(r, rel, MAX_PATH);
    if (s_has(rel, L"..") || rel[0] == L'\\' || s_has(rel, L":")) return 0;
    sub(f, L"current\\", exe, cap);
    s_cat(exe, rel, cap);
    return GetFileAttributesW(exe) != INVALID_FILE_ATTRIBUTES;
}

// the exe somewhere under dir (3 folders deep at most); rel gets its path from dir
static int find_exe(const wchar_t *dir, const wchar_t *name, wchar_t *rel, int cap, int depth) {
    wchar_t q[MAX_PATH];
    s_cpy(q, dir, MAX_PATH); s_cat(q, L"\\*", MAX_PATH);
    WIN32_FIND_DATAW d;
    HANDLE h = FindFirstFileW(q, &d);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int found = 0;
    do {
        if (d.cFileName[0] == L'.') continue;
        if (!(d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && s_ieq(d.cFileName, name)) { s_cpy(rel, d.cFileName, cap); found = 1; }
    } while (!found && FindNextFileW(h, &d));
    FindClose(h);
    if (found || depth >= 3) return found;
    h = FindFirstFileW(q, &d);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (d.cFileName[0] == L'.' || !(d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        wchar_t sd[MAX_PATH], r2[MAX_PATH];
        s_cpy(sd, dir, MAX_PATH); s_cat(sd, L"\\", MAX_PATH); s_cat(sd, d.cFileName, MAX_PATH);
        if (find_exe(sd, name, r2, MAX_PATH, depth + 1)) {
            s_cpy(rel, d.cFileName, cap); s_cat(rel, L"\\", cap); s_cat(rel, r2, cap);
            found = 1;
        }
    } while (!found && FindNextFileW(h, &d));
    FindClose(h);
    return found;
}

// ---------------------------------------------------------------- the install thread
static void post(int f, int kind, const wchar_t *msg) {
    Job *j = job(f);
    s_cpy(j->msg, msg, 240);
    if (!PostMessageW(ui_hwnd(), WM_PKG, (WPARAM)kind, (LPARAM)f)) InterlockedExchange(&j->busy, 0);
}

static int hex_eq(const unsigned char h[32], const char *hex) {
    static const char X[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) if (hex[2 * i] != X[h[i] >> 4] || hex[2 * i + 1] != X[h[i] & 15]) return 0;
    return 1;
}

static int sha256_file(const wchar_t *path, unsigned long long size, const char *hex) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if (h == INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || (unsigned long long)sz.QuadPart != size) { CloseHandle(h); return 0; }
    unsigned char *buf = (unsigned char *)VirtualAlloc(0, (SIZE_T)size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!buf) { CloseHandle(h); return 0; }
    unsigned long long got = 0;
    while (got < size) {
        DWORD n = 0, want = size - got > (1u << 24) ? (1u << 24) : (DWORD)(size - got);
        if (!ReadFile(h, buf + got, want, &n, 0) || !n) break;
        got += n;
    }
    CloseHandle(h);
    unsigned char d[32];
    int ok = got == size;
    if (ok) { el_sha256(buf, (int)size, d); ok = hex_eq(d, hex); }
    VirtualFree(buf, 0, MEM_RELEASE);
    return ok;
}

static int untar(const wchar_t *zip, const wchar_t *dir) {
    wchar_t tar[MAX_PATH], cmd[MAX_PATH * 3];
    GetSystemDirectoryW(tar, MAX_PATH);
    s_cat(tar, L"\\tar.exe", MAX_PATH);
    if (GetFileAttributesW(tar) == INVALID_FILE_ATTRIBUTES) return -1;
    s_cpy(cmd, L"\"", MAX_PATH * 3); s_cat(cmd, tar, MAX_PATH * 3); s_cat(cmd, L"\" -x -f \"", MAX_PATH * 3);
    s_cat(cmd, zip, MAX_PATH * 3); s_cat(cmd, L"\" -C \"", MAX_PATH * 3); s_cat(cmd, dir, MAX_PATH * 3); s_cat(cmd, L"\"", MAX_PATH * 3);
    STARTUPINFOW si; PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si); si.cb = sizeof si;
    if (!CreateProcessW(tar, cmd, 0, 0, FALSE, CREATE_NO_WINDOW, 0, dir, &si, &pi)) return 0;
    DWORD code = 1;
    if (WaitForSingleObject(pi.hProcess, 180000) == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &code);
    else TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return code == 0;
}

static DWORD WINAPI install_thread(LPVOID p) {
    int f = (int)(LONG_PTR)p;
    Job *j = job(f);
    const ExtPkg *pk = ext_pkg(f);
    wchar_t m[240], wn[100], wtag[48];
    static ExtRelease rel;                       // one install at a time per package; ~16 KB, kept off the stack
    char *json = (char *)VirtualAlloc(0, 512 * 1024, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!json) { post(f, PK_FAIL, L"Out of memory."); return 0; }

    // 1. the latest release
    wchar_t api[160], o[60], r[60];
    a2w(pk->owner, o, 60); a2w(pk->repo, r, 60);
    wsprintfW(api, L"/repos/%s/%s/releases/latest", o, r);
    DWORD got = 0;
    if (!net_get(L"api.github.com", api, L"application/vnd.github+json", json, 512 * 1024, &got, 0, 0) || !got) {
        VirtualFree(json, 0, MEM_RELEASE); post(f, PK_FAIL, L"Couldn't reach GitHub. Check the internet and try again."); return 0;
    }
    int parsed = ext_parse_release(json, &rel);
    VirtualFree(json, 0, MEM_RELEASE);
    if (!parsed) { post(f, PK_FAIL, L"GitHub's answer about the release didn't make sense."); return 0; }
    a2w(rel.tag, wtag, 48);
    s_cpy(j->tag, wtag, 48);

    // 2. which file: his choice here, else his choice from another PC (Ellune), else automatic
    wchar_t wpat[100];
    char pat[100];
    pat[0] = 0;
    if (ext_get_file(f, wpat, 100)) w2a(wpat, pat, sizeof pat);
    else if (j->ellune) {
        char key[60];
        pref_key(f, key);
        if (net_pref_get(key, pat, sizeof pat)) {
            a2w(pat, wpat, 100);
            if (pattern_ok(wpat)) { char a2[100]; w2a(wpat, a2, 100); wchar_t cf[MAX_PATH]; choice_file(f, cf, MAX_PATH); write_text(cf, a2); }
            else pat[0] = 0;
        }
    }
    char cp[100];
    if (pat[0]) ext_pattern(pat, rel.tag, cp, sizeof cp); else cp[0] = 0;
    int idx = -1, pick = ext_pick(&rel, cp, ext_arch(), &idx);
    if (pick != PICK_OK) {
        j->choices[0] = 0;
        for (int i = 0; i < rel.n; i++) {
            a2w(rel.a[i].name, wn, 100);
            if (s_len(j->choices) + s_len(wn) + 4 >= 400) break;
            if (j->choices[0]) s_cat(j->choices, L", ", 400);
            s_cat(j->choices, wn, 400);
        }
        if (pick == PICK_NOMATCH) wsprintfW(m, L"\x201C%s\x201D isn't on %s. Press E to choose another file.", wpat, wtag);
        else if (pick == PICK_MANY) wsprintfW(m, L"More than one file on %s fits this PC. Press E and type which one.", wtag);
        else wsprintfW(m, L"No Windows file found on %s. Press E and type its name.", wtag);
        post(f, PK_PICK, m);
        return 0;
    }
    ExtAsset *a = &rel.a[idx];
    a2w(a->name, wn, 100);

    // 3. already this one?
    wchar_t itag[48], ifile[100], exe[MAX_PATH];
    if (ext_installed(f, itag, 48, ifile, 100) && s_ieq(itag, wtag) && s_ieq(ifile, wn) && installed_exe(f, exe, MAX_PATH)) {
        wsprintfW(m, L"%s is already the newest (%s).", feature_name(f), wtag);
        post(f, PK_SAME, m);
        return 0;
    }
    if (!ext_asset_ok(a, pk, rel.tag)) {
        wsprintfW(m, L"%s on %s can't be checked (GitHub gave no checksum, or it's not from %s), so it wasn't downloaded.", wn, wtag, pk->site);
        post(f, PK_FAIL, m);
        return 0;
    }

    // 4. download it, checking the size and the sha256
    wchar_t zip[MAX_PATH], dl[400];
    base_dir(f, zip, MAX_PATH); CreateDirectoryW(zip, 0);
    sub(f, L"download.zip", zip, MAX_PATH);
    a2w(a->url + 18, dl, 400);                       // "https://github.com" is 18 characters
    j->size = a->size;
    InterlockedExchange(&j->bytes, 0);
    HANDLE h = CreateFileW(zip, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    if (h == INVALID_HANDLE_VALUE) { post(f, PK_FAIL, L"Couldn't save the download (no space, or no access to AppData?)."); return 0; }
    int ok = net_get(L"github.com", dl, L"application/octet-stream", 0, 0, 0, h, &j->bytes);
    CloseHandle(h);
    if (!ok) { DeleteFileW(zip); post(f, PK_FAIL, L"The download failed. Check the internet and try again."); return 0; }
    if (!sha256_file(zip, a->size, a->digest + 7)) {
        DeleteFileW(zip);
        post(f, PK_FAIL, L"The download doesn't match GitHub's checksum, so it was thrown away.");
        return 0;
    }

    // 5. unpack into new, find the exe, then swap new in for current
    wchar_t nd[MAX_PATH], cd[MAX_PATH], rel_exe[MAX_PATH];
    sub(f, L"new", nd, MAX_PATH);
    sub(f, L"current", cd, MAX_PATH);
    delete_tree(nd);
    CreateDirectoryW(nd, 0);
    int u = untar(zip, nd);
    DeleteFileW(zip);
    if (u <= 0) {
        delete_tree(nd);
        post(f, PK_FAIL, u < 0 ? L"Windows' tar.exe is missing (it comes with Windows 10 1803 and newer), so the zip can't be unpacked."
                               : L"The zip couldn't be unpacked.");
        return 0;
    }
    if (!find_exe(nd, pk->exe, rel_exe, MAX_PATH, 0)) {
        delete_tree(nd);
        wsprintfW(m, L"%s has no %s in it. Wrong file? Press E to choose another.", wn, pk->exe);
        post(f, PK_FAIL, m);
        return 0;
    }
    if (!delete_tree(cd)) {
        delete_tree(nd);
        wsprintfW(m, L"The old %s is still open. Close it and try again.", pk->exe);
        post(f, PK_FAIL, m);
        return 0;
    }
    if (!MoveFileW(nd, cd)) { delete_tree(nd); post(f, PK_FAIL, L"Couldn't move the new files in place."); return 0; }
    char line[700], re[MAX_PATH * 2];
    w2a(rel_exe, re, sizeof re);
    wsprintfA(line, "%s\r\n%s\r\n%s\r\n", rel.tag, a->name, re);
    wchar_t inst[MAX_PATH];
    sub(f, L"installed.txt", inst, MAX_PATH);
    write_text(inst, line);
    wsprintfW(m, L"%s %s is ready (%s).", feature_name(f), wtag, wn);
    post(f, PK_DONE, m);
    return 0;
}

void ext_install(int f, int quiet) {
    Job *j = job(f);
    if (!j || !ext_pkg(f) || InterlockedCompareExchange(&j->busy, 1, 0)) return;
    j->quiet = quiet;
    j->ellune = net_pref_ready();                // loads the key here, on the UI thread
    j->status[0] = 0;
    j->size = 0;
    if (!quiet) outf(OUT_DIM, L"Getting %s from %s...", feature_name(f), ext_pkg(f)->site);
    HANDLE t = CreateThread(0, 0, install_thread, (LPVOID)(LONG_PTR)f, 0, 0);
    if (t) CloseHandle(t);
    else { InterlockedExchange(&j->busy, 0); out(OUT_ERR, L"Couldn't start the download."); }
    ui_redraw();
    SetTimer(ui_hwnd(), 0x504B, 400, 0);         // redraws the progress while it downloads (ui.c repaints on timers it has no job for)
}

void ext_event(WPARAM kind, LPARAM fl) {
    int f = (int)fl;
    Job *j = job(f);
    if (!j) return;
    InterlockedExchange(&j->busy, 0);
    int any = 0;
    for (int i = 0; i < NFEATURES; i++) if (g_job[i].busy) any = 1;
    if (!any) KillTimer(ui_hwnd(), 0x504B);
    switch ((int)kind) {
    case PK_DONE:
    case PK_SAME:
        j->status[0] = 0; j->choices[0] = 0;
        if (!feature_on(f)) feature_set(f, 1);
        out(OUT_OK, j->msg);
        if (kind == PK_DONE && f == FEAT_SCRCPY)
            out(OUT_DIM, L"Plug your phone in by USB (USB debugging on), then type scrcpy, or say \x201Cmirror my phone\x201D.");
        break;
    case PK_PICK:
        s_cpy(j->status, j->msg, 240);
        out(OUT_ERR, j->msg);
        break;
    case PK_RUNFAIL:
        out(OUT_ERR, j->msg);
        break;
    default:
        s_cpy(j->status, j->msg, 240);
        out(OUT_ERR, j->msg);
        break;
    }
    ui_redraw();
}

void ext_remove(int f) {
    wchar_t b[MAX_PATH], cf[MAX_PATH];
    base_dir(f, b, MAX_PATH);
    Job *j = job(f);
    if (j) { j->status[0] = 0; j->choices[0] = 0; }
    choice_file(f, cf, MAX_PATH);                // his file choice stays: it's a choice, not a download
    if (!delete_tree(b)) out(OUT_DIM, L"Some of its files are still open (is it running?). Close it and turn it off again to delete them.");
}

// ---------------------------------------------------------------- running it
typedef struct { HANDLE proc, rd; int f; } Run;

// scrcpy prints why it stops ("ERROR: Could not find any ADB device"); this keeps the last of it, and says so if
// it closes within 8 s. Reads with PeekNamedPipe, so a helper that inherits the pipe (adb) can't hang it.
static DWORD WINAPI watch_thread(LPVOID p) {
    Run *r = (Run *)p;
    char tail[1024];
    int n = 0;
    DWORD start = GetTickCount(), code = STILL_ACTIVE;
    for (;;) {
        DWORD avail = 0, got = 0;
        char buf[512];
        while (PeekNamedPipe(r->rd, 0, 0, 0, &avail, 0) && avail) {
            if (!ReadFile(r->rd, buf, avail > sizeof buf ? sizeof buf : avail, &got, 0) || !got) break;
            for (DWORD i = 0; i < got; i++) {
                if (n == sizeof tail - 1) { for (int k = 0; k < n - 512; k++) tail[k] = tail[k + 512]; n -= 512; }
                tail[n++] = buf[i];
            }
        }
        if (WaitForSingleObject(r->proc, 300) == WAIT_OBJECT_0) {
            if (PeekNamedPipe(r->rd, 0, 0, 0, &avail, 0) && avail) continue;   // read what's left first
            GetExitCodeProcess(r->proc, &code);
            break;
        }
    }
    tail[n] = 0;
    if (code != 0 && GetTickCount() - start < 8000) {
        // the last "ERROR: ..." line, or the last line
        char *last = 0;
        for (char *q = tail; *q; q++) if (q[0] == 'E' && q[1] == 'R' && q[2] == 'R' && q[3] == 'O' && q[4] == 'R') last = q;
        if (!last) { last = tail + n; while (last > tail && (last[-1] == '\n' || last[-1] == '\r')) last--; while (last > tail && last[-1] != '\n') last--; }
        char line[200];
        int k = 0;
        while (last[k] && last[k] != '\r' && last[k] != '\n' && k < 199) { line[k] = last[k]; k++; }
        line[k] = 0;
        wchar_t wl[200], m[240];
        a2w(line, wl, 200);
        if (s_has(wl, L"ADB device") || s_has(wl, L"adb devices"))
            wsprintfW(m, L"No phone found. Plug it in by USB, turn on USB debugging, and allow this PC on the phone.");
        else wsprintfW(m, L"%s closed right away: %s", feature_name(r->f), wl[0] ? wl : L"(it didn't say why)");
        post(r->f, PK_RUNFAIL, m);
    }
    CloseHandle(r->rd);
    CloseHandle(r->proc);
    HeapFree(GetProcessHeap(), 0, r);
    return 0;
}

void ext_run(int f) {
    wchar_t exe[MAX_PATH], dir[MAX_PATH];
    if (!installed_exe(f, exe, MAX_PATH)) {
        outf(OUT_ERR, L"%s's files are missing.", feature_name(f));
        outf(OUT_DIM, L"/pkg update %s downloads them again.", feature_id(f));
        return;
    }
    s_cpy(dir, exe, MAX_PATH);
    int k = s_len(dir);
    while (k > 0 && dir[k - 1] != L'\\') k--;
    dir[k ? k - 1 : 0] = 0;
    SECURITY_ATTRIBUTES sa = { sizeof sa, 0, TRUE };
    HANDLE rd = 0, wr = 0;
    if (!CreatePipe(&rd, &wr, &sa, 64 * 1024)) rd = wr = 0;
    if (rd) SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si; PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si); si.cb = sizeof si;
    if (wr) { si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = wr; si.hStdError = wr; si.hStdInput = 0; }
    wchar_t cmd[MAX_PATH + 4];
    s_cpy(cmd, L"\"", MAX_PATH + 4); s_cat(cmd, exe, MAX_PATH + 4); s_cat(cmd, L"\"", MAX_PATH + 4);
    BOOL ok = CreateProcessW(exe, cmd, 0, 0, wr != 0, CREATE_NO_WINDOW, 0, dir, &si, &pi);
    if (wr) CloseHandle(wr);
    if (!ok) {
        if (rd) CloseHandle(rd);
        outf(OUT_ERR, L"Couldn't start %s (error %u). Blocked on this PC?", feature_name(f), (unsigned)GetLastError());
        return;
    }
    CloseHandle(pi.hThread);
    outf(OUT_OK, L"Starting %s: your phone's screen opens in its own window.", feature_name(f));
    Run *r = rd ? (Run *)HeapAlloc(GetProcessHeap(), 0, sizeof(Run)) : 0;
    if (r) {
        r->proc = pi.hProcess; r->rd = rd; r->f = f;
        HANDLE t = CreateThread(0, 0, watch_thread, r, 0, 0);
        if (t) CloseHandle(t); else { CloseHandle(rd); CloseHandle(pi.hProcess); HeapFree(GetProcessHeap(), 0, r); }
    } else { if (rd) CloseHandle(rd); CloseHandle(pi.hProcess); }
    ui_hide();                                   // out of the way of the phone's window
}
