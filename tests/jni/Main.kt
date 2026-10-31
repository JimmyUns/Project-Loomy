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
    check(!Native.sendCmd(p2, ByteArray(481) { 'a'.code.toByte() }), "too long refused")

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
    fun w(said: String, want: String) = check(Words.command(said) == want, "\"$said\" -> \"$want\" (got \"${Words.command(said)}\")")
    w("turn off the screen", "display off"); w("Screen off.", "display off"); w("turn the screens off please", "display off")
    w("show screen", "display on"); w("turn on the screen", "display on"); w("wake the monitors up", "display on")
    w("turn the display off", "display off"); w("display off", "display off"); w("display extend", "display extend")
    w("Ayo Loomy, can you open system info please", "si"); w("yo loomy system info", "si")
    w("open system info and go to the next tab", "si and input next tab")
    w("system info and input next tab", "si and input next tab")
    w("system info gpu one", "si gpu 1"); w("go back", "input back"); w("press enter", "input enter")
    w("show loomy", "open"); w("open loomy", "open"); w("bring up the recycle bin", "rb"); w("what can you do", "help")
    w("web gmail", "web gmail"); w("lp salt and pepper", "lp salt and pepper")
    check(Words.addressed("ayo loomy show screen") == "show screen", "addressed: ayo loomy")
    check(Words.addressed("so anyway Yo Loomy, system info.") == "system info", "addressed mid-sentence")
    check(Words.addressed("hey lumi") == "", "addressed: greeting alone")
    check(Words.addressed("I was telling loomy about it") == null, "name alone isn't addressing")
    check(Words.addressed("we should turn off the screen") == null, "talking to a friend: ignored")
    check(Words.addressed("Ayoloomy display extend") == "display extend", "run-together greeting")
    check(Words.isStop("stop listening") && !Words.isStop("display off"), "stop words")
    w("height", "hide"); w("Loomy, height.", "hide"); w("hide", "hide")
    fun k(said: String, want: String?) = check(Words.key(said) == want, "key \"$said\" -> $want (got ${Words.key(said)})")
    k("next", "input next"); k("Next tab.", "input next tab"); k("go back", "input back"); k("okay", "input ok")
    k("confirm", "input confirm"); k("cancel", "input cancel"); k("go down three", "input down 3"); k("down 2 times", "input down 2")
    k("press enter", "input enter"); k("page down", "input page down")
    k("we should go out tonight", null); k("down for it", null); k("display off", null)
    check(Words.isInputModeOn(Words.command("yo loomy input mode")), "input mode on (greeted)")
    check(Words.isInputModeOn(Words.command("Loomy, enable input mode.")), "enable input mode")
    check(Words.isInputModeOff(Words.command("input mode off")) && Words.isInputModeOff(Words.command("disable input mode")), "input mode off")
    // round 4: her name alone is enough, but it has to open the sentence
    fun a(said: String, want: String?) = check(Words.addressed(said) == want, "addressed \"$said\" -> $want (got ${Words.addressed(said)})")
    a("Loomy show screen", "show screen"); a("lumi system info", "system info"); a("Okay Lumi, display extend.", "display extend")
    a("Loomy.", ""); a("Lumia", ""); a("yo loomy hide", "hide"); a("so anyway yo loomy hide", "hide")
    a("I was telling loomy about it", null); a("we should turn off the screen", null); a("show screen", null)
    check(Words.endsWithName("so yeah okay Loomy") && !Words.endsWithName("loomy show"), "name said last")
    w("okay loomy show", "show"); w("Lumi, turn off the screen", "display off")
    k("loomy down", "input down"); k("input down", "input down"); k("Loomy, input down.", "input down"); k("Okay.", "input ok")
    check(Words.isSoloOn(Words.command("Loomy solo mode")) && Words.isSoloOn(Words.command("so low mode")), "solo mode on")
    check(Words.isSoloOff(Words.command("solo mode off")) && Words.isSoloOff(Words.command("Lumi, disable solo mode.")), "solo mode off")
    check(!Words.isSoloOn(Words.command("display solo")), "not solo")
    // round 5: several keys in one breath, off/on, mute, stop connection
    k("up up right confirm", "input up 2 and input right and input confirm")
    k("Up, up, right, confirm.", "input up 2 and input right and input confirm")
    k("down three and then right", "input down 3 and input right"); k("next tab next tab", "input next tab 2")
    k("go back go back", "input back 2"); k("down twice", "input down 2"); k("loomy up up", "input up 2")
    k("up up and away", null); k("down for it", null); k("left on read", null)
    w("loomy off", "hide"); w("Loomy on.", "open"); w("off", "hide"); w("screen off", "display off"); w("turn on the screen", "display on")
    check(Words.isStop(Words.command("mute")) && Words.isStop(Words.command("Okay, that's it.")) && Words.isStop(Words.command("ok thats it")), "mute words")
    check(Words.isDisconnect(Words.command("stop the connection please")) && Words.isDisconnect(Words.command("Loomy, disconnect.")), "stop connection words")
    check(!Words.isStop(Words.command("display off")) && !Words.isDisconnect(Words.command("connections")), "not controls")
    check(Words.command("slash packages") == "/pkg", "slash packages -> /pkg")
    check(Words.command("Slesh reload.") == "/reload", "slesh reload -> /reload")
    check(Words.command("Loomy slash web config add yt youtube.com") == "/webconfig add yt youtube.com", "slash web config")
    check(Words.command("recycle bin") == "rb", "recycle bin -> rb")
    check(Words.command("system info cpu") == "si cpu", "system info cpu -> si cpu")
    check(Words.command("D P extend") == "dp extend", "d p -> dp")
    check(Words.command("/pkg") == "/pkg", "typed slash kept")
    check(Words.command("slash") == "", "a lone slash is nothing")
    // v2: note + forget through the real JNI
    run {
        val c = "48210937".toByteArray()
        val pc = TestPc.create(); TestPc.initPc(pc, c, null, null, null)
        val ph = Native.create(); Native.initPhone(ph, c, null, null, null, "p".toByteArray())
        toPhone(ph, TestPc.takeOut(pc)); toPc(pc, Native.takeOut(ph)); toPhone(ph, TestPc.takeOut(pc)); toPc(pc, Native.takeOut(ph)); toPhone(ph, TestPc.takeOut(pc))
        val k = Native.myKeys(ph); val pk = TestPc.keys(pc)
        val pc2 = TestPc.create(); TestPc.initPc(pc2, null, pk.copyOfRange(0, 32), pk.copyOfRange(32, 64), k.copyOfRange(32, 64))
        val p2 = Native.create(); Native.initPhone(p2, null, k.copyOfRange(0, 32), k.copyOfRange(32, 64), pk.copyOfRange(32, 64), "p".toByteArray())
        toPhone(p2, TestPc.takeOut(pc2)); toPc(pc2, Native.takeOut(p2)); toPhone(p2, TestPc.takeOut(pc2)); toPc(pc2, Native.takeOut(p2))
        check(Native.sendNote(p2, "lumi displaay".toByteArray()) && toPc(pc2, Native.takeOut(p2)).contains(Native.EV_NOTE) && String(TestPc.text(pc2)) == "lumi displaay", "note via JNI")
        check(Native.sendNote(p2, ByteArray(0)) && toPc(pc2, Native.takeOut(p2)).contains(Native.EV_NOTE), "empty note via JNI")
        check(Native.sendForget(p2) && toPc(pc2, Native.takeOut(p2)).contains(Native.EV_FORGET), "forget via JNI")
        check(!Native.sendCmd(p2, "rb".toByteArray()), "nothing after forget")
    }
    println("$checks checks, $fails failure(s)")
    if (fails > 0) kotlin.system.exitProcess(1)
}
