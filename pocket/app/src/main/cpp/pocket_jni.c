// pocket_jni.c - lets the Kotlin side drive Loomy's protocol engine (src/pocket.c). One PkConn per
// connection, held behind an opaque handle. Everything secret stays in native memory and gets wiped.
#include <jni.h>
#include <stdlib.h>
#include <string.h>
#include "pocket.h"
#include "monocypher.h"

#define FN(name) JNIEXPORT JNICALL Java_io_github_jimmyuns_pocketloomy_Native_##name

// bionic's arc4random_buf: ChaCha20 seeded from the kernel's getrandom, on every Android version
void pk_random(uint8_t *p, int n) { arc4random_buf(p, (size_t)n); }

static PkConn *C(jlong h) { return (PkConn *)(intptr_t)h; }

// copy a Java byte[] into a buffer of exactly `want` bytes (or up to `want` if exact == 0); -1 = wrong size
static int take(JNIEnv *e, jbyteArray a, uint8_t *out, int want, int exact) {
    if (!a) return 0;
    jsize n = (*e)->GetArrayLength(e, a);
    if (n > want || (exact && n != want)) return -1;
    (*e)->GetByteArrayRegion(e, a, 0, n, (jbyte *)out);
    return n;
}

static jbyteArray give(JNIEnv *e, const uint8_t *p, int n) {
    jbyteArray a = (*e)->NewByteArray(e, n);
    if (a && n) (*e)->SetByteArrayRegion(e, a, 0, n, (const jbyte *)p);
    return a;
}

jlong FN(create)(JNIEnv *e, jclass k) {
    PkConn *c = calloc(1, sizeof(PkConn));
    return (jlong)(intptr_t)c;
}

void FN(destroy)(JNIEnv *e, jclass k, jlong h) {
    if (!h) return;
    pk_wipe(C(h));
    free(C(h));
}

// code = the 8 heard digits to pair, or null to connect with the stored keys (sk, pk, pcPk)
jboolean FN(initPhone)(JNIEnv *e, jclass k, jlong h, jbyteArray code, jbyteArray sk, jbyteArray pk,
                       jbyteArray pcPk, jbyteArray name) {
    uint8_t cd[PK_CODELEN], s[32], p[32], q[32], nm[PK_NAMEMAX];
    int hasCode = take(e, code, cd, PK_CODELEN, 1);
    int trust = sk != 0;
    if (hasCode < 0 || (trust && (take(e, sk, s, 32, 1) < 0 || take(e, pk, p, 32, 1) < 0 || take(e, pcPk, q, 32, 1) < 0)))
        return JNI_FALSE;
    int nl = take(e, name, nm, PK_NAMEMAX, 0);
    if (nl < 0) nl = 0;
    pk_init_phone(C(h), hasCode > 0 ? cd : 0, trust, s, p, q, nm, nl);
    crypto_wipe(cd, sizeof cd); crypto_wipe(s, sizeof s);
    return JNI_TRUE;
}

// feeds bytes until one event; returns (used << 8) | event
jlong FN(feed)(JNIEnv *e, jclass k, jlong h, jbyteArray data, jint off, jint len) {
    uint8_t buf[1024];
    if (len > (int)sizeof buf) len = sizeof buf;
    if (len <= 0) return 0;
    (*e)->GetByteArrayRegion(e, data, off, len, (jbyte *)buf);
    int used = 0, ev = pk_feed(C(h), buf, len, &used);
    crypto_wipe(buf, len);
    return ((jlong)used << 8) | (jlong)ev;
}

// what the engine wants sent; clears it
jbyteArray FN(takeOut)(JNIEnv *e, jclass k, jlong h) {
    PkConn *c = C(h);
    jbyteArray a = give(e, c->out, c->outlen);
    crypto_wipe(c->out, c->outlen);
    c->outlen = 0;
    return a;
}

jbyteArray FN(text)(JNIEnv *e, jclass k, jlong h)     { return give(e, C(h)->text, C(h)->textlen); }
jbyteArray FN(peerName)(JNIEnv *e, jclass k, jlong h) { return give(e, C(h)->peer_name, C(h)->peer_namelen); }
jint FN(fail)(JNIEnv *e, jclass k, jlong h)           { return C(h)->fail; }
jstring FN(failText)(JNIEnv *e, jclass k, jint f)     { return (*e)->NewStringUTF(e, pk_fail_text(f)); }
jboolean FN(ready)(JNIEnv *e, jclass k, jlong h)      { return pk_ready(C(h)) ? JNI_TRUE : JNI_FALSE; }

// after PK_EV_PAIRED: this phone's new key pair (sk || pk) and the PC's public key
jbyteArray FN(myKeys)(JNIEnv *e, jclass k, jlong h) {
    uint8_t b[64];
    memcpy(b, C(h)->my_sk, 32); memcpy(b + 32, C(h)->my_pk, 32);
    jbyteArray a = give(e, b, 64);
    crypto_wipe(b, 64);
    return a;
}
jbyteArray FN(peerKey)(JNIEnv *e, jclass k, jlong h) { return give(e, C(h)->peer_pk, 32); }

jboolean FN(sendCmd)(JNIEnv *e, jclass k, jlong h, jbyteArray text) {
    uint8_t t[PK_TEXTMAX];
    int n = take(e, text, t, PK_TEXTMAX, 0);
    return n > 0 && pk_send_cmd(C(h), t, n) ? JNI_TRUE : JNI_FALSE;
}
jboolean FN(sendPing)(JNIEnv *e, jclass k, jlong h) { return pk_send_ping(C(h)) ? JNI_TRUE : JNI_FALSE; }
jboolean FN(sendNote)(JNIEnv *e, jclass k, jlong h, jbyteArray text) {
    uint8_t t[PK_TEXTMAX];
    int n = take(e, text, t, PK_TEXTMAX, 0);
    return n >= 0 && pk_send_note(C(h), t, n) ? JNI_TRUE : JNI_FALSE;
}
jboolean FN(sendForget)(JNIEnv *e, jclass k, jlong h) { return pk_send_forget(C(h)) ? JNI_TRUE : JNI_FALSE; }
