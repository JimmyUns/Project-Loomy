// TestPc.c - a PC-role engine for the desktop JNI test (tests/jni/run.sh). Not shipped anywhere.
#include <jni.h>
#include <stdlib.h>
#include "pocket.h"
#define FN(n) JNIEXPORT JNICALL Java_TestPc_##n
void pk_random(uint8_t *p, int n) { arc4random_buf(p, (size_t)n); }
static PkConn *C(jlong h) { return (PkConn *)(intptr_t)h; }
jlong FN(create)(JNIEnv *e, jclass k) { return (jlong)(intptr_t)calloc(1, sizeof(PkConn)); }
void FN(initPc)(JNIEnv *e, jclass k, jlong h, jbyteArray code, jbyteArray sk, jbyteArray pk, jbyteArray phonePk) {
    uint8_t c[8], s[32], p[32], q[32];
    if (code) (*e)->GetByteArrayRegion(e, code, 0, 8, (jbyte *)c);
    if (sk) { (*e)->GetByteArrayRegion(e, sk, 0, 32, (jbyte *)s); (*e)->GetByteArrayRegion(e, pk, 0, 32, (jbyte *)p);
              (*e)->GetByteArrayRegion(e, phonePk, 0, 32, (jbyte *)q); }
    pk_init_pc(C(h), code ? c : 0, sk != 0, s, p, q, (const uint8_t *)"JIMMY-PC", 8);
}
jlong FN(feed)(JNIEnv *e, jclass k, jlong h, jbyteArray d, jint off, jint len) {
    uint8_t b[1024]; (*e)->GetByteArrayRegion(e, d, off, len, (jbyte *)b);
    int used, ev = pk_feed(C(h), b, len, &used); return ((jlong)used << 8) | ev;
}
jbyteArray FN(takeOut)(JNIEnv *e, jclass k, jlong h) {
    jbyteArray a = (*e)->NewByteArray(e, C(h)->outlen);
    (*e)->SetByteArrayRegion(e, a, 0, C(h)->outlen, (jbyte *)C(h)->out); C(h)->outlen = 0; return a;
}
jbyteArray FN(text)(JNIEnv *e, jclass k, jlong h) {
    jbyteArray a = (*e)->NewByteArray(e, C(h)->textlen);
    (*e)->SetByteArrayRegion(e, a, 0, C(h)->textlen, (jbyte *)C(h)->text); return a;
}
jbyteArray FN(keys)(JNIEnv *e, jclass k, jlong h) {   // pc sk || pc pk || phone pk
    jbyteArray a = (*e)->NewByteArray(e, 96);
    (*e)->SetByteArrayRegion(e, a, 0, 32, (jbyte *)C(h)->my_sk); (*e)->SetByteArrayRegion(e, a, 32, 32, (jbyte *)C(h)->my_pk);
    (*e)->SetByteArrayRegion(e, a, 64, 32, (jbyte *)C(h)->peer_pk); return a;
}
jint FN(fail)(JNIEnv *e, jclass k, jlong h) { return C(h)->fail; }
jboolean FN(reply)(JNIEnv *e, jclass k, jlong h, jbyteArray t) {
    uint8_t b[400]; int n = (*e)->GetArrayLength(e, t); (*e)->GetByteArrayRegion(e, t, 0, n, (jbyte *)b);
    return pk_send_reply(C(h), b, n);
}
