// version.c - version numbers and the "is this release really mine?" check for /update.
// Pure logic, no network, so tests/run.sh can exercise it on Linux.
//
// Versions: "2.9" is a version's first build. A letter after it is a fix build of the
// same version with no new features: 2.9 < 2.9b < 2.9c < 2.10. Keep LOOMY_VERSION
// identical to the release tag minus the v, letter included.
#include "loomy.h"

static int is_digit(wchar_t c) { return c >= L'0' && c <= L'9'; }
static int is_letter(wchar_t c) { c = s_low(c); return c >= L'a' && c <= L'z'; }

// 1 if a is newer than b. Never loops forever, whatever junk it's given.
int ver_gt(const wchar_t *a, const wchar_t *b) {
    while (*a || *b) {
        int na = 0, nb = 0;
        while (is_digit(*a)) na = na * 10 + (*a++ - L'0');
        while (is_digit(*b)) nb = nb * 10 + (*b++ - L'0');
        if (na != nb) return na > nb;

        int la = is_letter(*a) ? s_low(*a++) - L'a' + 1 : 0;   // no letter = 0, a = 1, b = 2...
        int lb = is_letter(*b) ? s_low(*b++) - L'a' + 1 : 0;
        if (la != lb) return la > lb;

        while (*a && *a != L'.') a++;                         // skip anything unexpected
        while (*b && *b != L'.') b++;
        if (*a == L'.') a++;
        if (*b == L'.') b++;
    }
    return 0;
}

// ---------------------------------------------------------------- tiny JSON reading
// Just enough to read GitHub's release JSON: find a "key" and read the string after it.

static int is_ws(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// find "key" (with quotes, followed by a colon) at or after p. Returns the spot after the colon, or 0.
static const char *find_key(const char *p, const char *key) {
    int n = 0; while (key[n]) n++;
    for (; p && *p; p++) {
        if (*p != '"') continue;
        int i = 0;
        while (i < n && p[1 + i] == key[i]) i++;
        if (i < n || p[1 + n] != '"') continue;
        const char *q = p + 2 + n;
        while (is_ws(*q)) q++;
        if (*q == ':') return q + 1;
    }
    return 0;
}

// read the string value at p (after a colon) into out. 1 if it was a string.
static int read_str(const char *p, char *out, int cap) {
    out[0] = 0;
    if (!p) return 0;
    while (is_ws(*p)) p++;
    if (*p != '"') return 0;
    p++;
    int n = 0;
    while (*p && *p != '"' && n < cap - 1) {
        if (*p == '\\' && p[1]) p++;
        out[n++] = *p++;
    }
    out[n] = 0;
    return *p == '"';
}

static int a_eq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

// tags only ever look like v2.10 or v2.9b; anything else gets refused
static int tag_ok(const char *t) {
    if (!*t) return 0;
    for (; *t; t++) {
        char c = *t;
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '-')) return 0;
    }
    return 1;
}

// Checks GitHub's JSON for the latest release. It's only trusted if the release was published
// by owner (you) and its loomy.exe was uploaded by GitHub Actions (your build), not by anyone
// else, e.g. a bot account. Fills tag (as published, like "v2.10") and the version ("2.10").
int release_check(const char *json, const char *owner, char *tag, int tagCap, wchar_t *ver, int verCap) {
    char s[128];
    tag[0] = 0; ver[0] = 0;

    if (!read_str(find_key(json, "tag_name"), tag, tagCap) || !tag_ok(tag)) return REL_NO_RELEASE;
    const char *t = tag + ((tag[0] == 'v' || tag[0] == 'V') ? 1 : 0);
    int n = 0;
    while (t[n] && n < verCap - 1) { ver[n] = (wchar_t)t[n]; n++; }
    ver[n] = 0;

    // the release's author: the first "author" in the JSON (assets use "uploader")
    const char *au = find_key(json, "author");
    if (!au || !read_str(find_key(au, "login"), s, sizeof(s)) || !a_eq(s, owner)) return REL_BAD_AUTHOR;

    // the loomy.exe asset and who uploaded it
    const char *as = find_key(json, "assets");
    for (const char *p = as; p; ) {
        const char *nm = find_key(p, "name");
        if (!nm) break;
        if (read_str(nm, s, sizeof(s)) && a_eq(s, "loomy.exe")) {
            const char *up = find_key(nm, "uploader");
            if (!up || !read_str(find_key(up, "login"), s, sizeof(s)) || !a_eq(s, "github-actions[bot]"))
                return REL_BAD_ASSET;
            return REL_OK;
        }
        p = nm;
    }
    return REL_NO_ASSET;
}
