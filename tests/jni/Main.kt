// Desktop JNI test: the app's real Native.kt + libpocket (pocket_jni.c) as the phone, TestPc as the PC,
// bytes passed by hand. Catches JNI name/signature mistakes before they crash on a phone. Also Words.
import io.github.jimmyuns.pocketloomy.Native
import io.github.jimmyuns.pocketloomy.Words

object TestPc {
    init { System.loadLibrary("testpc") }
    @JvmStatic external fun create(): Long
    @JvmStatic external fun initPc(h: Long, code: ByteArray?, sk: ByteArray?, pk: ByteArray?, phonePk: ByteArray?)
    @JvmStatic external fun feed(h: Long, d: ByteArray, off: Int, len: Int): Long
    @JvmStatic external fun takeOut(h: Long): ByteArray
    @JvmStatic external fun text(h: Long): ByteArray
    @JvmStatic external fun keys(h: Long): ByteArray
    @JvmStatic external fun fail(h: Long): Int
    @JvmStatic external fun reply(h: Long, t: ByteArray): Boolean
}

var fails = 0; var checks = 0
fun check(ok: Boolean, what: String) { checks++; if (!ok) { fails++; println("!! FAIL: $what") } }

// move bytes: returns the events the receiver reported
fun toPc(pc: Long, b: ByteArray): List<Int> { val ev = mutableListOf<Int>(); var o = 0
    while (o < b.size) { val r = TestPc.feed(pc, b, o, b.size - o); o += (r ushr 8).toInt(); val e = (r and 0xff).toInt(); if (e != 0) ev += e; if (e == 8) break }
    return ev }
fun toPhone(ph: Long, b: ByteArray): List<Int> { val ev = mutableListOf<Int>(); var o = 0
    while (o < b.size) { val r = Native.feed(ph, b, o, b.size - o); o += (r ushr 8).toInt(); val e = (r and 0xff).toInt(); if (e != 0) ev += e; if (e == Native.EV_FAIL) break }
    return ev }

fun main() {
    val code = "48210937".toByteArray()
    // ---- pairing
    val pc = TestPc.create(); TestPc.initPc(pc, code, null, null, null)
    val ph = Native.create()
    check(Native.initPhone(ph, code, null, null, null, "Pixel 8".toByteArray()), "initPhone (pairing)")
    toPhone(ph, TestPc.takeOut(pc))
    check(toPc(pc, Native.takeOut(ph)).contains(1), "PC: code used (ATTEMPT)")
    toPhone(ph, TestPc.takeOut(pc))
    check(toPc(pc, Native.takeOut(ph)).contains(2), "PC paired")
    check(toPhone(ph, TestPc.takeOut(pc)).contains(Native.EV_PAIRED), "phone paired")
    check(String(Native.peerName(ph)) == "JIMMY-PC", "peerName")
    val mine = Native.myKeys(ph); val pcKeys = TestPc.keys(pc)
    check(mine.size == 64 && mine.copyOfRange(32, 64).contentEquals(pcKeys.copyOfRange(64, 96)), "myKeys matches what the PC stored")
    check(Native.peerKey(ph).contentEquals(pcKeys.copyOfRange(32, 64)), "peerKey = PC public key")
    Native.destroy(ph)

    // ---- connect + command + reply + ping
    val pc2 = TestPc.create(); TestPc.initPc(pc2, null, pcKeys.copyOfRange(0, 32), pcKeys.copyOfRange(32, 64), mine.copyOfRange(32, 64))
    val p2 = Native.create()
    check(Native.initPhone(p2, null, mine.copyOfRange(0, 32), mine.copyOfRange(32, 64), pcKeys.copyOfRange(32, 64), "Pixel 8".toByteArray()), "initPhone (connect)")
    toPhone(p2, TestPc.takeOut(pc2))
    toPc(pc2, Native.takeOut(p2))
    check(toPhone(p2, TestPc.takeOut(pc2)).contains(Native.EV_READY) && Native.ready(p2), "phone ready")
    check(toPc(pc2, Native.takeOut(p2)).contains(3), "PC ready")
    check(Native.sendCmd(p2, "display extend".toByteArray()), "sendCmd")
    check(toPc(pc2, Native.takeOut(p2)).contains(4) && String(TestPc.text(pc2)) == "display extend", "PC got the command")
    TestPc.reply(pc2, "Switched to Extend.".toByteArray())
    check(toPhone(p2, TestPc.takeOut(pc2)).contains(Native.EV_REPLY) && String(Native.text(p2)) == "Switched to Extend.", "phone got the reply")
    check(Native.sendPing(p2) && toPc(pc2, Native.takeOut(p2)).contains(6), "ping")
    check(toPhone(p2, TestPc.takeOut(pc2)).contains(Native.EV_PONG), "pong")
    check(!Native.sendCmd(p2, ByteArray(401) { 'a'.code.toByte() }), "too long refused")

    // ---- failures surface with the right numbers
    val pc3 = TestPc.create(); TestPc.initPc(pc3, null, null, null, null)
    val p3 = Native.create(); Native.initPhone(p3, code, null, null, null, ByteArray(0))
    check(toPhone(p3, TestPc.takeOut(pc3)).contains(Native.EV_FAIL) && Native.fail(p3) == Native.F_NOPAIRING, "no code open -> F_NOPAIRING")
    check(Native.failText(Native.F_CODE) == "the wrong pairing code", "failText")
    val pc4 = TestPc.create(); TestPc.initPc(pc4, "11111111".toByteArray(), null, null, null)
    val p4 = Native.create(); Native.initPhone(p4, code, null, null, null, ByteArray(0))
    toPhone(p4, TestPc.takeOut(pc4)); toPc(pc4, Native.takeOut(p4))
    check(toPhone(p4, TestPc.takeOut(pc4)).contains(Native.EV_FAIL) && Native.fail(p4) == Native.F_CODE, "wrong code -> F_CODE")
    check(!Native.initPhone(p4, "123".toByteArray(), null, null, null, ByteArray(0)), "a short code is refused")
    check(!Native.initPhone(p4, null, ByteArray(31), ByteArray(32), ByteArray(32), ByteArray(0)), "a short key is refused")

    // ---- Words
    check(Words.code("4821 0937") == "48210937", "digits")
    check(Words.code("Four eight two one, zero nine three seven.") == "48210937", "number words")
    check(Words.code("for ate to won oh nine tree seven") == "48210937", "sound-alikes")
    check(Words.code("48-21-09-37") == "48210937", "dashes")
    check(Words.code("4821 093") == null && Words.code("4821 09377") == null, "wrong length")
    check(Words.code("4821 banana 0937") == null, "non-digit word")
    check(Words.command("Loomy, display extend.") == "display extend", "wake word + punctuation")
    check(Words.command("loomy run web gmail") == "web gmail", "run")
    check(Words.command("Web Gmail") == "web gmail", "lowercase")
    check(Words.command("run") == "run", "lone run kept")
    check(Words.command("  ") == "", "empty")
    println("$checks checks, $fails failure(s)")
    if (fails > 0) kotlin.system.exitProcess(1)
}
