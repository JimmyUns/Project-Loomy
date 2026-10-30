// pocket.c - Pocket Loomy's wire protocol, both roles (PC and phone). See pocket.h for how to
// drive it and POCKET-HANDOVER.md ("Wire protocol") for the message list and the reasoning.
//
// Shape of it:
//   frame     = 2-byte big-endian length L (1..512) + L bytes. Anything else ends the connection.
//   pairing   = CPace (draft-irtf-cfrg-cpace, suite CPACE-X25519-SHA512, initiator = phone) on the
//               8-digit code, plus the draft's key confirmation; then both swap fresh long-term
//               X25519 keys inside the encrypted channel.
//   connect   = fresh ephemerals + both long-term keys, four X25519s (ee, es, se, ss, like Noise KK),
//               HMAC-SHA512 key schedule over the whole transcript, confirmation both ways.
//   after     = XChaCha20-Poly1305 per frame, one key per direction, counter nonces never sent
//               (so a replayed, dropped or reordered frame fails its tag), the length prefix
//               authenticated as associated data.
// Nothing a peer sends is parsed before its size is checked, and nothing after the handshake is
// looked at before its tag checks out. Fixed buffers only.
#include "pocket.h"
#include "monocypher.h"
#include "monocypher-ed25519.h"

// plaintext handshake messages (first byte of the frame body)
#define M_HELLO   0x01   // PC:    version, flags, nb[16]
#define M_PAIR_A  0x02   // phone: na[16], Ya[32], name
#define M_PAIR_B  0x03   // PC:    Yb[32], Tb[64], name
#define M_PAIR_C  0x04   // phone: Ta[64]
#define M_CONN_A  0x05   // phone: na[16], Ea[32], name
#define M_CONN_B  0x06   // PC:    Eb[32], Tb[32]
#define M_CONN_C  0x07   // phone: Ta[32]
// encrypted messages (first byte inside the sealed body)
#define E_KEY_PHONE 0x10 // phone's new long-term public key
#define E_KEY_PC    0x11 // PC's new long-term public key
#define E_CMD       0x20
#define E_REPLY     0x21
#define E_PING      0x22
#define E_PONG      0x23

#define F_PAIRING 1      // HELLO flags: a pairing code is open
#define F_TRUSTED 2      //              a phone is trusted

enum { ST_PHONE_HELLO, ST_PC_HELLO, ST_PAIR_B, ST_PAIR_C, ST_PAIR_KEY, ST_CONN_B, ST_CONN_C,
       ST_READY, ST_DONE, ST_FAILED };

// CPace channel identifier: who is who (initiator first, so a relayed run can't swap roles),
// then HELLO's version + flags, so tampering with them breaks the run instead of slipping by
static const uint8_t CI_BASE[] = { 12, 'L','o','o','m','y','P','o','c','k','e','t','1',
                                   5, 'p','h','o','n','e', 2, 'p','c' };
#define CI_LEN (sizeof(CI_BASE) + 3)
static void make_ci(const PkConn *c, uint8_t ci[CI_LEN]) {
    for (unsigned i = 0; i < sizeof(CI_BASE); i++) ci[i] = CI_BASE[i];
    ci[sizeof(CI_BASE)] = 2; ci[sizeof(CI_BASE) + 1] = c->hp[0]; ci[sizeof(CI_BASE) + 2] = c->hp[1];
}

// ---------------------------------------------------------------- small helpers
static void cp(uint8_t *d, const uint8_t *s, int n) { for (int i = 0; i < n; i++) d[i] = s[i]; }
static int  zero32(const uint8_t *p) { uint8_t z[32] = {0}; return crypto_verify32(p, z) == 0; }
static int  slen(const char *s) { int n = 0; while (s[n]) n++; return n; }

// LEB128 length + bytes (the draft's prepend_len), fed straight into a hash
static void lv_hash(crypto_sha512_ctx *h, const uint8_t *p, int n) {
    uint8_t pre[4]; int k = 0, len = n;
    do { pre[k++] = (uint8_t)((len & 0x7f) | (len >= 128 ? 0x80 : 0)); len >>= 7; } while (len);
    crypto_sha512_update(h, pre, k);
    crypto_sha512_update(h, p, n);
}
static void lv_hmac(crypto_sha512_hmac_ctx *h, const uint8_t *p, int n) {
    uint8_t pre[4]; int k = 0, len = n;
    do { pre[k++] = (uint8_t)((len & 0x7f) | (len >= 128 ? 0x80 : 0)); len >>= 7; } while (len);
    crypto_sha512_hmac_update(h, pre, k);
    crypto_sha512_hmac_update(h, p, n);
}
static void hmac_label(uint8_t out[64], const uint8_t key[64], const char *label) {
    crypto_sha512_hmac(out, key, 64, (const uint8_t *)label, slen(label));
}

// ---------------------------------------------------------------- CPace (X25519 + SHA-512)
// generator: SHA-512 of the padded generator string, bit 255 cleared (RFC 7748 decodeUCoordinate),
// then Elligator 2 (RFC 9380 map_to_curve_elligator2). Monocypher's map reads only 254 bits, so a
// field element r with bit 254 set is first swapped for p - r (or r - p if r >= p): the map only
// depends on r^2, and that value fits in 254 bits. Done without branches on r.
static void fold254(uint8_t r[32]) {
    static const uint8_t P[32] = { 0xed, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0x7f };
    uint8_t d[32], e[32];
    int borrow = 0, borrow2 = 0;
    for (int i = 0; i < 32; i++) {                     // d = p - r, e = r - p
        int t = P[i] - r[i] - borrow;   borrow  = t < 0; d[i] = (uint8_t)t;
        int u = r[i] - P[i] - borrow2;  borrow2 = u < 0; e[i] = (uint8_t)u;
    }
    uint8_t big  = (uint8_t)(0 - ((r[31] >> 6) & 1));  // r >= 2^254: needs folding
    uint8_t over = (uint8_t)(0 - borrow);              // r > p: use r - p instead of p - r
    for (int i = 0; i < 32; i++) {
        uint8_t f = (uint8_t)((e[i] & over) | (d[i] & ~over));
        r[i] = (uint8_t)((f & big) | (r[i] & ~big));
    }
    crypto_wipe(d, 32); crypto_wipe(e, 32);
}

void pk_elligator(uint8_t u[32], const uint8_t field[32]) {
    uint8_t r[32];
    cp(r, field, 32);
    r[31] &= 0x7f;                                      // decodeUCoordinate: 255 bits
    fold254(r);
    crypto_elligator_map(u, r);
    crypto_wipe(r, 32);
}

void pk_cpace_generator(uint8_t g[32], const uint8_t *prs, int prslen, const uint8_t *ci, int cilen,
                        const uint8_t *sid, int sidlen) {
    static const uint8_t DSI[] = { 'C','P','a','c','e','2','5','5' };
    static const uint8_t Z[128] = {0};
    // len_zpad = max(0, s_in_bytes(128) - 1 - len(prepend_len(PRS)) - len(prepend_len(DSI)))
    int prepPrs = prslen < 128 ? 1 + prslen : 2 + prslen;
    int zpad = 128 - 1 - prepPrs - (1 + 8);
    if (zpad < 0) zpad = 0;
    crypto_sha512_ctx h;
    crypto_sha512_init(&h);
    lv_hash(&h, DSI, 8);
    lv_hash(&h, prs, prslen);
    lv_hash(&h, Z, zpad);
    lv_hash(&h, ci, cilen);
    lv_hash(&h, sid, sidlen);
    uint8_t hash[64];
    crypto_sha512_final(&h, hash);
    pk_elligator(g, hash);
    crypto_wipe(hash, 64);
}

// ISK = H(lv_cat(DSI || "_ISK", sid, K) || lv_cat(Ya, ADa) || lv_cat(Yb, ADb))  (initiator-responder)
void pk_cpace_isk(uint8_t isk[64], const uint8_t *sid, int sidlen, const uint8_t K[32],
                  const uint8_t Ya[32], const uint8_t *ada, int adalen,
                  const uint8_t Yb[32], const uint8_t *adb, int adblen) {
    static const uint8_t DSI_ISK[] = { 'C','P','a','c','e','2','5','5','_','I','S','K' };
    crypto_sha512_ctx h;
    crypto_sha512_init(&h);
    lv_hash(&h, DSI_ISK, 12);
    lv_hash(&h, sid, sidlen);
    lv_hash(&h, K, 32);
    lv_hash(&h, Ya, 32);  lv_hash(&h, ada, adalen);
    lv_hash(&h, Yb, 32);  lv_hash(&h, adb, adblen);
    crypto_sha512_final(&h, isk);
}

// the draft's key confirmation: mac_key = H("CPaceMac" || sid || ISK), T = HMAC(mac_key, lv_cat(Y, AD))
static void cpace_tag(uint8_t t[64], const uint8_t mac_key[64], const uint8_t Y[32], const uint8_t *ad, int adlen) {
    crypto_sha512_hmac_ctx h;
    crypto_sha512_hmac_init(&h, mac_key, 64);
    lv_hmac(&h, Y, 32);
    lv_hmac(&h, ad, adlen);
    crypto_sha512_hmac_final(&h, t);
}

static void cpace_mac_key(uint8_t mk[64], const uint8_t *sid, int sidlen, const uint8_t isk[64]) {
    crypto_sha512_ctx h;
    crypto_sha512_init(&h);
    crypto_sha512_update(&h, (const uint8_t *)"CPaceMac", 8);
    crypto_sha512_update(&h, sid, sidlen);
    crypto_sha512_update(&h, isk, 64);
    crypto_sha512_final(&h, mk);
}

// ---------------------------------------------------------------- keys and frames
static void set_keys(PkConn *c, const uint8_t secret[64]) {
    uint8_t p2c[64], c2p[64];
    hmac_label(p2c, secret, "LoomyPocket1 phone->pc");
    hmac_label(c2p, secret, "LoomyPocket1 pc->phone");
    cp(c->ktx, c->role == PK_PC ? c2p : p2c, 32);
    cp(c->krx, c->role == PK_PC ? p2c : c2p, 32);
    c->ntx = c->nrx = 0;
    crypto_wipe(p2c, 64); crypto_wipe(c2p, 64);
}

static void nonce_of(uint8_t n[24], uint64_t ctr) {
    for (int i = 0; i < 24; i++) n[i] = 0;
    for (int i = 0; i < 8; i++) n[i] = (uint8_t)(ctr >> (8 * i));
}

// queue a plaintext handshake frame
static uint8_t *frame(PkConn *c, int L) {
    if (L < 1 || L > PK_MAXFRAME || c->outlen + 2 + L > (int)sizeof(c->out)) return 0;
    uint8_t *f = c->out + c->outlen;
    f[0] = (uint8_t)(L >> 8); f[1] = (uint8_t)L;
    c->outlen += 2 + L;
    return f + 2;
}

static int seal(PkConn *c, uint8_t type, const uint8_t *d, int n) {
    int L = 1 + n + PK_MAC;
    if (n < 0 || L > PK_MAXFRAME || c->outlen + 2 + L > (int)sizeof(c->out)) return 0;
    uint8_t *f = c->out + c->outlen, plain[PK_MAXFRAME], nonce[24];
    f[0] = (uint8_t)(L >> 8); f[1] = (uint8_t)L;
    plain[0] = type;
    cp(plain + 1, d, n);
    nonce_of(nonce, c->ntx);
    crypto_aead_lock(f + 2, f + 2 + 1 + n, c->ktx, nonce, f, 2, plain, 1 + n);
    c->ntx++;
    c->outlen += 2 + L;
    crypto_wipe(plain, 1 + n);
    return 1;
}

static int fail(PkConn *c, int why) {
    uint8_t keep_role = (uint8_t)c->role;
    uint8_t name[PK_NAMEMAX]; int nl = c->peer_namelen;      // keep the claimed name for the log
    cp(name, c->peer_name, nl);
    crypto_wipe(c, sizeof(*c));
    c->role = keep_role;
    cp(c->peer_name, name, nl); c->peer_namelen = nl;
    c->state = ST_FAILED;
    c->fail = why;
    return PK_EV_FAIL;
}

static void take_name(PkConn *c, const uint8_t *p, int n) {
    if (n > PK_NAMEMAX) n = PK_NAMEMAX;
    cp(c->peer_name, p, n);
    c->peer_namelen = n;
}

// ---------------------------------------------------------------- start
static void init_common(PkConn *c, int role, const uint8_t *code, int trust, const uint8_t *my_sk,
                        const uint8_t *my_pk, const uint8_t *peer_pk, const uint8_t *name, int namelen) {
    crypto_wipe(c, sizeof(*c));
    c->role = role;
    if (code) { cp(c->code, code, PK_CODELEN); c->has_code = 1; }
    if (trust) { cp(c->my_sk, my_sk, 32); cp(c->my_pk, my_pk, 32); cp(c->peer_pk, peer_pk, 32); c->has_trust = 1; }
    if (namelen > PK_NAMEMAX) namelen = PK_NAMEMAX;
    if (namelen < 0) namelen = 0;
    cp(c->my_name, name, namelen);
    c->my_namelen = namelen;
}

void pk_init_pc(PkConn *c, const uint8_t *code, int trust, const uint8_t *my_sk, const uint8_t *my_pk,
                const uint8_t *peer_pk, const uint8_t *name, int namelen) {
    init_common(c, PK_PC, code, trust, my_sk, my_pk, peer_pk, name, namelen);
    c->state = ST_PC_HELLO;
    pk_random(c->nb, 16);
    uint8_t *b = frame(c, 19);
    c->hp[0] = PK_VERSION;
    c->hp[1] = (uint8_t)((c->has_code ? F_PAIRING : 0) | (c->has_trust ? F_TRUSTED : 0));
    b[0] = M_HELLO; b[1] = c->hp[0]; b[2] = c->hp[1];
    cp(b + 3, c->nb, 16);
}

void pk_init_phone(PkConn *c, const uint8_t *code, int trust, const uint8_t *my_sk, const uint8_t *my_pk,
                   const uint8_t *peer_pk, const uint8_t *name, int namelen) {
    init_common(c, PK_PHONE, code, trust, my_sk, my_pk, peer_pk, name, namelen);
    c->state = ST_PHONE_HELLO;
}

// ---------------------------------------------------------------- the connect key schedule
// th = H(lv_cat("LoomyPocket1 connect", version+flags, nb, na, Ea, Eb, S_phone, S_pc, phone name))
// master = HMAC(th, ee || es || se || ss); tags and traffic keys come from master.
static int connect_keys(PkConn *c, const uint8_t Ea[32], const uint8_t Eb[32], const uint8_t *name, int namelen,
                        uint8_t tag_pc[32], uint8_t tag_phone[32]) {
    uint8_t dh[128];
    int phone = c->role == PK_PHONE;
    const uint8_t *Sa = phone ? c->my_pk : c->peer_pk, *Sb = phone ? c->peer_pk : c->my_pk;
    if (phone) {
        crypto_x25519(dh,      c->eph,   Eb);          // ee
        crypto_x25519(dh + 32, c->eph,   c->peer_pk);  // es: phone ephemeral x PC static
        crypto_x25519(dh + 64, c->my_sk, Eb);          // se: phone static x PC ephemeral
        crypto_x25519(dh + 96, c->my_sk, c->peer_pk);  // ss
    } else {
        crypto_x25519(dh,      c->eph,   Ea);
        crypto_x25519(dh + 32, c->my_sk, Ea);
        crypto_x25519(dh + 64, c->eph,   c->peer_pk);
        crypto_x25519(dh + 96, c->my_sk, c->peer_pk);
    }
    int bad = zero32(dh) | zero32(dh + 32) | zero32(dh + 64) | zero32(dh + 96);
    if (bad) { crypto_wipe(dh, 128); return 0; }

    static const uint8_t LABEL[] = "LoomyPocket1 connect";
    crypto_sha512_ctx h;
    uint8_t th[64], master[64], t[64];
    crypto_sha512_init(&h);
    lv_hash(&h, LABEL, sizeof(LABEL) - 1);
    lv_hash(&h, c->hp, 2);
    lv_hash(&h, c->nb, 16); lv_hash(&h, c->na, 16);
    lv_hash(&h, Ea, 32);    lv_hash(&h, Eb, 32);
    lv_hash(&h, Sa, 32);    lv_hash(&h, Sb, 32);
    lv_hash(&h, name, namelen);
    crypto_sha512_final(&h, th);
    crypto_sha512_hmac(master, th, 64, dh, 128);
    hmac_label(t, master, "LoomyPocket1 pc confirm");    cp(tag_pc, t, 32);
    hmac_label(t, master, "LoomyPocket1 phone confirm"); cp(tag_phone, t, 32);
    set_keys(c, master);
    crypto_wipe(dh, 128); crypto_wipe(master, 64); crypto_wipe(t, 64);
    return 1;
}

// pairing: both sides' CPace work after the peer's share is known
static int pair_keys(PkConn *c, const uint8_t Ya[32], const uint8_t *ada, int adalen,
                     const uint8_t Yb[32], const uint8_t *adb, int adblen, uint8_t Ta[64], uint8_t Tb[64]) {
    uint8_t K[32], sid[32], isk[64], mk[64];
    crypto_x25519(K, c->eph, c->role == PK_PC ? Ya : Yb);
    if (zero32(K)) { crypto_wipe(K, 32); return 0; }      // low-order point: abort (CPace: K = G.I)
    cp(sid, c->nb, 16); cp(sid + 16, c->na, 16);
    pk_cpace_isk(isk, sid, 32, K, Ya, ada, adalen, Yb, adb, adblen);
    cpace_mac_key(mk, sid, 32, isk);
    cpace_tag(Ta, mk, Ya, ada, adalen);
    cpace_tag(Tb, mk, Yb, adb, adblen);
    set_keys(c, isk);
    crypto_wipe(K, 32); crypto_wipe(isk, 64); crypto_wipe(mk, 64);
    return 1;
}

static void cpace_share(PkConn *c, uint8_t Y[32]) {
    uint8_t g[32], sid[32], ci[CI_LEN];
    cp(sid, c->nb, 16); cp(sid + 16, c->na, 16);
    make_ci(c, ci);
    pk_cpace_generator(g, c->code, PK_CODELEN, ci, CI_LEN, sid, 32);
    pk_random(c->eph, 32);
    crypto_x25519(Y, c->eph, g);
    crypto_wipe(g, 32);
}

static void new_static(PkConn *c) {
    pk_random(c->my_sk, 32);
    crypto_x25519_public_key(c->my_pk, c->my_sk);
}

static int peer_key_ok(PkConn *c) {
    uint8_t t[32];
    crypto_x25519(t, c->my_sk, c->peer_pk);
    int ok = !zero32(t);
    crypto_wipe(t, 32);
    return ok;
}

// ---------------------------------------------------------------- one plaintext frame
static int on_plain(PkConn *c, const uint8_t *b, int L) {
    int t = b[0];
    uint8_t Ta[64], Tb[64];

    if (c->role == PK_PHONE && c->state == ST_PHONE_HELLO && t == M_HELLO) {
        if (L != 19) return fail(c, PK_F_FRAMESIZE);
        if (b[1] != PK_VERSION) return fail(c, PK_F_VERSION);
        if (b[2] & ~(F_PAIRING | F_TRUSTED)) return fail(c, PK_F_BADMSG);   // reserved bits must be 0
        c->hp[0] = b[1]; c->hp[1] = b[2];
        cp(c->nb, b + 3, 16);
        pk_random(c->na, 16);
        if (c->has_code) {
            if (!(b[2] & F_PAIRING)) return fail(c, PK_F_NOPAIRING);
            cpace_share(c, c->E_me);
            uint8_t *o = frame(c, 1 + 16 + 32 + c->my_namelen);
            o[0] = M_PAIR_A; cp(o + 1, c->na, 16); cp(o + 17, c->E_me, 32); cp(o + 49, c->my_name, c->my_namelen);
            c->state = ST_PAIR_B;
            return PK_EV_NONE;
        }
        if (!c->has_trust || !(b[2] & F_TRUSTED)) return fail(c, PK_F_NOTRUST);
        pk_random(c->eph, 32);
        crypto_x25519_public_key(c->E_me, c->eph);
        uint8_t *o = frame(c, 1 + 16 + 32 + c->my_namelen);
        o[0] = M_CONN_A; cp(o + 1, c->na, 16); cp(o + 17, c->E_me, 32); cp(o + 49, c->my_name, c->my_namelen);
        c->state = ST_CONN_B;
        return PK_EV_NONE;
    }

    if (c->role == PK_PC && c->state == ST_PC_HELLO && t == M_PAIR_A) {
        if (L < 49 || L > 49 + PK_NAMEMAX) return fail(c, PK_F_FRAMESIZE);
        take_name(c, b + 49, L - 49);
        if (!c->has_code) return fail(c, PK_F_NOPAIRING);
        cp(c->na, b + 1, 16);
        cp(c->E_peer, b + 17, 32);
        cpace_share(c, c->E_me);
        if (!pair_keys(c, c->E_peer, c->peer_name, c->peer_namelen, c->E_me, c->my_name, c->my_namelen, Ta, Tb))
            return fail(c, PK_F_LOWORDER);
        cp(c->expect, Ta, 64);
        uint8_t *o = frame(c, 1 + 32 + 64 + c->my_namelen);
        o[0] = M_PAIR_B; cp(o + 1, c->E_me, 32); cp(o + 33, Tb, 64); cp(o + 97, c->my_name, c->my_namelen);
        crypto_wipe(Ta, 64); crypto_wipe(Tb, 64);
        crypto_wipe(c->code, PK_CODELEN); c->has_code = 0;   // one run per code, whatever happens next
        c->state = ST_PAIR_C;
        return PK_EV_ATTEMPT;
    }

    if (c->role == PK_PHONE && c->state == ST_PAIR_B && t == M_PAIR_B) {
        if (L < 97 || L > 97 + PK_NAMEMAX) return fail(c, PK_F_FRAMESIZE);
        take_name(c, b + 97, L - 97);
        cp(c->E_peer, b + 1, 32);
        if (!pair_keys(c, c->E_me, c->my_name, c->my_namelen, c->E_peer, c->peer_name, c->peer_namelen, Ta, Tb))
            return fail(c, PK_F_LOWORDER);
        int ok = crypto_verify64(Tb, b + 33) == 0;
        crypto_wipe(Tb, 64);
        if (!ok) { crypto_wipe(Ta, 64); return fail(c, PK_F_CODE); }
        uint8_t *o = frame(c, 65);
        o[0] = M_PAIR_C; cp(o + 1, Ta, 64);
        crypto_wipe(Ta, 64);
        new_static(c);
        seal(c, E_KEY_PHONE, c->my_pk, 32);
        c->state = ST_PAIR_KEY;
        return PK_EV_NONE;
    }

    if (c->role == PK_PC && c->state == ST_PAIR_C && t == M_PAIR_C) {
        if (L != 65) return fail(c, PK_F_FRAMESIZE);
        if (crypto_verify64(c->expect, b + 1) != 0) return fail(c, PK_F_CODE);
        crypto_wipe(c->expect, 64);
        c->state = ST_PAIR_KEY;
        return PK_EV_NONE;
    }

    if (c->role == PK_PC && c->state == ST_PC_HELLO && t == M_CONN_A) {
        if (L < 49 || L > 49 + PK_NAMEMAX) return fail(c, PK_F_FRAMESIZE);
        take_name(c, b + 49, L - 49);
        if (!c->has_trust) return fail(c, PK_F_NOTRUST);
        cp(c->na, b + 1, 16);
        cp(c->E_peer, b + 17, 32);
        pk_random(c->eph, 32);
        crypto_x25519_public_key(c->E_me, c->eph);
        uint8_t tpc[32], tph[32];
        if (!connect_keys(c, c->E_peer, c->E_me, c->peer_name, c->peer_namelen, tpc, tph))
            return fail(c, PK_F_LOWORDER);
        cp(c->expect, tph, 32);
        uint8_t *o = frame(c, 65);
        o[0] = M_CONN_B; cp(o + 1, c->E_me, 32); cp(o + 33, tpc, 32);
        crypto_wipe(tpc, 32); crypto_wipe(tph, 32); crypto_wipe(c->eph, 32);
        c->state = ST_CONN_C;
        return PK_EV_NONE;
    }

    if (c->role == PK_PHONE && c->state == ST_CONN_B && t == M_CONN_B) {
        if (L != 65) return fail(c, PK_F_FRAMESIZE);
        cp(c->E_peer, b + 1, 32);
        uint8_t tpc[32], tph[32];
        if (!connect_keys(c, c->E_me, c->E_peer, c->my_name, c->my_namelen, tpc, tph))
            return fail(c, PK_F_LOWORDER);
        int ok = crypto_verify32(tpc, b + 33) == 0;
        crypto_wipe(tpc, 32); crypto_wipe(c->eph, 32);
        if (!ok) { crypto_wipe(tph, 32); return fail(c, PK_F_AUTH); }
        uint8_t *o = frame(c, 33);
        o[0] = M_CONN_C; cp(o + 1, tph, 32);
        crypto_wipe(tph, 32);
        c->state = ST_READY;
        return PK_EV_READY;
    }

    if (c->role == PK_PC && c->state == ST_CONN_C && t == M_CONN_C) {
        if (L != 33) return fail(c, PK_F_FRAMESIZE);
        if (crypto_verify32(c->expect, b + 1) != 0) return fail(c, PK_F_AUTH);
        crypto_wipe(c->expect, 64);
        c->state = ST_READY;
        return PK_EV_READY;
    }
    return fail(c, PK_F_UNEXPECTED);
}

// ---------------------------------------------------------------- one encrypted frame
static int on_sealed(PkConn *c, const uint8_t *hdr, const uint8_t *body, int L) {
    if (L < 1 + PK_MAC) return fail(c, PK_F_DECRYPT);
    uint8_t plain[PK_MAXFRAME], nonce[24];
    int n = L - PK_MAC;
    nonce_of(nonce, c->nrx);
    if (crypto_aead_unlock(plain, body + n, c->krx, nonce, hdr, 2, body, n) != 0) return fail(c, PK_F_DECRYPT);
    c->nrx++;
    int t = plain[0], ev = PK_EV_NONE;

    if (c->state == ST_PAIR_KEY) {
        if (n != 33 || t != (c->role == PK_PC ? E_KEY_PHONE : E_KEY_PC)) ev = fail(c, PK_F_BADMSG);
        else {
            cp(c->peer_pk, plain + 1, 32);
            if (c->role == PK_PC) new_static(c);
            if (!peer_key_ok(c)) ev = fail(c, PK_F_LOWORDER);
            else {
                if (c->role == PK_PC) seal(c, E_KEY_PC, c->my_pk, 32);
                c->has_trust = 1;
                c->state = ST_DONE;
                ev = PK_EV_PAIRED;
            }
        }
    } else {                                                   // ST_READY
        int want = c->role == PK_PC ? E_CMD : E_REPLY;
        if (t == want) {
            int tl = n - 1, ok = c->role == PK_PC ? tl >= 1 : tl >= 0;
            for (int i = 1; i < n && ok; i++) if (!plain[i]) ok = 0;   // no NULs inside text
            if (!ok || tl > PK_TEXTMAX) ev = fail(c, PK_F_BADMSG);
            else { cp(c->text, plain + 1, tl); c->text[tl] = 0; c->textlen = tl; ev = t == E_CMD ? PK_EV_CMD : PK_EV_REPLY; }
        } else if (c->role == PK_PC && t == E_PING && n == 1) {
            seal(c, E_PONG, 0, 0);
            ev = PK_EV_PING;
        } else if (c->role == PK_PHONE && t == E_PONG && n == 1) {
            ev = PK_EV_PONG;
        } else ev = fail(c, PK_F_BADMSG);
    }
    crypto_wipe(plain, n);
    return ev;
}

// ---------------------------------------------------------------- feeding bytes
int pk_feed(PkConn *c, const uint8_t *p, int n, int *used) {
    *used = 0;
    if (c->state == ST_FAILED) { *used = n; return PK_EV_FAIL; }
    while (*used < n) {
        if (c->state == ST_DONE) { *used = n; return fail(c, PK_F_AFTERDONE); }
        c->in[c->inlen++] = p[(*used)++];
        if (c->inlen < 2) continue;
        int L = (c->in[0] << 8) | c->in[1];
        if (L < 1 || L > PK_MAXFRAME) { *used = n; return fail(c, PK_F_FRAMESIZE); }   // size before content
        if (c->inlen < 2 + L) continue;
        c->inlen = 0;
        int ev = (c->state == ST_PAIR_KEY || c->state == ST_READY)
               ? on_sealed(c, c->in, c->in + 2, L)
               : on_plain(c, c->in + 2, L);
        crypto_wipe(c->in, 2 + L);
        if (ev == PK_EV_FAIL) *used = n;
        return ev;
    }
    return PK_EV_NONE;
}

// ---------------------------------------------------------------- sending once ready
int pk_send_cmd(PkConn *c, const uint8_t *text, int n) {
    if (c->role != PK_PHONE || c->state != ST_READY || n < 1 || n > PK_TEXTMAX) return 0;
    return seal(c, E_CMD, text, n);
}
int pk_send_reply(PkConn *c, const uint8_t *text, int n) {
    if (c->role != PK_PC || c->state != ST_READY || n < 0 || n > PK_TEXTMAX) return 0;
    return seal(c, E_REPLY, text, n);
}
int pk_send_ping(PkConn *c) {
    if (c->role != PK_PHONE || c->state != ST_READY) return 0;
    return seal(c, E_PING, 0, 0);
}

int  pk_ready(const PkConn *c)       { return c->state == ST_READY; }
int  pk_done(const PkConn *c)        { return c->state == ST_DONE; }
int  pk_mid_pairing(const PkConn *c) { return c->role == PK_PC && (c->state == ST_PAIR_C || c->state == ST_PAIR_KEY); }
void pk_wipe(PkConn *c)              { crypto_wipe(c, sizeof(*c)); }

const char *pk_fail_text(int f) {
    switch (f) {
    case PK_F_FRAMESIZE:  return "a frame with a bad size";
    case PK_F_UNEXPECTED: return "a message out of order";
    case PK_F_VERSION:    return "a different protocol version";
    case PK_F_NOPAIRING:  return "a pairing try while no code was open";
    case PK_F_NOTRUST:    return "a connect try while no phone is trusted";
    case PK_F_LOWORDER:   return "a bad key (low-order point)";
    case PK_F_CODE:       return "the wrong pairing code";
    case PK_F_AUTH:       return "a phone that isn't the trusted one";
    case PK_F_DECRYPT:    return "a frame that failed its check (tampered or replayed)";
    case PK_F_BADMSG:     return "a malformed message";
    case PK_F_AFTERDONE:  return "data after pairing finished";
    }
    return "an unknown problem";
}
