// pocket.h - Pocket Loomy's wire protocol (phone <-> PC over Bluetooth RFCOMM).
// Plain portable C on top of Monocypher, no OS calls, no allocation: Loomy builds it for the PC
// side and the Android app builds the very same file through the NDK for the phone side.
// The full spec (messages, keys, why) is in POCKET-HANDOVER.md, section "Wire protocol".
//
// How to drive it: pk_init_pc / pk_init_phone, then hand every received byte to pk_feed() and
// act on the event it returns. Whatever the engine wants to send sits in c->out / c->outlen:
// send it and set outlen to 0 after every call. On PK_EV_FAIL, close the connection.
#ifndef POCKET_H
#define POCKET_H
#include <stdint.h>

#define PK_VERSION   2          // v2 (Loomy 2.18): + NOTE and FORGET
#define PK_MAXFRAME  512        // body bytes per frame; the 2-byte length prefix comes on top
#define PK_CODELEN   8          // pairing code digits
#define PK_NAMEMAX   48         // device name bytes (UTF-8) carried in the handshakes
#define PK_TEXTMAX   400        // command / reply text bytes
#define PK_MAC       16

enum { PK_PC, PK_PHONE };

// what pk_feed() reports
enum {
    PK_EV_NONE,          // need more bytes (or nothing to report)
    PK_EV_ATTEMPT,       // PC: a phone just used the pairing code (burn it now: one try per code)
    PK_EV_PAIRED,        // pairing done: c->peer_pk + c->peer_name, and our own new c->my_sk/my_pk
    PK_EV_READY,         // a trusted phone is in: commands can flow
    PK_EV_CMD,           // PC: c->text holds a command (UTF-8, 0-terminated)
    PK_EV_REPLY,         // phone: c->text holds the PC's answer
    PK_EV_PING,          // PC: the phone checked in (a PONG is already queued)
    PK_EV_PONG,          // phone: the PC answered a ping
    PK_EV_FAIL,          // c->fail says why; nothing is queued; close the connection
    PK_EV_NOTE,          // PC: c->text holds something the phone couldn't turn into a command (may be empty)
    PK_EV_FORGET         // PC: the phone dropped its pairing; forget it too (nothing more will come)
};

// why it failed (pk_fail_text() turns these into words)
enum {
    PK_F_NONE, PK_F_FRAMESIZE, PK_F_UNEXPECTED, PK_F_VERSION, PK_F_NOPAIRING, PK_F_NOTRUST,
    PK_F_LOWORDER, PK_F_CODE, PK_F_AUTH, PK_F_DECRYPT, PK_F_BADMSG, PK_F_AFTERDONE
};

typedef struct {
    int role, state, fail, flags;
    // set before starting
    uint8_t code[PK_CODELEN]; int has_code;                  // PC: a pairing code is open; phone: the code it heard
    uint8_t my_sk[32], my_pk[32], peer_pk[32]; int has_trust; // a paired peer (PC: the phone; phone: the PC)
    uint8_t my_name[PK_NAMEMAX]; int my_namelen;
    // learned during the handshake (peer_name is what the other side CLAIMS until it authenticates)
    uint8_t peer_name[PK_NAMEMAX]; int peer_namelen;
    // handshake scratch
    uint8_t hp[2];                                           // HELLO's version + flags (bound into the keys)
    uint8_t nb[16], na[16], eph[32], E_me[32], E_peer[32], expect[64];
    // traffic keys, one per direction, with implicit counter nonces
    uint8_t ktx[32], krx[32];
    uint64_t ntx, nrx;
    // one frame being received, the bytes waiting to be sent, the last message's text
    uint8_t in[2 + PK_MAXFRAME]; int inlen;
    uint8_t out[2 * (2 + PK_MAXFRAME)]; int outlen;
    uint8_t text[PK_TEXTMAX + 1]; int textlen;
} PkConn;

// The platform supplies this: cryptographically secure random bytes.
void pk_random(uint8_t *p, int n);

// PC: waits for a phone. code = the 8 open digits or 0; trust = 1 if my_sk/my_pk/peer_pk are a
// paired phone (fill them in before calling). Queues the HELLO.
void pk_init_pc(PkConn *c, const uint8_t *code, int trust, const uint8_t *my_sk, const uint8_t *my_pk,
                const uint8_t *peer_pk, const uint8_t *name, int namelen);
// phone: code = the heard digits to pair, or 0 to connect with the stored trust.
void pk_init_phone(PkConn *c, const uint8_t *code, int trust, const uint8_t *my_sk, const uint8_t *my_pk,
                   const uint8_t *peer_pk, const uint8_t *name, int namelen);

// Eats bytes up to the end of one frame; *used says how many. Call again with the rest.
int  pk_feed(PkConn *c, const uint8_t *p, int n, int *used);

// once READY: queue an encrypted message (1 = queued, 0 = too long or not ready)
int  pk_send_cmd(PkConn *c, const uint8_t *text, int n);   // phone -> PC
int  pk_send_reply(PkConn *c, const uint8_t *text, int n); // PC -> phone
int  pk_send_ping(PkConn *c);                              // phone -> PC
int  pk_send_note(PkConn *c, const uint8_t *text, int n);  // phone -> PC: a failed attempt, for the PC's log
int  pk_send_forget(PkConn *c);                            // phone -> PC: "I forgot you", ends the connection

int  pk_ready(const PkConn *c);
int  pk_done(const PkConn *c);                             // pairing finished, nothing more expected
int  pk_mid_pairing(const PkConn *c);                      // PC: the code was used and pairing isn't done
const char *pk_fail_text(int fail);
void pk_wipe(PkConn *c);

// exposed for the tests (CPace test vectors from draft-irtf-cfrg-cpace)
void pk_elligator(uint8_t u[32], const uint8_t field[32]);   // RFC 9380 map on a 255-bit field element
void pk_cpace_generator(uint8_t g[32], const uint8_t *prs, int prslen, const uint8_t *ci, int cilen,
                        const uint8_t *sid, int sidlen);
void pk_cpace_isk(uint8_t isk[64], const uint8_t *sid, int sidlen, const uint8_t K[32],
                  const uint8_t Ya[32], const uint8_t *ada, int adalen,
                  const uint8_t Yb[32], const uint8_t *adb, int adblen);
#endif
