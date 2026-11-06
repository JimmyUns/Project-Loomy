// extpkg.c - external packages (2.24, his ask): features that aren't built into Loomy but downloaded from their
// maker's GitHub releases when he turns them on in /pkg. This half has no OS calls (the Linux tests run it):
// the catalog, reading a release's JSON, and picking which file of it to download.
//
// Picking the file (his example: scrcpy ships win32, win64 and winarm64 zips next to Linux/macOS ones):
//  1. a file he chose for this PC (E in /pkg; also kept in Ellune so his other PCs get it), matched with the
//     version turned into * ("scrcpy-win64-v5.0.1.zip" -> "scrcpy-win64-*.zip", so it still fits next release);
//  2. otherwise automatic: only .zip files, nothing for another OS (linux, macos, android...), checksums,
//     signatures or sources; then the one built for this PC's processor (x64 / x86 / ARM64) wins. If two are
//     still equal, it asks (E) instead of guessing.
// Trust (extwin.c checks it all before anything runs): the release comes from the catalog's own repo, the file is
// downloaded from that repo's release URL only, GitHub's sha256 of it must match, and it must be fully uploaded.
#include "loomy.h"

static const ExtPkg PKGS[] = {
    { FEAT_SCRCPY, "Genymobile", "scrcpy", L"scrcpy.exe", L"github.com/Genymobile/scrcpy" },
};

const ExtPkg *ext_pkg(int feature) {
    for (int i = 0; i < (int)(sizeof PKGS / sizeof PKGS[0]); i++) if (PKGS[i].feature == feature) return &PKGS[i];
    return 0;
}

const char *ext_arch_name(int arch) { return arch == EXT_ARM64 ? "arm64" : arch == EXT_X86 ? "x86" : "x64"; }

// ---------------------------------------------------------------- small string helpers (no CRT)
static int alen(const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static char low(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }
static int has(const char *s, const char *w) {             // contains, any case
    int n = alen(s), m = alen(w);
    for (int i = 0; i + m <= n; i++) {
        int k = 0;
        while (k < m && low(s[i + k]) == low(w[k])) k++;
        if (k == m) return 1;
    }
    return 0;
}
static int ends(const char *s, const char *w) {
    int n = alen(s), m = alen(w);
    if (m > n) return 0;
    for (int i = 0; i < m; i++) if (low(s[n - m + i]) != low(w[i])) return 0;
    return 1;
}
static int starts(const char *s, const char *w) { for (int i = 0; w[i]; i++) if (s[i] != w[i]) return 0; return 1; }
static int keq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void acpy(char *d, const char *s, int cap) { int i = 0; while (s[i] && i < cap - 1) { d[i] = s[i]; i++; } d[i] = 0; }

// ---------------------------------------------------------------- a tiny JSON walker (only what a release needs)
static const char *ws(const char *p) { while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++; return p; }

// a string at p (after its opening quote) into out; returns just past the closing quote, or 0
static const char *str(const char *p, char *out, int cap) {
    int n = 0;
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '\\') {
            c = *p++;
            if (!c) return 0;
            if (c == 'n') c = '\n'; else if (c == 't') c = '\t'; else if (c == 'r') c = '\r';
            else if (c == 'u') { for (int i = 0; i < 4; i++) if (!*p++) return 0; c = '?'; }
            else if (c == 'b' || c == 'f') c = ' ';
        }
        if (out && n < cap - 1) out[n++] = c;
    }
    if (out && cap) out[n] = 0;
    return *p == '"' ? p + 1 : 0;
}

// skip one value; returns just past it, or 0 if it's broken
static const char *skip(const char *p, int depth) {
    p = ws(p);
    if (depth > 20) return 0;
    if (*p == '"') return str(p + 1, 0, 0);
    if (*p == '{' || *p == '[') {
        char close = *p == '{' ? '}' : ']';
        p = ws(p + 1);
        if (*p == close) return p + 1;
        for (;;) {
            if (close == '}') {
                if (*p != '"' || !(p = str(p + 1, 0, 0))) return 0;
                p = ws(p);
                if (*p++ != ':') return 0;
            }
            if (!(p = skip(p, depth + 1))) return 0;
            p = ws(p);
            if (*p == ',') { p = ws(p + 1); continue; }
            return *p == close ? p + 1 : 0;
        }
    }
    while (*p && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\r' && *p != '\n') p++;   // number, true...
    return p;
}

static unsigned long long num(const char *p) {
    unsigned long long v = 0;
    p = ws(p);
    while (*p >= '0' && *p <= '9') v = v * 10 + (unsigned)(*p++ - '0');
    return v;
}

// one asset object at p ('{'); returns just past it
static const char *asset(const char *p, ExtAsset *a) {
    char k[40];
    p = ws(p + 1);
    if (*p == '}') return p + 1;
    for (;;) {
        if (*p != '"' || !(p = str(p + 1, k, sizeof k))) return 0;
        p = ws(p);
        if (*p++ != ':') return 0;
        p = ws(p);
        const char *v = p;
        if (*v == '"') {
            char *dst = 0, state[16]; int cap = 0;
            if (keq(k, "name")) { dst = a->name; cap = sizeof a->name; }
            else if (keq(k, "browser_download_url")) { dst = a->url; cap = sizeof a->url; }
            else if (keq(k, "digest")) { dst = a->digest; cap = sizeof a->digest; }
            else if (keq(k, "state")) { dst = state; cap = sizeof state; }
            if (!(p = str(v + 1, dst, cap))) return 0;
            if (dst == state) a->uploaded = keq(state, "uploaded");
        } else {
            if (keq(k, "size")) a->size = num(v);
            if (!(p = skip(v, 1))) return 0;
        }
        p = ws(p);
        if (*p == ',') { p = ws(p + 1); continue; }
        return *p == '}' ? p + 1 : 0;
    }
}

int ext_parse_release(const char *json, ExtRelease *r) {
    r->tag[0] = 0; r->n = 0;
    if (!json) return 0;
    const char *p = ws(json);
    if (*p != '{') return 0;
    p = ws(p + 1);
    char k[40];
    while (*p == '"') {
        if (!(p = str(p + 1, k, sizeof k))) return 0;
        p = ws(p);
        if (*p++ != ':') return 0;
        p = ws(p);
        if (keq(k, "tag_name") && *p == '"') {
            if (!(p = str(p + 1, r->tag, sizeof r->tag))) return 0;
        } else if (keq(k, "assets") && *p == '[') {
            p = ws(p + 1);
            while (*p == '{') {
                ExtAsset tmp, *a = r->n < EXT_MAXASSETS ? &r->a[r->n] : &tmp;
                for (int i = 0; i < (int)sizeof *a; i++) ((char *)a)[i] = 0;
                if (!(p = asset(p, a))) return 0;
                if (a != &tmp && a->name[0]) r->n++;
                p = ws(p);
                if (*p == ',') p = ws(p + 1);
            }
            if (*p++ != ']') return 0;
        } else if (!(p = skip(p, 1))) return 0;
        p = ws(p);
        if (*p == ',') p = ws(p + 1);
    }
    return r->tag[0] && r->n > 0;
}

// ---------------------------------------------------------------- choosing the file
int ext_glob(const char *pat, const char *s) {
    if (!*pat) return !*s;
    if (*pat == '*') {
        for (;;) {
            if (ext_glob(pat + 1, s)) return 1;
            if (!*s++) return 0;
        }
    }
    return *s && low(*pat) == low(*s) && ext_glob(pat + 1, s + 1);
}

// "scrcpy-win64-v5.0.1.zip" with tag "v5.0.1" -> "scrcpy-win64-*.zip". Something already holding a * is kept.
void ext_pattern(const char *typed, const char *tag, char *out, int cap) {
    while (*typed == ' ') typed++;
    const char *ver = tag && (tag[0] == 'v' || tag[0] == 'V') ? tag + 1 : tag;
    int vl = alen(ver), n = 0, i = 0;
    if (has(typed, "*") || vl < 1) acpy(out, typed, cap);
    else {
        while (typed[i] && n < cap - 1) {
            int k = 0;
            while (k < vl && typed[i + k] == ver[k]) k++;
            if (k == vl) {
                if (n > 0 && low(out[n - 1]) == 'v') n--;      // "v5.0.1" goes as a whole
                out[n++] = '*';
                i += vl;
                continue;
            }
            out[n++] = typed[i++];
        }
        out[n] = 0;
    }
    n = alen(out);
    while (n > 0 && out[n - 1] == ' ') out[--n] = 0;
}

static int other_os(const char *n) {
    static const char *NO[] = { "linux", "macos", "mac-", "darwin", "osx", "android", "ios", "freebsd", "apk",
                                "server", "source", "src", "sha256", "sha512", "checksum", "debug", "symbols", "pdb", 0 };
    for (int i = 0; NO[i]; i++) if (has(n, NO[i])) return 1;
    return ends(n, ".asc") || ends(n, ".sig") || ends(n, ".txt");
}

// which processor a file is for: -1 = doesn't say
static int file_arch(const char *n) {
    if (has(n, "arm64") || has(n, "aarch64")) return EXT_ARM64;
    if (has(n, "x86_64") || has(n, "x86-64") || has(n, "amd64") || has(n, "x64") || has(n, "win64") || has(n, "64bit") || has(n, "64-bit")) return EXT_X64;
    if (has(n, "win32") || has(n, "x86") || has(n, "i686") || has(n, "i386") || has(n, "32bit") || has(n, "32-bit")) return EXT_X86;
    return -1;
}

static int score(const char *n, int arch) {
    if (!ends(n, ".zip") || other_os(n)) return 0;
    int a = file_arch(n), s = 10;
    if (has(n, "win")) s += 5;
    if (a == arch) s += 20;
    else if (a < 0) s += 8;                                // one file for every processor
    else if (arch == EXT_ARM64 && a == EXT_X64) s += 4;    // Windows on ARM runs x64 too, slower
    else if (arch == EXT_X64 && a == EXT_X86) s += 2;      // 32-bit runs on 64-bit
    else return 0;                                         // won't run here
    return s;
}

int ext_pick(const ExtRelease *r, const char *pattern, int arch, int *idx) {
    *idx = -1;
    if (pattern && pattern[0]) {
        int found = -1, count = 0;
        for (int i = 0; i < r->n; i++) if (ext_glob(pattern, r->a[i].name)) { if (found < 0) found = i; count++; }
        if (count == 0) return PICK_NOMATCH;
        *idx = found;                                       // several match: the first, in GitHub's order
        return PICK_OK;
    }
    int best = 0, besti = -1, ties = 0;
    for (int i = 0; i < r->n; i++) {
        int s = score(r->a[i].name, arch);
        if (s > best) { best = s; besti = i; ties = 0; }
        else if (s == best && s > 0) ties++;
    }
    if (besti < 0) return PICK_NONE;
    if (ties) return PICK_MANY;
    *idx = besti;
    return PICK_OK;
}

// the file has to come from the package's own repo and this release, fully uploaded, with GitHub's sha256
int ext_asset_ok(const ExtAsset *a, const ExtPkg *p, const char *tag) {
    char want[200];
    int n = 0;
    const char *parts[] = { "https://github.com/", p->owner, "/", p->repo, "/releases/download/", tag, "/", 0 };
    for (int i = 0; parts[i]; i++) for (int k = 0; parts[i][k] && n < (int)sizeof want - 1; k++) want[n++] = parts[i][k];
    want[n] = 0;
    if (!starts(a->url, want) || !a->url[n] || has(a->url + n, "/") || has(a->url, "..")) return 0;
    if (!a->uploaded || a->size < 1024 || a->size > 400ull * 1024 * 1024) return 0;
    if (!starts(a->digest, "sha256:") || alen(a->digest) != 7 + 64) return 0;
    for (int i = 7; i < 71; i++) { char c = a->digest[i]; if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return 0; }
    for (int i = 0; a->name[i]; i++) if (a->name[i] == '/' || a->name[i] == '\\' || a->name[i] == ':') return 0;
    return 1;
}
