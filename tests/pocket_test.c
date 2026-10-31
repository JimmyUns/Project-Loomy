// pocket_test.c - Pocket Loomy's protocol on Linux: the CPace test vectors from
// draft-irtf-cfrg-cpace (CPACE-X25519-SHA512), then a real PC engine against a real phone engine,
// then hostile input: tampered, replayed, reordered, oversized and random frames in every state.
// Built with AddressSanitizer + UBSan by run.sh, so a bad read anywhere fails the run.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../src/pocket.h"
#include "../src/monocypher.h"
#include "../src/monocypher-ed25519.h"

static int fails, checks;
#define EXPECT(cond, msg) do { checks++; if (!(cond)) { printf("!! FAIL: %s (line %d)\n", msg, __LINE__); fails++; } } while (0)

// ---- deterministic randomness for the engines (ChaCha20 keystream from a seed)
static uint8_t g_seed[32]; static uint64_t g_ctr;
void pk_random(uint8_t *p, int n) {
    static const uint8_t nonce[8] = {0};
    memset(p, 0, n);
    g_ctr = crypto_chacha20_djb(p, p, n, g_seed, nonce, g_ctr);   // whole 64-byte blocks advance
}
static void seed(int s) { memset(g_seed, 0, 32); memcpy(g_seed, &s, sizeof s); g_ctr = 0; }
static uint32_t rnd(void) { uint8_t b[4]; pk_random(b, 4); return b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24; }

static int unhex(const char *h, uint8_t *o) {
    int n = 0;
    while (h[0] && h[1]) { unsigned v; sscanf(h, "%2x", &v); o[n++] = (uint8_t)v; h += 2; }
    return n;
}
static int eqhex(const uint8_t *p, const char *h) { uint8_t b[256]; int n = unhex(h, b); return memcmp(p, b, n) == 0; }

// ---------------------------------------------------------------- CPace vectors
static void vectors(void) {
    printf("== CPace test vectors (CPACE-X25519-SHA512) ==\n");
    uint8_t ci[64], sid[16], g[32];
    int cil = unhex("0b415f696e69746961746f720b425f726573706f6e646572", ci);
    unhex("7e4b4791d6a8ef019b936c79fb7f2c57", sid);
    pk_cpace_generator(g, (const uint8_t *)"Password", 8, ci, cil, sid, 16);
    EXPECT(eqhex(g, "d04bf6d41f6a289632a2e929fa29bebd51092512a7829fdde7d314b62f05a73f"), "generator g");

    uint8_t ya[32], yb[32], Ya[32], Yb[32], K1[32], K2[32], isk[64];
    unhex("21b4f4bd9e64ed355c3eb676a28ebedaf6d8f17bdc365995b319097153044080", ya);
    unhex("848b0779ff415f0af4ea14df9dd1d3c29ac41d836c7808896c4eba19c51ac40a", yb);
    crypto_x25519(Ya, ya, g);
    crypto_x25519(Yb, yb, g);
    EXPECT(eqhex(Ya, "1d13c89278cdadd826f6d8d7f887701430f8380ddc17611cdd6dc989ce0c9f32"), "Ya");
    EXPECT(eqhex(Yb, "248cccf6d5cdc3646f0ad593f9e6cef4e69d4945f8372e623512ecea32185623"), "Yb");
    crypto_x25519(K1, ya, Yb);
    crypto_x25519(K2, yb, Ya);
    EXPECT(eqhex(K1, "5b067effbdc0b2a0e1d907b21ebb25cfedb96a852179a847c37e43ee71322c6b") && !memcmp(K1, K2, 32), "K both ways");
    pk_cpace_isk(isk, sid, 16, K1, Ya, (const uint8_t *)"ADa", 3, Yb, (const uint8_t *)"ADb", 3);
    EXPECT(eqhex(isk, "6e19b875f7a561d6b3ca3dbb9ef42ac55de3e717881018204b8922b4d5e53bb2"
                      "aa82c300bea7b65d2b671da71922ddf6472301b79bc270adfa8bf413285f2263"), "ISK (initiator/responder)");

    // Elligator 2 on field elements Monocypher's 254-bit map can't take directly (bit 254 set,
    // r >= p, all-ones): expected values from a plain big-integer RFC 9380 reference in Python
    static const char *ELL[][2] = {
        { "f2ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f", "e967a8afafafafafafafafafafafafafafafafafafafafafafafafafafafaf2f" },
        { "edffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f", "0000000000000000000000000000000000000000000000000000000000000000" },
        { "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff", "1e5942dd97c756040d27755f1e5b11349cd47d796c45d07052f7e5b11541c349" },
        { "0000000000000000000000000000000000000000000000000000000000000040", "bcf24d5555555555555555555555555555555555555555555555555555555555" },
        { "3930000000000000000000000000000000000000000000000000000000000040", "a881c3c85991247af5c0a2546e2e69c064befe33eab68a1b513a580a73e63952" },
        { "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff3f", "80e5132b658f7f451b2b658f7f451b2b658f7f451b2b658f7f451b2b658f7f45" },
        { "0500000000000000000000000000000000000000000000000000000000000000", "e967a8afafafafafafafafafafafafafafafafafafafafafafafafafafafaf2f" },
        { "38b4e652e44da7f2370d9e260e27136550a4a3a6d07f5c0c332f8b1212841f69", "5270033717b021fc57c0ee1a1582ee55836063e1b0d3b4ec6b38e20d914cdf18" },
        { "2b902f8911e81818f8c99d5d5d9831957504d90e945de2e8f54ee781e63a7b5b", "a686d921c46051b387265c76e359eae2e20e3184fddb0fa2141ac11b54f7902f" },
        { "d85099095aa300165a67036f9b540d6b8f0be21124179c3dd9f7381767b78846", "bd0768f2a8aa25a33e77508a38e85070f303ee0747a32107681bdc36cd4ae56e" },
        { "264aad6cb6dd210faf94acd3cf92c190237cb11f5d108cf2593026399c59b850", "5190e75f6c4e1cc98d1128ffab2e3e199b65ed82d07296fee88363529f69ac04" } };
    for (int i = 0; i < 11; i++) {
        uint8_t r[32], u2[32]; char m[40];
        unhex(ELL[i][0], r);
        pk_elligator(u2, r);
        snprintf(m, 40, "elligator 2 reference #%d", i);
        EXPECT(eqhex(u2, ELL[i][1]), m);
    }

    printf("== low-order points (scalar_mult_vfy) ==\n");
    static const char *U[12] = {
        "0000000000000000000000000000000000000000000000000000000000000000",
        "0100000000000000000000000000000000000000000000000000000000000000",
        "ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
        "e0eb7a7c3b41b8ae1656e3faf19fc46ada098deb9c32b1fd866205165f49b800",
        "5f9c95bca3508c24b1d0b1559c83ef5b04445cc4581c8e86d8224eddd09f1157",
        "edffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
        "daffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
        "eeffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
        "dbffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
        "d9ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
        "cdeb7a7c3b41b8ae1656e3faf19fc46ada098deb9c32b1fd866205165f49b880",
        "4c9c95bca3508c24b1d0b1559c83ef5b04445cc4581c8e86d8224eddd09f11d7" };
    static const char *Q[12] = {
        "0000000000000000000000000000000000000000000000000000000000000000",
        "0000000000000000000000000000000000000000000000000000000000000000",
        "0000000000000000000000000000000000000000000000000000000000000000",
        "0000000000000000000000000000000000000000000000000000000000000000",
        "0000000000000000000000000000000000000000000000000000000000000000",
        "0000000000000000000000000000000000000000000000000000000000000000",
        "d8e2c776bbacd510d09fd9278b7edcd25fc5ae9adfba3b6e040e8d3b71b21806",
        "0000000000000000000000000000000000000000000000000000000000000000",
        "c85c655ebe8be44ba9c0ffde69f2fe10194458d137f09bbff725ce58803cdb38",
        "db64dafa9b8fdd136914e61461935fe92aa372cb056314e1231bc4ec12417456",
        "e062dcd5376d58297be2618c7498f55baa07d7e03184e8aada20bca28888bf7a",
        "993c6ad11c4c29da9a56f7691fd0ff8d732e49de6250b6c2e80003ff4629a175" };
    uint8_t s[32], u[32], q[32];
    unhex("af46e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449aff", s);
    for (int i = 0; i < 12; i++) {
        unhex(U[i], u);
        crypto_x25519(q, s, u);
        char m[40]; snprintf(m, 40, "q%x", i);
        EXPECT(eqhex(q, Q[i]), m);
    }
}

// ---------------------------------------------------------------- two engines talking
// pump bytes from one side's out buffer into the other, in random-sized chunks (Bluetooth
// splits however it likes); returns the event the receiver reported last
static int deliver(PkConn *from, PkConn *to, int *ev2) {
    int ev = PK_EV_NONE, off = 0, n = from->outlen;
    uint8_t buf[sizeof from->out];
    memcpy(buf, from->out, n);
    from->outlen = 0;
    *ev2 = PK_EV_NONE;
    while (off < n) {
        int chunk = 1 + (int)(rnd() % 40);
        if (chunk > n - off) chunk = n - off;
        while (chunk > 0) {
            int used, e = pk_feed(to, buf + off, chunk, &used);
            off += used; chunk -= used;
            if (e != PK_EV_NONE) { if (ev != PK_EV_NONE) *ev2 = e; else ev = e; }
            if (e == PK_EV_FAIL) return ev;
        }
    }
    return ev;
}

static const uint8_t CODE[8] = { '4','8','2','1','0','9','3','7' };
static const uint8_t BADCODE[8] = { '4','8','2','1','0','9','3','8' };
typedef struct { uint8_t pc_sk[32], pc_pk[32], ph_sk[32], ph_pk[32]; } Trust;

static int pair(const uint8_t *pccode, const uint8_t *phcode, Trust *t, int *pcfail, int *phfail) {
    PkConn pc, ph; int e2;
    pk_init_pc(&pc, pccode, 0, 0, 0, 0, (const uint8_t *)"JIMMY-PC", 8);
    pk_init_phone(&ph, phcode, 0, 0, 0, 0, (const uint8_t *)"Pixel 8", 7);
    int e = deliver(&pc, &ph, &e2);              // HELLO -> PAIR_A
    if (e == PK_EV_FAIL) { *phfail = ph.fail; return 0; }
    e = deliver(&ph, &pc, &e2);                  // PAIR_A -> PAIR_B
    if (e == PK_EV_FAIL) { *pcfail = pc.fail; return 0; }
    EXPECT(e == PK_EV_ATTEMPT && pk_mid_pairing(&pc), "PC reports the code being used");
    EXPECT(pc.peer_namelen == 7 && !memcmp(pc.peer_name, "Pixel 8", 7), "PC sees the claimed name");
    e = deliver(&pc, &ph, &e2);                  // PAIR_B -> PAIR_C + KEY
    if (e == PK_EV_FAIL) { *phfail = ph.fail; return 0; }
    e = deliver(&ph, &pc, &e2);                  // PAIR_C + KEY -> KEY
    if (e == PK_EV_FAIL || e2 == PK_EV_FAIL) { *pcfail = pc.fail; return 0; }
    EXPECT(e == PK_EV_PAIRED && pk_done(&pc), "PC paired");
    e = deliver(&pc, &ph, &e2);
    EXPECT(e == PK_EV_PAIRED && pk_done(&ph), "phone paired");
    EXPECT(!memcmp(pc.peer_pk, ph.my_pk, 32) && !memcmp(ph.peer_pk, pc.my_pk, 32), "both hold each other's keys");
    EXPECT(ph.peer_namelen == 8 && !memcmp(ph.peer_name, "JIMMY-PC", 8), "phone sees the PC's name");
    memcpy(t->pc_sk, pc.my_sk, 32); memcpy(t->pc_pk, pc.my_pk, 32);
    memcpy(t->ph_sk, ph.my_sk, 32); memcpy(t->ph_pk, ph.my_pk, 32);
    return 1;
}

static int connect(const Trust *pct, const Trust *pht, PkConn *pc, PkConn *ph) {
    int e2;
    pk_init_pc(pc, 0, 1, pct->pc_sk, pct->pc_pk, pct->ph_pk, (const uint8_t *)"JIMMY-PC", 8);
    pk_init_phone(ph, 0, 1, pht->ph_sk, pht->ph_pk, pht->pc_pk, (const uint8_t *)"Pixel 8", 7);
    if (deliver(pc, ph, &e2) == PK_EV_FAIL) return -1;
    if (deliver(ph, pc, &e2) == PK_EV_FAIL) return -2;
    if (deliver(pc, ph, &e2) != PK_EV_READY) return -3;
    if (deliver(ph, pc, &e2) != PK_EV_READY) return -4;
    return 1;
}

// run a recorded transcript: which messages go which way, so single bytes can be tampered
typedef struct { uint8_t b[600]; int n, toPc; } Msg;
static int record_pairing(Msg *m) {
    PkConn pc, ph; int k = 0, e2;
    pk_init_pc(&pc, CODE, 0, 0, 0, 0, (const uint8_t *)"PC", 2);
    pk_init_phone(&ph, CODE, 0, 0, 0, 0, (const uint8_t *)"ph", 2);
    for (int step = 0; step < 4; step++) {
        PkConn *from = step % 2 ? &ph : &pc, *to = step % 2 ? &pc : &ph;
        memcpy(m[k].b, from->out, from->outlen); m[k].n = from->outlen; m[k].toPc = step % 2; k++;
        deliver(from, to, &e2);
    }
    return k;
}

static void engines(void) {
    printf("== pairing + connecting, engine against engine ==\n");
    Trust t; int pf = 0, hf = 0;
    seed(1);
    EXPECT(pair(CODE, CODE, &t, &pf, &hf) == 1, "pairing with the right code");

    seed(2); pf = hf = 0;
    EXPECT(pair(CODE, BADCODE, &t, &pf, &hf) == 0 && hf == PK_F_CODE, "wrong code: the phone sees the PC's tag fail");
    // and a phone that lies about the PC's tag (sends PAIR_C anyway) can't pass the PC's check:
    {
        seed(3);
        PkConn pc, ph; int e2;
        pk_init_pc(&pc, CODE, 0, 0, 0, 0, (const uint8_t *)"PC", 2);
        pk_init_phone(&ph, BADCODE, 0, 0, 0, 0, (const uint8_t *)"x", 1);
        deliver(&pc, &ph, &e2); deliver(&ph, &pc, &e2);
        uint8_t fake[67] = { 0, 65, 0x04 };
        for (int i = 3; i < 67; i++) fake[i] = (uint8_t)rnd();
        int used, e = pk_feed(&pc, fake, 67, &used);
        EXPECT(e == PK_EV_FAIL && pc.fail == PK_F_CODE, "a guessed confirmation tag fails on the PC");
    }
    seed(4); pf = hf = 0;
    EXPECT(pair(0, CODE, &t, &pf, &hf) == 0 && hf == PK_F_NOPAIRING, "no code open: the phone stops before sending anything");
    {   // a phone that ignores the flag and sends PAIR_A anyway
        seed(5);
        PkConn pc; pk_init_pc(&pc, 0, 0, 0, 0, 0, 0, 0); pc.outlen = 0;
        uint8_t m[2 + 49] = { 0, 49, 0x02 };
        int used, e = pk_feed(&pc, m, sizeof m, &used);
        EXPECT(e == PK_EV_FAIL && pc.fail == PK_F_NOPAIRING, "PAIR_A with no open code is refused");
    }

    seed(6);
    Trust good; pf = hf = 0;
    EXPECT(pair(CODE, CODE, &good, &pf, &hf) == 1, "pair for the connect tests");
    PkConn pc, ph; int e2;
    EXPECT(connect(&good, &good, &pc, &ph) == 1, "trusted phone connects");
    EXPECT(pk_send_cmd(&ph, (const uint8_t *)"web gmail", 9), "phone sends a command");
    EXPECT(deliver(&ph, &pc, &e2) == PK_EV_CMD && !strcmp((char *)pc.text, "web gmail"), "PC gets it");
    EXPECT(pk_send_reply(&pc, (const uint8_t *)"Opening gmail", 13), "PC replies");
    EXPECT(deliver(&pc, &ph, &e2) == PK_EV_REPLY && !strcmp((char *)ph.text, "Opening gmail"), "phone gets the reply");
    EXPECT(pk_send_ping(&ph) && deliver(&ph, &pc, &e2) == PK_EV_PING, "ping");
    EXPECT(deliver(&pc, &ph, &e2) == PK_EV_PONG, "pong");
    EXPECT(pk_send_reply(&pc, (const uint8_t *)"", 0) && deliver(&pc, &ph, &e2) == PK_EV_REPLY && ph.textlen == 0, "empty reply is fine");
    EXPECT(pk_send_note(&ph, (const uint8_t *)"banana split", 12) && deliver(&ph, &pc, &e2) == PK_EV_NOTE && !strcmp((char *)pc.text, "banana split"), "note (a failed attempt) arrives");
    EXPECT(pk_send_note(&ph, (const uint8_t *)"", 0) && deliver(&ph, &pc, &e2) == PK_EV_NOTE && pc.textlen == 0, "empty note is fine");
    EXPECT(!pk_send_note(&pc, (const uint8_t *)"x", 1), "PC can't send notes");
    uint8_t big[PK_TEXTMAX + 1]; memset(big, 'a', sizeof big);
    EXPECT(!pk_send_cmd(&ph, big, PK_TEXTMAX + 1) && pk_send_cmd(&ph, big, PK_TEXTMAX), "text cap");
    EXPECT(deliver(&ph, &pc, &e2) == PK_EV_CMD && pc.textlen == PK_TEXTMAX, "max-size command arrives");
    EXPECT(!pk_send_cmd(&pc, (const uint8_t *)"x", 1) && !pk_send_reply(&ph, (const uint8_t *)"x", 1), "roles can't send each other's messages");

    {   // forget: ends the connection on both sides; anything after it is refused
        PkConn a, b; connect(&good, &good, &a, &b);
        EXPECT(pk_send_forget(&b) && pk_done(&b) && !pk_send_cmd(&b, (const uint8_t *)"rb", 2), "phone: forget ends its side");
        EXPECT(deliver(&b, &a, &e2) == PK_EV_FORGET && pk_done(&a), "PC hears it");
        uint8_t junk[3] = { 0, 1, 0x22 }; int used;
        EXPECT(pk_feed(&a, junk, 3, &used) == PK_EV_FAIL && a.fail == PK_F_AFTERDONE, "nothing after forget");
    }
    {   // a v1 phone meets a v2 PC: the phone stops before sending anything
        PkConn a, b; pk_init_pc(&a, CODE, 0, 0, 0, 0, 0, 0); pk_init_phone(&b, CODE, 0, 0, 0, 0, 0, 0);
        a.out[3] = 1; int used;
        EXPECT(pk_feed(&b, a.out, a.outlen, &used) == PK_EV_FAIL && b.fail == PK_F_VERSION && b.outlen == 0, "old version refused cleanly");
    }
    {   // replay: the same sealed frame twice
        pk_send_cmd(&ph, (const uint8_t *)"rb", 2);
        uint8_t copy[600]; int n = ph.outlen; memcpy(copy, ph.out, n);
        EXPECT(deliver(&ph, &pc, &e2) == PK_EV_CMD, "first copy ok");
        int used, e = pk_feed(&pc, copy, n, &used);
        EXPECT(e == PK_EV_FAIL && pc.fail == PK_F_DECRYPT, "replayed frame rejected");
    }
    {   // reorder: two frames swapped
        connect(&good, &good, &pc, &ph);
        pk_send_cmd(&ph, (const uint8_t *)"one", 3); int n1 = ph.outlen;
        pk_send_cmd(&ph, (const uint8_t *)"two", 3);
        uint8_t sw[600]; int n = ph.outlen;
        memcpy(sw, ph.out + n1, n - n1); memcpy(sw + (n - n1), ph.out, n1); ph.outlen = 0;
        int used, e = pk_feed(&pc, sw, n, &used);
        EXPECT(e == PK_EV_FAIL && pc.fail == PK_F_DECRYPT, "reordered frames rejected");
    }
    {   // a frame that changes the length prefix (AD) but keeps the body
        connect(&good, &good, &pc, &ph);
        pk_send_cmd(&ph, (const uint8_t *)"rb", 2);
        uint8_t f[600]; int n = ph.outlen; memcpy(f, ph.out, n); ph.outlen = 0;
        f[n] = 0; f[1]++; n++;                       // claims one more byte, pads one
        int used, e = pk_feed(&pc, f, n, &used);
        EXPECT(e == PK_EV_FAIL && pc.fail == PK_F_DECRYPT, "length prefix is authenticated");
    }
    {   // wrong trust: a phone with another key pair, or the right phone with an old PC key
        Trust other; seed(7); pf = hf = 0;
        pair(CODE, CODE, &other, &pf, &hf);
        Trust mix = good; memcpy(mix.ph_sk, other.ph_sk, 32); memcpy(mix.ph_pk, other.ph_pk, 32);
        EXPECT(connect(&good, &mix, &pc, &ph) == -3 && ph.fail == PK_F_AUTH, "unknown phone can't verify the PC's tag");
        {   // and if it just sends a made-up confirmation, the PC refuses it
            pk_init_pc(&pc, 0, 1, good.pc_sk, good.pc_pk, good.ph_pk, 0, 0);
            pk_init_phone(&ph, 0, 1, mix.ph_sk, mix.ph_pk, mix.pc_pk, 0, 0);
            deliver(&pc, &ph, &e2); deliver(&ph, &pc, &e2); pc.outlen = 0;
            uint8_t fake[35] = { 0, 33, 0x07 };
            for (int i = 3; i < 35; i++) fake[i] = (uint8_t)rnd();
            int used, e = pk_feed(&pc, fake, 35, &used);
            EXPECT(e == PK_EV_FAIL && pc.fail == PK_F_AUTH, "unknown phone: PC refuses its confirmation");
        }
        Trust old = good; memcpy(old.pc_pk, other.pc_pk, 32);
        int r = connect(&good, &old, &pc, &ph);
        EXPECT(r == -3 && ph.fail == PK_F_AUTH, "phone with a stale PC key refuses the PC's tag");
        // PC with no trusted phone at all
        PkConn p2, h2; pk_init_pc(&p2, 0, 0, 0, 0, 0, 0, 0);
        pk_init_phone(&h2, 0, 1, good.ph_sk, good.ph_pk, good.pc_pk, 0, 0);
        EXPECT(deliver(&p2, &h2, &e2) == PK_EV_FAIL && h2.fail == PK_F_NOTRUST, "phone sees nobody is trusted");
    }
    {   // after pairing, nothing else may come in
        seed(8);
        PkConn a, b; int k;
        pk_init_pc(&a, CODE, 0, 0, 0, 0, 0, 0); pk_init_phone(&b, CODE, 0, 0, 0, 0, 0, 0);
        for (k = 0; k < 4; k++) deliver(k % 2 ? &b : &a, k % 2 ? &a : &b, &e2);
        EXPECT(pk_done(&a), "paired");
        uint8_t junk[3] = { 0, 1, 0x22 };
        int used, e = pk_feed(&a, junk, 3, &used);
        EXPECT(e == PK_EV_FAIL && a.fail == PK_F_AFTERDONE, "bytes after pairing end it");
    }
}

static void hostile(void) {
    printf("== hostile input ==\n");
    int e2, used, e;
    PkConn pc;
    // size before content
    uint8_t zero[2] = { 0, 0 }, huge[2] = { 0x02, 0x01 }, max[2] = { 0xff, 0xff };
    pk_init_pc(&pc, CODE, 0, 0, 0, 0, 0, 0); e = pk_feed(&pc, zero, 2, &used);
    EXPECT(e == PK_EV_FAIL && pc.fail == PK_F_FRAMESIZE && pc.outlen == 0, "length 0 refused");
    pk_init_pc(&pc, CODE, 0, 0, 0, 0, 0, 0); e = pk_feed(&pc, huge, 2, &used);
    EXPECT(e == PK_EV_FAIL && pc.fail == PK_F_FRAMESIZE, "length 513 refused after 2 bytes");
    pk_init_pc(&pc, CODE, 0, 0, 0, 0, 0, 0); e = pk_feed(&pc, max, 2, &used);
    EXPECT(e == PK_EV_FAIL && pc.fail == PK_F_FRAMESIZE, "length 65535 refused after 2 bytes");
    {   // a frame of the right type but wrong size, at each handshake step
        uint8_t m[2 + 100] = { 0, 48, 0x02 };    // PAIR_A one byte short
        pk_init_pc(&pc, CODE, 0, 0, 0, 0, 0, 0); e = pk_feed(&pc, m, 50, &used);
        EXPECT(e == PK_EV_FAIL && pc.fail == PK_F_FRAMESIZE, "short PAIR_A");
        m[1] = 49 + PK_NAMEMAX + 1;
        pk_init_pc(&pc, CODE, 0, 0, 0, 0, 0, 0); e = pk_feed(&pc, m, 2 + m[1], &used);
        EXPECT(e == PK_EV_FAIL && pc.fail == PK_F_FRAMESIZE, "PAIR_A with an overlong name");
        uint8_t h[2 + 19] = { 0, 19, 0x01, 9 };
        PkConn ph; pk_init_phone(&ph, CODE, 0, 0, 0, 0, 0, 0); e = pk_feed(&ph, h, 21, &used);
        EXPECT(e == PK_EV_FAIL && ph.fail == PK_F_VERSION, "phone refuses another version");
        uint8_t w[3] = { 0, 1, 0x07 };
        pk_init_pc(&pc, CODE, 0, 0, 0, 0, 0, 0); e = pk_feed(&pc, w, 3, &used);
        EXPECT(e == PK_EV_FAIL && pc.fail == PK_F_UNEXPECTED, "message out of order");
    }
    {   // low-order shares in a real run: u0..u5 and u7 must abort (the rest just fail the tag)
        static const char *U[] = {
            "0000000000000000000000000000000000000000000000000000000000000000",
            "0100000000000000000000000000000000000000000000000000000000000000",
            "ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
            "e0eb7a7c3b41b8ae1656e3faf19fc46ada098deb9c32b1fd866205165f49b800",
            "5f9c95bca3508c24b1d0b1559c83ef5b04445cc4581c8e86d8224eddd09f1157",
            "edffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
            "eeffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f" };
        for (int i = 0; i < 7; i++) {
            seed(100 + i);
            PkConn ph; pk_init_pc(&pc, CODE, 0, 0, 0, 0, 0, 0);
            pk_init_phone(&ph, CODE, 0, 0, 0, 0, 0, 0);
            deliver(&pc, &ph, &e2);
            unhex(U[i], ph.out + 2 + 17);                // swap the phone's Ya for a low-order point
            e = deliver(&ph, &pc, &e2);
            char msg[60]; snprintf(msg, 60, "PC aborts on low-order Ya #%d", i);
            EXPECT(e == PK_EV_FAIL && pc.fail == PK_F_LOWORDER, msg);
            // and the phone aborts on a low-order Yb
            seed(200 + i);
            pk_init_pc(&pc, CODE, 0, 0, 0, 0, 0, 0); pk_init_phone(&ph, CODE, 0, 0, 0, 0, 0, 0);
            deliver(&pc, &ph, &e2); deliver(&ph, &pc, &e2);
            unhex(U[i], pc.out + 2 + 1);
            e = deliver(&pc, &ph, &e2);
            snprintf(msg, 60, "phone aborts on low-order Yb #%d", i);
            EXPECT(e == PK_EV_FAIL && ph.fail == PK_F_LOWORDER, msg);
        }
        // low-order ephemeral in a connect
        Trust t; int pf = 0, hf = 0; seed(300); pair(CODE, CODE, &t, &pf, &hf);
        PkConn ph;
        pk_init_pc(&pc, 0, 1, t.pc_sk, t.pc_pk, t.ph_pk, 0, 0);
        pk_init_phone(&ph, 0, 1, t.ph_sk, t.ph_pk, t.pc_pk, 0, 0);
        deliver(&pc, &ph, &e2);
        memset(ph.out + 2 + 17, 0, 32);
        e = deliver(&ph, &pc, &e2);
        EXPECT(e == PK_EV_FAIL && pc.fail == PK_F_LOWORDER, "connect: low-order Ea aborts");
    }
    {   // flip every bit of every recorded pairing message: the run must fail, never pair
        Msg m[4];
        seed(400);
        int nm = record_pairing(m), bad = 0, total = 0;
        for (int i = 0; i < nm; i++)
            for (int byte = 0; byte < m[i].n; byte++)
                for (int bit = 0; bit < 8; bit++) {
                    seed(400);                           // same randomness: same transcript up to the flip
                    PkConn a, b;
                    pk_init_pc(&a, CODE, 0, 0, 0, 0, (const uint8_t *)"PC", 2);
                    pk_init_phone(&b, CODE, 0, 0, 0, 0, (const uint8_t *)"ph", 2);
                    int paired = 0, failed = 0;
                    for (int k = 0; k < nm && !failed; k++) {
                        PkConn *from = k % 2 ? &b : &a, *to = k % 2 ? &a : &b;
                        if (k == i) from->out[byte] ^= (uint8_t)(1 << bit);
                        int ev = deliver(from, to, &e2);
                        if (ev == PK_EV_FAIL || e2 == PK_EV_FAIL) failed = 1;
                        if (ev == PK_EV_PAIRED && to == &a) paired = 1;
                    }
                    // every byte is covered by the CPace transcript, the tags or the AEAD,
                    // so no single flipped bit may leave the PC paired
                    if (paired) { bad++; printf("   got through: msg %d byte %d bit %d (len %d)\n", i, byte, bit, m[i].n); }
                    total++;
                }
        printf("   %d tampered pairing runs, %d got through\n", total, bad);
        EXPECT(bad == 0, "no tampered pairing run succeeds");
    }
    {   // flip every bit of a recorded connect: the PC must never reach READY
        Trust t; int pf = 0, hf = 0; seed(450); pair(CODE, CODE, &t, &pf, &hf);
        int bad = 0, total = 0;
        for (int i = 0; i < 4; i++)
            for (int byte = 0; byte < 70; byte++)
                for (int bit = 0; bit < 8; bit++) {
                    seed(451);
                    PkConn a, b;
                    pk_init_pc(&a, 0, 1, t.pc_sk, t.pc_pk, t.ph_pk, 0, 0);
                    pk_init_phone(&b, 0, 1, t.ph_sk, t.ph_pk, t.pc_pk, (const uint8_t *)"ph", 2);
                    int ready = 0, ok = 1;
                    for (int k = 0; k < 4 && ok; k++) {
                        PkConn *from = k % 2 ? &b : &a, *to = k % 2 ? &a : &b;
                        if (k == i) { if (byte >= from->outlen) { ok = 0; break; } from->out[byte] ^= (uint8_t)(1 << bit); }
                        int ev = deliver(from, to, &e2);
                        if (ev == PK_EV_FAIL) ok = 0;
                        if (ev == PK_EV_READY && to == &a) ready = 1;
                    }
                    if (byte < 70 && ready) bad++;
                    total++;
                }
        printf("   %d tampered connects, %d got through\n", total, bad);
        EXPECT(bad == 0, "no tampered connect succeeds");
    }
    {   // random garbage into a PC engine in every state, thousands of times
        Trust t; int pf = 0, hf = 0; seed(500); pair(CODE, CODE, &t, &pf, &hf);
        int wins = 0;
        for (int iter = 0; iter < 20000; iter++) {
            seed(1000 + iter);
            PkConn a, b;
            int stage = iter % 5;
            if (stage < 3) { pk_init_pc(&a, CODE, 0, 0, 0, 0, 0, 0); pk_init_phone(&b, CODE, 0, 0, 0, 0, 0, 0); }
            else { pk_init_pc(&a, 0, 1, t.pc_sk, t.pc_pk, t.ph_pk, 0, 0); pk_init_phone(&b, 0, 1, t.ph_sk, t.ph_pk, t.pc_pk, 0, 0); }
            // walk an honest run to some point first
            int steps = stage == 0 ? 0 : stage == 1 ? 2 : stage == 2 ? 4 : stage == 3 ? 0 : 4;
            for (int k = 0; k < steps; k++) deliver(k % 2 ? &b : &a, k % 2 ? &a : &b, &e2);
            a.outlen = 0;
            uint8_t junk[700]; int n = 1 + (int)(rnd() % 700);
            for (int i = 0; i < n; i++) junk[i] = (uint8_t)rnd();
            if (rnd() % 2) { int L = 1 + (int)(rnd() % PK_MAXFRAME); junk[0] = (uint8_t)(L >> 8); junk[1] = (uint8_t)L; }  // plausible length
            if (rnd() % 2 && n > 2) junk[2] = (uint8_t)(1 + rnd() % 7);                                            // plausible type
            int off = 0;
            while (off < n) {
                int ev = pk_feed(&a, junk + off, n - off, &used);
                off += used;
                if (ev == PK_EV_PAIRED || ev == PK_EV_READY || ev == PK_EV_CMD || ev == PK_EV_PING) wins++;
                if (ev == PK_EV_FAIL) break;
            }
        }
        printf("   20000 random frames: %d accepted\n", wins);
        EXPECT(wins == 0, "random bytes never pair, connect or run a command");
    }
}

int main(void) {
    vectors();
    engines();
    hostile();
    printf("\n%d checks, %d failure(s)\n", checks, fails);
    return fails != 0;
}
