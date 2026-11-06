// Desktop JNI test: the app's real Native.kt + libpocket (pocket_jni.c) as the phone, TestPc as the PC,
// bytes passed by hand. Catches JNI name/signature mistakes before they crash on a phone. Also Words.
import io.github.jimmyuns.pocketloomy.Ellune
import io.github.jimmyuns.pocketloomy.Sound
import io.github.jimmyuns.pocketloomy.Vad
import io.github.jimmyuns.pocketloomy.Update
import io.github.jimmyuns.pocketloomy.Json
import io.github.jimmyuns.pocketloomy.Games
import io.github.jimmyuns.pocketloomy.Ws
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
    val id = Native.phoneId(ph)
    check(Native.awaitingPc(ph) && Regex("[0-9A-Z]{4}-[0-9A-Z]{4}").matches(id), "phone waits for the PC, shows its ID ($id)")
    check(toPc(pc, Native.takeOut(ph)).contains(2), "PC paired")
    check(toPhone(ph, TestPc.takeOut(pc)).contains(Native.EV_PAIRED) && !Native.awaitingPc(ph), "phone paired")
    check(Native.phoneId(ph) == id, "the ID stays the same after pairing")
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
    check(Words.isMute(Words.plain("stop listening")) && !Words.isMute(Words.plain("display off")), "stop words")
    w("height", "hide"); w("Loomy, height.", "hide"); w("hide", "hide")
    fun k(said: String, want: String?) = check(Words.key(said) == want, "key \"$said\" -> $want (got ${Words.key(said)})")
    k("next", "input next"); k("Next tab.", "input next tab"); k("go back", "input back"); k("okay", "input ok")
    k("confirm", "input confirm"); k("cancel", "input cancel"); k("go down three", "input down 3"); k("down 2 times", "input down 2")
    k("press enter", "input enter"); k("page down", "input page down")
    k("we should go out tonight", null); k("down for it", null); k("display off", null)
    check(Words.isInputModeOn(Words.plain("yo loomy input mode")), "input mode on (greeted)")
    check(Words.isInputModeOn(Words.plain("Loomy, enable input mode.")), "enable input mode")
    check(Words.isInputModeOff(Words.plain("input mode off")) && Words.isInputModeOff(Words.plain("disable input mode")), "input mode off")
    // round 4: her name alone is enough, but it has to open the sentence
    fun a(said: String, want: String?) = check(Words.addressed(said) == want, "addressed \"$said\" -> $want (got ${Words.addressed(said)})")
    a("Loomy show screen", "show screen"); a("lumi system info", "system info"); a("Okay Lumi, display extend.", "display extend")
    a("Loomy.", ""); a("Lumia", ""); a("yo loomy hide", "hide"); a("so anyway yo loomy hide", "hide")
    a("I was telling loomy about it", null); a("we should turn off the screen", null); a("show screen", null)
    check(Words.endsWithName("so yeah okay Loomy") && !Words.endsWithName("loomy show"), "name said last")
    w("okay loomy show", "open"); w("Lumi, turn off the screen", "display off")
    k("loomy down", "input down"); k("input down", "input down"); k("Loomy, input down.", "input down"); k("Okay.", "input ok")
    check(Words.isSoloOn(Words.plain("Loomy solo mode")) && Words.isSoloOn(Words.plain("so low mode")), "solo mode on")
    check(Words.isSoloOff(Words.plain("solo mode off")) && Words.isSoloOff(Words.plain("Lumi, disable solo mode.")), "solo mode off")
    check(!Words.isSoloOn(Words.plain("display solo")), "not solo")
    // round 5: several keys in one breath, off/on, mute, stop connection
    k("up up right confirm", "input up 2 and input right and input confirm")
    k("Up, up, right, confirm.", "input up 2 and input right and input confirm")
    k("down three and then right", "input down 3 and input right"); k("next tab next tab", "input next tab 2")
    k("go back go back", "input back 2"); k("down twice", "input down 2"); k("loomy up up", "input up 2")
    k("up up and away", null); k("down for it", null); k("left on read", null)
    w("loomy off", "hide"); w("Loomy on.", "open"); w("off", "hide"); w("screen off", "display off"); w("turn on the screen", "display on")
    // round 6: mute is never one word; solo sleeps on "ok thank you"; the phrase library
    fun mute(said: String, want: Boolean) = check(Words.isMute(Words.plain(said)) == want, "mute \"$said\" = $want (plain \"${Words.plain(said)}\")")
    mute("ok turn off the mic", true); mute("Turn the mic off.", true); mute("Loomy, stop listening", true); mute("can you turn the mic off please", true)
    mute("mute", false); mute("done", false); mute("stop", false); mute("display off", false)
    fun sleep(said: String, want: Boolean) = check(Words.isSleep(Words.plain(said)) == want, "sleep \"$said\" = $want (plain \"${Words.plain(said)}\")")
    sleep("ok thank you", true); sleep("Thanks Loomy.", true); sleep("okay stop listening", true); sleep("ok that's it", true); sleep("thank you so much for dinner", false)
    check(Words.isDisconnect(Words.plain("stop the connection please")) && Words.isDisconnect(Words.plain("Loomy, disconnect.")), "stop connection words")
    check(!Words.isDisconnect(Words.plain("connections")), "not controls")
    check(Words.isSoloOn(Words.plain("Loomy solo mode")) && Words.isSoloOn(Words.plain("so low mode")) && Words.isSoloOff(Words.plain("Lumi, disable solo mode.")), "solo by voice (plain)")
    w("go back one tab", "input previous tab"); w("next page", "input next tab"); w("tab right", "input next tab")
    w("mirror the screens", "display duplicate"); w("main screen only", "display pc"); w("extend the screens please", "display extend")
    w("how's my pc doing", "si"); w("cpu usage", "si cpu"); w("open gmail", "web gmail"); w("open system info", "si"); w("open lp", "lp")
    w("go away", "hide"); w("come back", "open"); w("open the trash", "rb"); w("what can i say", "help")
    k("go back one tab", "input previous tab"); k("next page next page", "input next tab 2"); k("scroll down three", "input down 3")
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
    // ---- Ellune's ping reply (same cases as ellune_check() in tests/test.c)
    check(Ellune.check("{\"ok\":true,\"service\":\"ellune\",\"version\":\"0.1\"}") == "0.1", "ellune: real reply")
    check(Ellune.check("{ \"ok\" : true , \"service\": \"ellune\", \"version\": \"0.12b\" }\n") == "0.12b", "ellune: spaces fine")
    check(Ellune.check("{\"ok\":false,\"error\":\"not found\"}") == null, "ellune: 404 body isn't up")
    check(Ellune.check("{\"ok\":true,\"service\":\"other\",\"version\":\"0.1\"}") == null, "ellune: someone else's ok")
    check(Ellune.check("<html><body>Sign in to the wifi</body></html>") == null, "ellune: captive portal page")
    check(Ellune.check("<html>{\"ok\":true,\"service\":\"ellune\"}</html>") == null, "ellune: json inside a page")
    check(Ellune.check("{\"ok\":truthy,\"service\":\"ellune\"}") == null, "ellune: ok must be true")
    check(Ellune.check("") == null && Ellune.check(null) == null, "ellune: empty / null")
    check(Ellune.check("{\"ok\":true,\"service\":\"ellune\"}") == "?", "ellune: no version = ?")
    check(Ellune.check("{\"ok\":true,\"service\":\"ellune\",\"version\":\"0.1&calc\"}") == "?", "ellune: weird version = ?")
    check(Ellune.check("{\"ok\":true,\"service\":\"ellune\",\"version\":\"123456789012345678901234567890\"}") == "?", "ellune: long version = ?")
    check(Ellune.message(Ellune.Result.Up("0.1", 63)) == "Ellune is up (v0.1, 63 ms).", "ellune: message")
    check(Ellune.pingNow() == Ellune.Result.Unreachable || Ellune.pingNow() is Ellune.Result.Up, "ellune: ping fails cleanly here (or works)")

    // ---- the guest list: the phone's real signing code (a software key here; on the phone it's the Keystore's)
    run {
        val g = java.security.KeyPairGenerator.getInstance("EC").apply { initialize(java.security.spec.ECGenParameterSpec("secp256r1")) }
        val kp = g.generateKeyPair()
        val pub = Ellune.rawPublic(kp.public as java.security.interfaces.ECPublicKey)
        check(pub.size == 65 && pub[0] == 4.toByte(), "ellune: raw public key is 65 bytes, 04 first")
        val id = Ellune.deviceId(pub)
        check(Regex("[0-9A-HJKMNP-TV-Z]{4}-[0-9A-HJKMNP-TV-Z]{4}").matches(id) && id == Ellune.deviceId(pub.copyOf()), "ellune: device id ($id)")
        check(Ellune.guestLine(pub).startsWith("$id=") && !Ellune.guestLine(pub).contains("="+"=") && Regex("[A-Za-z0-9_=-]+").matches(Ellune.guestLine(pub)), "ellune: guest line")
        // DER -> raw, including short r/s (leading zeros dropped) and junk
        var shortOk = true
        repeat(300) {
            val der = java.security.Signature.getInstance("SHA256withECDSA").run { initSign(kp.private); update(byteArrayOf(it.toByte())); sign() }
            val raw = Ellune.derToRaw(der)
            if (raw == null || raw.size != 64) shortOk = false
        }
        check(shortOk, "ellune: 300 signatures convert to raw r||s")
        val r1 = ByteArray(32) { 1 }; val s1 = ByteArray(31) { 2 }
        val der1 = byteArrayOf(0x30, 68, 0x02, 33, 0) + r1 + byteArrayOf(0x02, 31) + s1
        check(Ellune.derToRaw(der1)?.contentEquals(r1 + byteArrayOf(0) + s1) == true, "ellune: der with padded r, short s")
        check(Ellune.derToRaw(ByteArray(0)) == null && Ellune.derToRaw(der1.copyOf(der1.size - 1)) == null &&
              Ellune.derToRaw(der1 + byteArrayOf(0)) == null && Ellune.derToRaw(ByteArray(64) { 0x30 }) == null, "ellune: junk der refused")
        val tooBig = byteArrayOf(0x30, 70, 0x02, 33, 9) + ByteArray(32) { 1 } + byteArrayOf(0x02, 33, 0) + ByteArray(32) { 2 }
        check(Ellune.derToRaw(tooBig) == null, "ellune: r over 32 bytes refused")
        // hand a signed request to ellune/test.mjs (run.sh runs it next)
        val time = 1_793_600_000_000L
        val h = Ellune.signedHeaders(kp.private, pub, "GET", "/me", time)
        val bad = h.toMutableMap().apply { put("x-ellune-time", (time + 1).toString()) }
        fun js(m: Map<String, String>) = m.entries.joinToString(",", "{", "}") { "\"${it.key}\":\"${it.value}\"" }
        java.io.File("/tmp/ellune_cross.json").writeText(
            "{\"name\":\"$id\",\"key\":\"${Ellune.guestLine(pub).substringAfter("=")}\",\"time\":$time,\"headers\":${js(h)},\"tampered\":${js(bad)}}")
        check(Ellune.meMessage(Ellune.Me.NotGuest("not on the guest list")) == "This phone isn't on her guest list yet.", "ellune: guest message")
        // a /hear request with a WAV from Sound.wav, for the Worker to check (wavOk + signature + whisper input)
        val pcm = ShortArray(Sound.RATE * 2) { ((it * 37) % 2000 - 1000).toShort() }
        val clip = Sound.wav(pcm)
        val hh = Ellune.signedHeaders(kp.private, pub, "POST", "/hear", time, clip)
        val b64 = java.util.Base64.getEncoder().encodeToString(clip)
        val say = "{\"text\":${Ellune.jsonQuote("make the \"screens\" go dark")}}".toByteArray()
        val uh = Ellune.signedHeaders(kp.private, pub, "POST", "/understand", time, say)
        // 0.5: a /register request as the app makes it (RFC 6238's 59 s code), for the Worker to check
        val regBody = "{\"code\":${Ellune.jsonQuote("287082")},\"name\":${Ellune.jsonQuote("phone-" + id)}}".toByteArray()
        val rh = Ellune.signedHeaders(kp.private, pub, "POST", "/register", 59_000L, regBody)
        java.io.File("/tmp/ellune_cross_reg.json").writeText("{\"headers\":${js(rh)},\"body\":\"" + java.util.Base64.getEncoder().encodeToString(regBody) + "\"}")
        java.io.File("/tmp/ellune_cross_hear.json").writeText(
            "{\"hear\":{\"headers\":${js(hh)},\"body\":\"$b64\"},\"understand\":{\"headers\":${js(uh)},\"body\":\"" +
            java.util.Base64.getEncoder().encodeToString(say) + "\"}}")
    }

    // ---- recording for Ellune: WAV, voice detection, JSON
    run {
        val w = Sound.wav(ShortArray(4) { (it * 1000 - 1500).toShort() })
        check(w.size == 52 && String(w, 0, 4) == "RIFF" && String(w, 8, 4) == "WAVE" && String(w, 36, 4) == "data" &&
              w[22].toInt() == 1 && (w[24].toInt() and 0xff) + ((w[25].toInt() and 0xff) shl 8) == 16000 && w[34].toInt() == 16, "wav: header")
        check(w[44] == (-1500 and 0xff).toByte() && w[45] == (-1500 shr 8).toByte() && w[50] == (1500 and 0xff).toByte(), "wav: little-endian samples")
        check(Sound.wav(ShortArray(10), 2, 6).size == 52, "wav: a slice")
        check(Sound.rms(ShortArray(100) { 300 }) == 300.0 && Sound.rms(ShortArray(0)) == 0.0, "rms")
        check(Sound.level(1.0) == -2f && Sound.level(100.0) == 0f && Sound.level(1e6) == 10f, "level range")

        fun runVad(frames: List<Double>, v: Vad = Vad()): Pair<Vad, List<Vad.Event>> {
            val ev = mutableListOf<Vad.Event>()
            for (f in frames) { val e = v.push(f); if (e != Vad.Event.NONE) ev += e }
            return v to ev
        }
        val quiet = List(300) { 50.0 + (it % 3) * 10 }
        val (v1, e1) = runVad(quiet)
        check(e1 == listOf(Vad.Event.TIMEOUT), "vad: silence times out at 6 s")
        val talk = List(40) { 50.0 } + List(60) { 3000.0 } + List(80) { 60.0 }
        val (v2, e2) = runVad(talk)
        check(e2 == listOf(Vad.Event.STARTED, Vad.Event.ENDED), "vad: talk starts and ends ($e2)")
        check(v2.start == 40 + 2 - 2 - 15 && v2.end == 100 + 15, "vad: keeps 300 ms before and after (${v2.start}..${v2.end})")
        val (_, e3) = runVad(List(10) { 50.0 } + listOf(3000.0, 3000.0) + List(100) { 50.0 })
        check(e3.isEmpty(), "vad: a 40 ms click isn't talking")
        val (v4, e4) = runVad(List(10) { 50.0 } + List(700) { 3000.0 })
        check(e4 == listOf(Vad.Event.STARTED, Vad.Event.ENDED) && v4.end - v4.start == 500, "vad: 10 s at most")
        val (v5, e5) = runVad(List(20) { 800.0 } + List(40) { 4000.0 } + List(60) { 900.0 })
        check(e5 == listOf(Vad.Event.STARTED, Vad.Event.ENDED), "vad: a noisy room (floor 800)")
        val (_, e6) = runVad(List(400) { 0.0 })
        check(e6 == listOf(Vad.Event.TIMEOUT), "vad: digital silence times out")
        check(Vad().let { it.push(1.0); it.frame } == 1, "vad: counts frames")
        // 2.23: noise that starts while you talk (his motorcycle) must not keep the clip open to 10 s
        run {
            val f = List(20) { 50.0 } +
                List(40) { if (it % 5 == 4) 60.0 else 3000.0 } +             // talking, gaps between words
                List(40) { if (it % 5 == 4) 1200.0 else 3200.0 } +           // still talking, an engine starts
                List(400) { 1200.0 + (it % 4) * 60 }                        // you stopped; the engine goes on
            val (v, e) = runVad(f)
            check(e == listOf(Vad.Event.STARTED, Vad.Event.ENDED) && v.frame < 100 + 70, "vad: an engine starting mid-sentence doesn't hold it open (ended at frame ${v.frame}, $e)")
            check(v.end in 100..100 + 50, "vad: the clip ends near where you stopped (${v.end})")
        }
        run {   // an engine already running when you start: learned before, ends like a quiet room
            val f = List(40) { 1000.0 + (it % 3) * 50 } + List(50) { if (it % 6 == 5) 1100.0 else 5000.0 } + List(200) { 1050.0 }
            val (v, e) = runVad(f)
            check(e == listOf(Vad.Event.STARTED, Vad.Event.ENDED) && v.frame <= 90 + 36, "vad: a steady engine from the start (frame ${v.frame})")
        }
        run {   // a quiet room ends 0.7 s after you stop (was 1 s)
            val (v, e) = runVad(List(30) { 50.0 } + List(50) { if (it % 7 == 6) 80.0 else 2500.0 } + List(100) { 55.0 })
            check(e == listOf(Vad.Event.STARTED, Vad.Event.ENDED) && v.frame == 80 + 35, "vad: 0.7 s of quiet ends it (frame ${v.frame})")
        }
        run {   // loud talking with short pauses isn't cut: pauses are short, and the voice keeps coming back
            val f = List(20) { 50.0 } + List(300) { k -> if (k % 40 in 30..39) 400.0 else 2000.0 + (k % 7) * 300 } + List(60) { 50.0 }
            val (v, e) = runVad(f)
            check(e == listOf(Vad.Event.STARTED, Vad.Event.ENDED) && v.frame > 320, "vad: a long sentence with pauses isn't cut (frame ${v.frame})")
        }
        run {   // the room goes quieter than your voice by a lot (a fan), but stays far above the old floor: still ends
            val f = List(20) { 30.0 } + List(60) { if (it % 5 == 4) 300.0 else 6000.0 } + List(200) { 700.0 + (it % 2) * 40 }
            val (v, e) = runVad(f)
            check(e == listOf(Vad.Event.STARTED, Vad.Event.ENDED) && v.frame < 80 + 50, "vad: -18 dB under the voice is quiet (frame ${v.frame})")
        }

        check(Ellune.jsonString("{\"ok\":true,\"heard\":\"Loomy, turn the screens off.\"}", "heard") == "Loomy, turn the screens off.", "json: plain")
        check(Ellune.jsonString("{\"heard\" : \"it\\u2019s \\\"x\\\" a\\\\b\"}", "heard") == "it\u2019s \"x\" a\\b", "json: escapes")
        check(Ellune.jsonString("{\"heard\":\"\"}", "heard") == "" && Ellune.jsonString("{}", "heard") == null &&
              Ellune.jsonString("{\"heard\":5}", "heard") == null && Ellune.jsonString("{\"heard\":\"open", "heard") == null, "json: empty / missing / broken")
        val tricky = "say \"hi\" \\ then\n\u0001"
        check(Ellune.jsonString("{\"t\":" + Ellune.jsonQuote(tricky) + "}", "t") == tricky, "json: quote round trip")

        check(Words.knows("display off") && Words.knows("si and input next tab") && Words.knows("web youtube") && Words.knows("/pkg"), "knows: real commands")
        check(!Words.knows("make the screens go dark") && !Words.knows("") && !Words.knows("si and dance"), "knows: everything else")
        check(!Words.knows("display extent") && !Words.knows("dp extend now") && !Words.knows("si cpus") && !Words.knows("help me"),
              "knows: 2.23, the words after it must be real too")
        check(Words.knows("display") && Words.knows("dp off") && Words.knows("display on") && Words.knows("si gpu 2") &&
              Words.knows("sysinfo general") && Words.knows("lp games") && Words.knows("input down 3") && Words.knows("hide"),
              "knows: 2.23, real arguments still pass")
        check(!Words.knows("si gpu 12") && !Words.knows("rb now"), "knows: 2.23, odd arguments")
    }

    // ---- 2.22: "thank you" ends a sentence, "please" never does
    run {
        check(Words.untilThanks("turn off the screens thank you") == ("turn off the screens" to true), "thanks: at the end")
        check(Words.untilThanks("Loomy, open YouTube. Thank you!") == ("Loomy, open YouTube" to true), "thanks: punctuation")
        check(Words.untilThanks("system info thanks and next tab") == ("system info" to true), "thanks: what follows is dropped")
        check(Words.untilThanks("thank you") == ("" to true) && Words.untilThanks("thanks loomy").first == "", "thanks: alone")
        check(Words.untilThanks("turn off the screens") == ("turn off the screens" to false), "thanks: none")
        check(!Words.untilThanks("open thanksgiving recipes").second, "thanks: only the whole word")
        check(Words.command(Words.untilThanks("can you please turn my screen on thank you").first) == "display on", "please + thanks: screen on")
        check(Words.command("can you turn on my screen please") == "display on", "please at the end isn't an end")
        // her name, however the recognizer writes it with his accent
        for (n in listOf("Lumi", "Lumie", "Loomino", "Lumino", "Loomy's", "Lummy", "Lomi", "Lumia", "Loomie", "lumee"))
            check(Words.addressed("$n turn off the screen") == "turn off the screen", "name: $n")
        check(Words.addressed("lou me open youtube") == "open youtube" && Words.addressed("lu mi system info") == "system info", "name: two words")
        check(Words.addressed("okay lumino, system info") == "system info" && Words.addressed("ayo lumie show") == "show", "name: after a greeting")
        check(Words.addressed("loom is broken") == null && Words.addressed("luminous paint") == null && Words.addressed("lemon tea") == null, "name: not words like it")
        check(Words.addressed("I told lumi about it") == null, "name: not in the middle")
        // 2.22: Whisper hearing noise, and typing vs talking
        for (n in listOf("[typing]", "(keyboard clicking)", "you", "You.", "Thanks for watching!", "  ", "...", "\u266A\u266A", "*click*", "Uh.", "Thank you."))
            check(Words.isNoise(n), "noise: '$n'")
        for (n in listOf("turn off the screens", "next tab", "ok", "Loomy, system info.", "back"))
            check(!Words.isNoise(n), "not noise: '$n'")
        check(Sound.zcr(ShortArray(320) { if (it % 2 == 0) 1000 else -1000 }) > 0.9 && Sound.zcr(ShortArray(320) { if ((it / 40) % 2 == 0) 1000 else -1000 }) < 0.05, "zcr: crackle vs a low tone")
        run {
            val v = Vad()
            val ev = mutableListOf<Vad.Event>()
            for (k in 0 until 20) v.push(50.0, 0.1)
            for (k in 0 until 40) ev += v.push(3000.0, 0.45)          // loud but crackly: typing
            for (k in 0 until 60) ev += v.push(50.0, 0.1)
            check(ev.filter { it != Vad.Event.NONE } == listOf(Vad.Event.STARTED, Vad.Event.NOISE), "vad: typing ends as noise")
            val w = Vad()
            val ew = mutableListOf<Vad.Event>()
            for (k in 0 until 20) w.push(50.0, 0.1)
            for (k in 0 until 40) ew += w.push(3000.0, if (k % 3 == 0) 0.4 else 0.08)   // a voice: mostly vowels
            for (k in 0 until 60) ew += w.push(50.0, 0.1)
            check(ew.filter { it != Vad.Event.NONE } == listOf(Vad.Event.STARTED, Vad.Event.ENDED), "vad: a voice ends as speech")
        }
    }

    // ---- 2.22 remote: the relay room (same vector as ellune.c on the PC) and the WebSocket frames
    run {
        check(Ellune.room(ByteArray(32) { it.toByte() }) == "8a0e1d384dc28f829b6dbfec15ddb06d", "room: same as the PC's")
        check(Ws.accept("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", "ws: accept key (RFC 6455's example)")
        val mk = byteArrayOf(1, 2, 3, 4)
        val f = Ws.frame(2, byteArrayOf(10, 20, 30), mk)
        check(f.size == 2 + 4 + 3 && f[0] == 0x82.toByte() && f[1] == 0x83.toByte() && f[6] == (10 xor 1).toByte(), "ws: a small masked binary frame")
        val big = Ws.frame(2, ByteArray(300) { 7 }, mk)
        check(big[1] == (0x80 or 126).toByte() && big[2] == 1.toByte() && big[3] == 44.toByte() && big.size == 4 + 4 + 300, "ws: 16-bit length")
        fun srv(op: Int, p: ByteArray, fin: Boolean = true) = byteArrayOf(((if (fin) 0x80 else 0) or op).toByte(), p.size.toByte()) + p
        var r = Ws.readFrame(java.io.ByteArrayInputStream(srv(2, byteArrayOf(5, 6))))
        check(r != null && r.first == 2 && r.second.contentEquals(byteArrayOf(5, 6)), "ws: read a binary message")
        r = Ws.readFrame(java.io.ByteArrayInputStream(srv(1, "no".toByteArray(), false) + srv(0, "pc".toByteArray())))
        check(r != null && r.first == 1 && String(r.second) == "nopc", "ws: fragments join")
        r = Ws.readFrame(java.io.ByteArrayInputStream(srv(9, byteArrayOf(1))))
        check(r != null && r.first == 9, "ws: a ping comes through on its own")
        check(Ws.readFrame(java.io.ByteArrayInputStream(ByteArray(0))) == null, "ws: end of stream")
        val masked = Ws.frame(2, byteArrayOf(42, 43), mk)              // a masked frame reads back too
        r = Ws.readFrame(java.io.ByteArrayInputStream(masked))
        check(r != null && r.second.contentEquals(byteArrayOf(42, 43)), "ws: masked round trip")
        val huge = byteArrayOf(0x82.toByte(), 127) + ByteArray(8).also { it[6] = 0x20 }
        check(runCatching { Ws.readFrame(java.io.ByteArrayInputStream(huge)) }.isFailure, "ws: an 8 KB message is refused")
        check(Ws.readHead(java.io.ByteArrayInputStream("HTTP/1.1 101 Switching\r\nUpgrade: websocket\r\n\r\nxx".toByteArray()))?.startsWith("HTTP/1.1 101") == true, "ws: the answer's head")
        check(Ws.readHead(java.io.ByteArrayInputStream("HTTP/1.1 101".toByteArray())) == null, "ws: a cut-off head")
    }

    // ---- 2.23: saying what you mean, and "Which one?"
    run {
        fun cmd(said: String, want: String) = check(Words.command(said) == want, "means: \"$said\" -> $want (got ${Words.command(said)})")
        cmd("Loomy show me only the first monitor", "display pc")
        cmd("show me only the first monitor please", "display pc")
        cmd("just the first screen", "display pc")
        cmd("switch to the second", "display second")
        cmd("switch to the second screen", "display second")
        cmd("only use my laptop screen", "display pc")
        cmd("show it on the other monitor only", "display second")
        cmd("turn off the second screen", "display pc")
        cmd("turn off the first monitor", "display second")
        cmd("turn on the second screen", "display extend")
        cmd("use both monitors", "display extend")
        cmd("put the same thing on both screens", "display duplicate")
        cmd("screen one only", "display pc")
        cmd("turn off the screens", "display off")
        cmd("turn the screens back on", "display on")
        cmd("display second", "display second")
        cmd("display", "display")
        cmd("can you open the general system information please", "si general")
        cmd("show me the cpu system info", "si cpu")
        cmd("open system info for the second gpu", "si gpu 2")
        cmd("system information graphics card", "si gpu")
        cmd("open the system information", "si")
        cmd("si gpu 2", "si gpu 2")
        // 2.24: the Phone Screen package
        cmd("mirror my phone", "scrcpy")
        cmd("Loomy i plugged in my phone please display it", "scrcpy")
        cmd("show my phone screen on the pc", "scrcpy")
        cmd("copy screen", "scrcpy")
        cmd("can you cast my android please", "scrcpy")
        cmd("share my phone to my screen", "scrcpy")
        cmd("can you share my phone on the pc please", "scrcpy")
        cmd("mirror", "display duplicate")
        cmd("mirror the screens", "display duplicate")
        check(Words.command("pair my phone") != "scrcpy" && Words.command("call my phone") != "scrcpy", "phone screen: other phone things aren't it")
        check(Words.knows("scrcpy") && !Words.knows("scrcpy now"), "knows: scrcpy, alone")
        check(Words.command("switch to the second tab") != "display second", "means: a tab isn't a screen")
        check(Words.command("open the second monitor settings") !in setOf("display second", "display pc"), "means: unknown words leave it to the AI")

        val q = Words.question("Done.\nWhich one (si)? general, cpu, gpu 1, gpu 2.")
        check(q == ("si" to listOf("general", "cpu", "gpu 1", "gpu 2")), "question: read from the PC's reply ($q)")
        check(Words.question("Which one (web)? gmail, yt, ...") == ("web" to listOf("gmail", "yt")), "question: '...' isn't a choice")
        check(Words.question("Showing General. Views: general, cpu. Say next tab.") == null && Words.question("Done.") == null, "question: none")
        check(Words.withoutQuestion("Done.\nWhich one (si)? general, cpu.") == "", "question: the line comes out of the answer")
        val si = listOf("general", "cpu", "gpu 1"); val two = listOf("general", "cpu", "gpu 1", "gpu 2")
        val lay = listOf("extend", "second", "pc", "duplicate", "off")
        fun pick(said: String, c: String, o: List<String>, want: String?) =
            check(Words.pick(said, c, o) == want, "pick: \"$said\" -> $want (got ${Words.pick(said, c, o)})")
        pick("general please", "si", si, "si general")
        pick("Loomy general", "si", si, "si general")
        pick("the cpu one", "si", si, "si cpu")
        pick("processor", "si", si, "si cpu")
        pick("graphics", "si", si, "si gpu 1")
        pick("gpu one", "si", si, "si gpu 1")
        pick("graphics", "si", two, null)
        pick("the second gpu", "si", two, "si gpu 2")
        pick("gpu two please", "si", two, "si gpu 2")
        pick("the first one", "si", si, "si general")
        pick("the last one", "si", si, "si gpu 1")
        pick("number 2", "si", si, "si cpu")
        pick("overview", "si", si, "si general")
        pick("first screen", "display", lay, "display pc")
        pick("the main one", "display", lay, "display pc")
        pick("second", "display", lay, "display second")
        pick("both", "display", lay, "display extend")
        pick("turn off the second screen", "display", lay, "display pc")
        pick("mirror", "display", lay, "display duplicate")
        pick("youtube", "web", listOf("gmail", "youtube"), "web youtube")
        pick("open gmail please", "web", listOf("gmail", "youtube"), "web gmail")
        pick("display off", "si", si, null)
        pick("what's the weather like today in town", "si", si, null)
        pick("banana", "si", si, null)
        for (c in listOf("cancel", "ok cancel", "never mind", "nevermind", "go back", "forget it", "Loomy never mind", "okay never mind then", "no thanks"))
            check(Words.isCancel(c), "cancel: \"$c\"")
        for (c in listOf("general", "cancel the display", "go back to the cpu", "back up"))
            check(!Words.isCancel(c), "not cancel: \"$c\"")
        check(Words.pretty("si", "gpu 1") == "GPU 1" && Words.pretty("display", "pc") == "First screen only" && Words.ask("si") == "Which view?", "pretty")
    }

    // ---- 2.25: "did League update today?"
    run {
        fun q(said: String, kind: Games.Kind?, game: String?) {
            val r = Words.gameQuestion(said)
            check((kind == null && r == null) || (r != null && r.kind == kind && r.game == game), "game q: \"$said\" -> $kind $game (got ${r?.kind} ${r?.game})")
        }
        q("did league drop an update today", Games.Kind.TODAY, "league")
        q("Loomy did league of legends get a patch today", Games.Kind.TODAY, "league of legends")
        q("is there a league update", Games.Kind.LAST, "league")
        q("is there a new valorant patch", Games.Kind.LAST, "valorant")
        q("when was the last valorant update", Games.Kind.WHEN, "valorant")
        q("how long ago did rust update", Games.Kind.WHEN, "rust")
        q("did counter strike 2 update this morning", Games.Kind.TODAY, "counter strike 2")
        q("has tft been patched", Games.Kind.LAST, "tft")
        q("did league drop an up date today", Games.Kind.TODAY, "league")
        q("update", null, null); q("check for updates", null, null); q("/update", null, null); q("update my games", null, null)
        q("turn off the screens", null, null)
        val z = java.time.ZoneId.of("Europe/Helsinki")
        val now = java.time.ZonedDateTime.of(2026, 11, 7, 20, 0, 0, 0, z).toInstant().toEpochMilli()
        val today = java.time.ZonedDateTime.of(2026, 11, 7, 18, 12, 0, 0, z).toInstant().toEpochMilli()
        val three = java.time.ZonedDateTime.of(2026, 11, 4, 18, 12, 0, 0, z).toInstant().toEpochMilli()
        val late = java.time.ZonedDateTime.of(2026, 11, 6, 23, 50, 0, 0, z).toInstant().toEpochMilli()
        val lol = { t: Long? -> Games.Info("League of Legends", true, t, "15.22.725", now - 86400000L * 5) }
        val rust = { t: Long? -> Games.Info("Rust", false, t, "20711850", null) }
        fun a(k: Games.Kind, i: Games.Info, big: String, small: String) {
            val (b, s2) = Games.answer(k, i, now, z)
            check(b == big && s2 == small, "game a: $k -> \"$b\" / \"$s2\"")
        }
        a(Games.Kind.TODAY, rust(today), "Yes, at 6:12 PM", "Rust \u00B7 Build 20711850")
        a(Games.Kind.TODAY, lol(today), "Yes, at around 6:12 PM", "League of Legends \u00B7 Version 15.22.725")
        a(Games.Kind.TODAY, rust(three), "No", "Rust \u00B7 Build 20711850\nLast update: 3 days ago (Wed 4/11/2026, 6:12 PM)")
        a(Games.Kind.WHEN, rust(three), "3 days ago", "Rust \u00B7 Build 20711850\nWed 4/11/2026, 6:12 PM")
        a(Games.Kind.WHEN, rust(late), "Yesterday at 11:50 PM", "Rust \u00B7 Build 20711850\nFri 6/11/2026, 11:50 PM")
        a(Games.Kind.LAST, rust(three), "Last update: Wed 4/11/2026, 6:12 PM", "Rust \u00B7 Build 20711850")
        a(Games.Kind.TODAY, lol(null), "Not that Ellune saw", "League of Legends: No new patch since Ellune started watching (Mon 2/11/2026).\nVersion 15.22.725")
        // a UTC time in summer shows as EEST (+3)
        val summer = java.time.ZonedDateTime.of(2026, 7, 1, 15, 0, 0, 0, java.time.ZoneOffset.UTC).toInstant().toEpochMilli()
        check(Games.clock(summer, z) == "6:00 PM" && Games.clock(today, z) == "6:12 PM", "game: EEST in summer, EET in winter")
    }

    // ---- 2.23: the update button's checks (Update.kt), against GitHub's real answer for v2.22
    run {
        check(Update.code("2.23") == 22300 && Update.code("v2.9b") == 20902 && Update.code("V2.10") == 21000, "update: version codes like pocket.yml")
        check(Update.code("2.9")!! < Update.code("2.9b")!! && Update.code("2.9c")!! < Update.code("2.10")!!, "update: 2.9 < 2.9b < 2.9c < 2.10")
        check(Update.code("dev-abc1234") == null && Update.code("2.100") == null && Update.code("2.22-rc") == null && Update.code("") == null, "update: odd versions")
        val real = java.io.File("../fixtures/release_v2.22.json").readText()
        val ok = Update.check(real)
        check(ok is Update.Check.Ok && ok.tag == "v2.22" && ok.version == "2.22" && ok.code == 22200 && ok.size == 216739L &&
              ok.sha256 == "ed7796061e844571ffc4b498c2999bd04b4433ba5b4d6369bb8e76ee9e0c6c99" &&
              ok.url == "https://github.com/JimmyUns/Project-Loomy/releases/download/v2.22/pocket-loomy.apk", "update: the real v2.22 release ($ok)")
        fun bad(json: String, what: String) { val r = Update.check(json); check(r is Update.Check.Bad, "update refuses: $what ($r)") }
        bad(real.replaceFirst("\"login\": \"JimmyUns\"", "\"login\": \"LoomyBot\"").replaceFirst("\"login\":\"JimmyUns\"", "\"login\":\"LoomyBot\""), "someone else's release")
        val apkAt = real.indexOf("\"pocket-loomy.apk\"")
        val upl = real.indexOf("github-actions[bot]", apkAt)
        bad(real.substring(0, upl) + "LoomyBot" + real.substring(upl + "github-actions[bot]".length), "the APK uploaded by hand")
        bad(real.replace("pocket-loomy.apk", "other.apk"), "no APK yet")
        bad(real.replace("sha256:ed7796061e844571ffc4b498c2999bd04b4433ba5b4d6369bb8e76ee9e0c6c99", "sha256:xyz"), "a broken checksum")
        bad(real.replace("\"digest\": \"sha256:ed7796061e844571ffc4b498c2999bd04b4433ba5b4d6369bb8e76ee9e0c6c99\"", "\"digest\": null")
                .replace("\"digest\":\"sha256:ed7796061e844571ffc4b498c2999bd04b4433ba5b4d6369bb8e76ee9e0c6c99\"", "\"digest\":null"), "no checksum")
        bad(real.replace("JimmyUns/Project-Loomy/releases/download/v2.22/pocket-loomy.apk", "evil/x/releases/download/v2.22/pocket-loomy.apk"), "a download link elsewhere")
        bad(real.replace("\"size\": 216739", "\"size\": 0").replace("\"size\":216739", "\"size\":0"), "an empty file")
        bad(real.replace("\"draft\": false", "\"draft\": true").replace("\"draft\":false", "\"draft\":true"), "a draft")
        bad(real.replace("\"tag_name\": \"v2.22\"", "\"tag_name\": \"latest\"").replace("\"tag_name\":\"v2.22\"", "\"tag_name\":\"latest\""), "an odd tag")
        bad("<html><body>Sign in to the wifi</body></html>", "a wifi login page")
        bad("", "nothing"); bad("{\"message\":\"Not Found\"}", "GitHub's 'not found'"); bad(real.dropLast(40), "a cut-off answer")
        check(Update.hex(byteArrayOf(0, 15, -1, 16)) == "000fff10" && Update.size(216739) == "212 KB" && Update.size(3L * 1048576) == "3.0 MB", "update: hex, sizes")
        // the little JSON reader
        val j = Json.parse("{\"a\": [1, 2.5, true, null, \"x\\\"y\\u00e9\\n\"], \"b\": {\"c\": -3}}") as Map<*, *>
        check((j["a"] as List<*>) == listOf(1L, 2.5, true, null, "x\"y\u00e9\n") && (j["b"] as Map<*, *>)["c"] == -3L, "json: values")
        check(Json.parse("{\"a\":1,}") == null && Json.parse("[1 2]") == null && Json.parse("{\"a\":1} x") == null && Json.parse("\"\u0001\"") == null, "json: malformed")
        check(Json.parse("[".repeat(100) + "]".repeat(100)) == null && Json.parse(" [ ] ") == listOf<Any?>(), "json: too deep / empty")
    }

    println("$checks checks, $fails failure(s)")
    if (fails > 0) kotlin.system.exitProcess(1)
}
