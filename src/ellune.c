// ellune.c - the portable half of the PC talking to Ellune (2.22, remote). No OS calls, so the Linux tests run
// it; phonenet.c has the Windows half (the key, the signature, the WebSocket).
//   - SHA-256 (Monocypher has SHA-512 only; Ellune's guest list and room names use SHA-256)
//   - base64url, this PC's Ellune ID ("XXXX-XXXX") and guest line ("ID=key"), like the phone's (Ellune.kt)
//   - the relay room name, from the pairing: both ends know the PC's pairing public key, so nothing new is sent
//   - the text a request signs (see the top of ellune/src/index.js)
#include <stdint.h>
#include "loomy.h"

// ---------------------------------------------------------------- SHA-256 (FIPS 180-4)
static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha_block(uint32_t h[8], const uint8_t *b) {
    uint32_t w[64], a, c, d, e, f, g, hh, bb;
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)b[4 * i] << 24 | (uint32_t)b[4 * i + 1] << 16 | (uint32_t)b[4 * i + 2] << 8 | b[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = h[0]; bb = h[1]; c = h[2]; d = h[3]; e = h[4]; f = h[5]; g = h[6]; hh = h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = hh + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
        uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & bb) ^ (a & c) ^ (bb & c));
        hh = g; g = f; f = e; e = d + t1; d = c; c = bb; bb = a; a = t1 + t2;
    }
    h[0] += a; h[1] += bb; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

// two parts (a prefix and the data), which is all Loomy needs
void el_sha256_2(const uint8_t *p1, int n1, const uint8_t *p2, int n2, uint8_t out[32]) {
    uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    uint8_t blk[64];
    int fill = 0;
    unsigned long long total = (unsigned long long)n1 + (unsigned long long)n2;
    for (int part = 0; part < 2; part++) {
        const uint8_t *p = part ? p2 : p1;
        int n = part ? n2 : n1;
        for (int i = 0; i < n; i++) { blk[fill++] = p[i]; if (fill == 64) { sha_block(h, blk); fill = 0; } }
    }
    blk[fill++] = 0x80;
    if (fill > 56) { while (fill < 64) blk[fill++] = 0; sha_block(h, blk); fill = 0; }
    while (fill < 56) blk[fill++] = 0;
    unsigned long long bits = total * 8;
    for (int i = 7; i >= 0; i--) blk[fill++] = (uint8_t)(bits >> (8 * i));
    sha_block(h, blk);
    for (int i = 0; i < 8; i++) { out[4 * i] = (uint8_t)(h[i] >> 24); out[4 * i + 1] = (uint8_t)(h[i] >> 16); out[4 * i + 2] = (uint8_t)(h[i] >> 8); out[4 * i + 3] = (uint8_t)h[i]; }
}

void el_sha256(const uint8_t *p, int n, uint8_t out[32]) { el_sha256_2(p, n, 0, 0, out); }

// ---------------------------------------------------------------- encodings
int el_b64url(const uint8_t *p, int n, char *out, int cap) {
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    int o = 0;
    for (int i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)p[i] << 16 | (i + 1 < n ? (uint32_t)p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
        int k = n - i >= 3 ? 4 : n - i + 1;               // no padding
        for (int j = 0; j < k; j++) { if (o + 1 >= cap) { out[0] = 0; return 0; } out[o++] = T[(v >> (18 - 6 * j)) & 63]; }
    }
    out[o] = 0;
    return o;
}

static void hex(const uint8_t *p, int n, char *out) {
    static const char H[] = "0123456789abcdef";
    for (int i = 0; i < n; i++) { out[2 * i] = H[p[i] >> 4]; out[2 * i + 1] = H[p[i] & 15]; }
    out[2 * n] = 0;
}

// ---------------------------------------------------------------- names
// "XXXX-XXXX": 40 bits of SHA-256("Ellune id" + key), Crockford base32 (the same as the phone's deviceId)
void el_device_id(const uint8_t pub[65], char out[10]) {
    static const char A[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    uint8_t h[32];
    el_sha256_2((const uint8_t *)"Ellune id", 9, pub, 65, h);
    unsigned long long v = 0;
    for (int i = 0; i < 5; i++) v = v << 8 | h[i];
    for (int i = 0, o = 0; i < 8; i++) { if (i == 4) out[o++] = '-'; out[o++] = A[(v >> (35 - 5 * i)) & 31]; }
    out[9] = 0;
}

// "ID=base64url(key)": the line for Ellune's DEVICES list
int el_guest_line(const uint8_t pub[65], char *out, int cap) {
    if (cap < 100) return 0;
    el_device_id(pub, out);
    out[9] = '=';
    return el_b64url(pub, 65, out + 10, cap - 10) ? 1 : 0;
}

// the relay room: hex(SHA-256("Loomy room" + the PC's pairing public key))[0..32]
void el_room(const uint8_t pc_pk[32], char out[33]) {
    uint8_t h[32];
    el_sha256_2((const uint8_t *)"Loomy room", 10, pc_pk, 32, h);
    hex(h, 16, out);
}

// what a request signs: "ellune1\n" METHOD "\n" path "\n" time "\n" hex(sha256(body)); here the body is empty
int el_signed_text(const char *method, const char *path, unsigned long long ms, char *out, int cap) {
    char t[24], body[65];
    int tn = 0;
    unsigned long long v = ms;
    char rev[24];
    do { rev[tn++] = (char)('0' + v % 10); v /= 10; } while (v);
    for (int i = 0; i < tn; i++) t[i] = rev[tn - 1 - i];
    t[tn] = 0;
    uint8_t h[32];
    el_sha256((const uint8_t *)"", 0, h);
    hex(h, 32, body);
    const char *parts[] = { "ellune1\n", method, "\n", path, "\n", t, "\n", body };
    int o = 0;
    for (int i = 0; i < 8; i++)
        for (const char *c = parts[i]; *c; c++) { if (o + 1 >= cap) { out[0] = 0; return 0; } out[o++] = *c; }
    out[o] = 0;
    return o;
}
