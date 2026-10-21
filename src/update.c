// update.c - the `update` command. Checks your GitHub repo for a newer release,
// downloads the new loomy.exe, and swaps itself out.
//
// How the swap works: a running .exe is locked, so Loomy can't overwrite itself directly.
// Instead it downloads the new build next to itself as loomy.new.exe, then writes a tiny
// .bat that waits for Loomy to close, replaces the old exe, and relaunches. Loomy kicks
// off that .bat and exits. Standard self-update dance.
#include "loomy.h"
#include <winhttp.h>

// ---- where your builds live. CHANGE "YOUR_USERNAME" to your GitHub username. ----
// Repo name here must match what you create on GitHub (we're using Project-Loomy).
#define GH_USER L"JimmyUns"
#define GH_REPO L"Project-Loomy"

#define GH_API_HOST L"api.github.com"
#define GH_API_PATH L"/repos/" GH_USER L"/" GH_REPO L"/releases/latest"
#define GH_DL_HOST  L"github.com"
#define GH_DL_PATH  L"/" GH_USER L"/" GH_REPO L"/releases/latest/download/loomy.exe"

// Fetch host+path over HTTPS. Writes up to cap bytes into buf, sets *got. Follows redirects.
// If toFile is set, writes the body to that file instead and buf/cap are ignored.
static int https_get(const wchar_t *host, const wchar_t *path, const wchar_t *accept,
                     char *buf, DWORD cap, DWORD *got, HANDLE toFile) {
    int ok = 0;
    if (got) *got = 0;

    HINTERNET s = WinHttpOpen(L"Loomy", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, 0, 0, 0);
    if (!s) return 0;
    HINTERNET c = WinHttpConnect(s, host, 443, 0);
    if (!c) { WinHttpCloseHandle(s); return 0; }

    const wchar_t *accepts[] = { accept ? accept : L"*/*", 0 };
    HINTERNET r = WinHttpOpenRequest(c, L"GET", path, 0, 0, accepts, WINHTTP_FLAG_SECURE);
    if (!r) { WinHttpCloseHandle(c); WinHttpCloseHandle(s); return 0; }

    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(r, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));

    // GitHub's API requires a User-Agent; WinHttpOpen already set one ("Loomy").
    if (!WinHttpSendRequest(r, 0, 0, 0, 0, 0, 0) || !WinHttpReceiveResponse(r, 0)) {
        WinHttpCloseHandle(r); WinHttpCloseHandle(c); WinHttpCloseHandle(s); return 0;
    }

    // check HTTP status is 2xx
    DWORD code = 0, len = sizeof(code);
    if (WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, 0, &code, &len, 0)) {
        if (code < 200 || code >= 300) { WinHttpCloseHandle(r); WinHttpCloseHandle(c); WinHttpCloseHandle(s); return 0; }
    }

    DWORD total = 0;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(r, &avail) || avail == 0) { ok = 1; break; }
        char chunk[8192];
        while (avail > 0) {
            DWORD want = avail > sizeof(chunk) ? sizeof(chunk) : avail;
            DWORD read = 0;
            if (!WinHttpReadData(r, chunk, want, &read) || read == 0) { avail = 0; break; }
            if (toFile) {
                DWORD w; WriteFile(toFile, chunk, read, &w, 0);
            } else if (buf) {
                for (DWORD i = 0; i < read && total < cap - 1; i++) buf[total++] = chunk[i];
            }
            avail -= read;
        }
    }
    if (buf) buf[total] = 0;
    if (got) *got = total;

    WinHttpCloseHandle(r); WinHttpCloseHandle(c); WinHttpCloseHandle(s);
    return ok;
}

// pull "tag_name":"v2.5" out of the API JSON -> "2.5" (leading v stripped)
static int parse_tag(const char *json, wchar_t *out, int cap) {
    const char *key = "\"tag_name\"";
    const char *p = json;
    while (*p) {
        const char *k = key; const char *q = p;
        while (*k && *q == *k) { q++; k++; }
        if (!*k) { p = q; break; }
        p++;
    }
    if (!*p) return 0;
    while (*p && *p != ':') p++;
    if (*p) p++;
    while (*p == ' ' || *p == '"') p++;
    if (*p == 'v' || *p == 'V') p++;
    int n = 0;
    while (*p && *p != '"' && n < cap - 1) out[n++] = (wchar_t)*p++;
    out[n] = 0;
    return n > 0;
}

// compare dotted versions like "2.10" vs "2.4": returns 1 if a > b
static int ver_gt(const wchar_t *a, const wchar_t *b) {
    while (*a || *b) {
        int na = 0, nb = 0;
        while (*a >= L'0' && *a <= L'9') na = na * 10 + (*a++ - L'0');
        while (*b >= L'0' && *b <= L'9') nb = nb * 10 + (*b++ - L'0');
        if (na != nb) return na > nb;
        if (*a == L'.') a++;
        if (*b == L'.') b++;
        if (!*a && !*b) break;
    }
    return 0;
}

static char g_json[64 * 1024];

void cmd_update(wchar_t *args) {
    if (s_ieq(GH_USER, L"YOUR_USERNAME")) {
        out(OUT_ERR, L"Update isn't wired up yet.");
        out(OUT_DIM, L"Set your GitHub username in src/update.c (GH_USER) and rebuild.");
        return;
    }

    out(OUT_DIM, L"Checking for updates...");
    ui_redraw();

    DWORD got = 0;
    if (!https_get(GH_API_HOST, GH_API_PATH, L"application/vnd.github+json", g_json, sizeof(g_json), &got, 0) || got == 0) {
        out(OUT_ERR, L"Couldn't reach GitHub. Check your connection and try again.");
        return;
    }

    wchar_t latest[32];
    if (!parse_tag(g_json, latest, 32)) {
        out(OUT_ERR, L"No releases found yet on GitHub.");
        out(OUT_DIM, L"Once the first build is published, update will work.");
        return;
    }

    if (!ver_gt(latest, LOOMY_VERSION)) {
        outf(OUT_OK, L"You're up to date (v%s).", LOOMY_VERSION);
        return;
    }

    outf(OUT_ACCENT, L"New version available: v%s (you have v%s)", latest, LOOMY_VERSION);
    out(OUT_DIM, L"Downloading...");
    ui_redraw();

    // download next to the current exe as loomy.new.exe
    wchar_t exe[MAX_PATH], newExe[MAX_PATH], dir[MAX_PATH], bat[MAX_PATH];
    GetModuleFileNameW(0, exe, MAX_PATH);
    s_cpy(dir, exe, MAX_PATH);
    int slash = s_len(dir);
    while (slash > 0 && dir[slash - 1] != L'\\') slash--;
    dir[slash] = 0;
    s_cpy(newExe, dir, MAX_PATH); s_cat(newExe, L"loomy.new.exe", MAX_PATH);
    s_cpy(bat, dir, MAX_PATH);    s_cat(bat, L"loomy.update.bat", MAX_PATH);

    HANDLE f = CreateFileW(newExe, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    if (f == INVALID_HANDLE_VALUE) {
        out(OUT_ERR, L"Couldn't save the download (no write access here?).");
        return;
    }
    int dok = https_get(GH_DL_HOST, GH_DL_PATH, L"application/octet-stream", 0, 0, 0, f);
    CloseHandle(f);

    // sanity check: a real exe starts with "MZ" and is more than a few KB
    int valid = 0;
    HANDLE vf = CreateFileW(newExe, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if (vf != INVALID_HANDLE_VALUE) {
        char hdr[2] = {0}; DWORD rd = 0;
        ReadFile(vf, hdr, 2, &rd, 0);
        DWORD size = GetFileSize(vf, 0);
        CloseHandle(vf);
        valid = dok && rd == 2 && hdr[0] == 'M' && hdr[1] == 'Z' && size > 8192;
    }
    if (!valid) {
        DeleteFileW(newExe);
        out(OUT_ERR, L"Download looked wrong, so I didn't touch your current version.");
        return;
    }

    // write the swap script: wait for us to close, replace, relaunch, delete self
    // (ASCII .bat; keep it simple and robust)
    char script[1024];
    char exeA[MAX_PATH], newA[MAX_PATH], batA[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, exe, -1, exeA, MAX_PATH, 0, 0);
    WideCharToMultiByte(CP_ACP, 0, newExe, -1, newA, MAX_PATH, 0, 0);
    WideCharToMultiByte(CP_ACP, 0, bat, -1, batA, MAX_PATH, 0, 0);
    int n = wsprintfA(script,
        "@echo off\r\n"
        "ping 127.0.0.1 -n 2 >nul\r\n"            /* ~1s pause for Loomy to exit */
        ":retry\r\n"
        "del \"%s\" >nul 2>&1\r\n"
        "if exist \"%s\" ( ping 127.0.0.1 -n 2 >nul & goto retry )\r\n"
        "move /y \"%s\" \"%s\" >nul\r\n"
        "start \"\" \"%s\" --restarted\r\n"
        "del \"%s\" >nul 2>&1\r\n",
        exeA, exeA, newA, exeA, exeA, batA);

    HANDLE bf = CreateFileW(bat, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    if (bf == INVALID_HANDLE_VALUE) {
        DeleteFileW(newExe);
        out(OUT_ERR, L"Couldn't write the update helper.");
        return;
    }
    DWORD w; WriteFile(bf, script, n, &w, 0);
    CloseHandle(bf);

    outf(OUT_OK, L"Updating to v%s. Loomy will blink and come back.", latest);
    ui_redraw();
    Sleep(600);

    // launch the helper hidden, then quit so it can replace us
    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi;
    wchar_t cmd[MAX_PATH + 32];
    s_cpy(cmd, L"cmd.exe /c \"", MAX_PATH + 32);
    s_cat(cmd, bat, MAX_PATH + 32);
    s_cat(cmd, L"\"", MAX_PATH + 32);
    if (CreateProcessW(0, cmd, 0, 0, FALSE, CREATE_NO_WINDOW, 0, dir, &si, &pi)) {
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        ui_quit();   // closes cleanly so the .bat can swap the file
    } else {
        DeleteFileW(newExe); DeleteFileW(bat);
        out(OUT_ERR, L"Couldn't start the updater.");
    }
}
