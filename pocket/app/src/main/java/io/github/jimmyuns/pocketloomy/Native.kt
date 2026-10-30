package io.github.jimmyuns.pocketloomy

// The protocol engine: Loomy's own src/pocket.c (+ Monocypher), the same code the PC runs.
// pocket.h documents it; event and failure numbers below must match it.
object Native {
    init { System.loadLibrary("pocket") }

    const val EV_NONE = 0
    const val EV_ATTEMPT = 1
    const val EV_PAIRED = 2
    const val EV_READY = 3
    const val EV_CMD = 4
    const val EV_REPLY = 5
    const val EV_PING = 6
    const val EV_PONG = 7
    const val EV_FAIL = 8

    const val F_FRAMESIZE = 1
    const val F_UNEXPECTED = 2
    const val F_VERSION = 3
    const val F_NOPAIRING = 4
    const val F_NOTRUST = 5
    const val F_LOWORDER = 6
    const val F_CODE = 7
    const val F_AUTH = 8
    const val F_DECRYPT = 9
    const val F_BADMSG = 10
    const val F_AFTERDONE = 11

    @JvmStatic external fun create(): Long
    @JvmStatic external fun destroy(h: Long)
    /** code = 8 ASCII digits to pair, or null to connect with the stored keys */
    @JvmStatic external fun initPhone(h: Long, code: ByteArray?, sk: ByteArray?, pk: ByteArray?, pcPk: ByteArray?, name: ByteArray): Boolean
    /** returns (bytes used shl 8) or event */
    @JvmStatic external fun feed(h: Long, data: ByteArray, off: Int, len: Int): Long
    @JvmStatic external fun takeOut(h: Long): ByteArray
    @JvmStatic external fun text(h: Long): ByteArray
    @JvmStatic external fun peerName(h: Long): ByteArray
    @JvmStatic external fun fail(h: Long): Int
    @JvmStatic external fun failText(f: Int): String
    @JvmStatic external fun ready(h: Long): Boolean
    /** after EV_PAIRED: this phone's new secret key + public key, 64 bytes */
    @JvmStatic external fun myKeys(h: Long): ByteArray
    @JvmStatic external fun peerKey(h: Long): ByteArray
    @JvmStatic external fun sendCmd(h: Long, text: ByteArray): Boolean
    @JvmStatic external fun sendPing(h: Long): Boolean
}
