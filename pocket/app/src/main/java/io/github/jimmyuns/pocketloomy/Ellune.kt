package io.github.jimmyuns.pocketloomy

import java.net.URL
import java.security.MessageDigest
import java.security.PrivateKey
import java.security.Signature
import java.security.interfaces.ECPublicKey
import java.util.Base64
import javax.net.ssl.HttpsURLConnection

// Talks to Ellune, Loomy's online sister (ellune/ in the repo, a Cloudflare Worker). Plain Java (no Android
// classes), so all of it runs in the desktop tests (tests/jni); the key itself lives in ElluneKey.kt.
// - ping: the same check as /ellune on the PC, a plain GET.
// - the guest list (2.21): this phone has its own key (Android Keystore, can't be copied out). Its public half
//   goes on Ellune's list (her DEVICES variable, pasted by Jimmy). Requests are signed with it; Ellune turns
//   away anything else. Format in ellune/src/index.js. Only when you tap: nothing runs in the background.
// Commands to the PC still only go over Bluetooth.
object Ellune {
    const val HOST = "https://ellune.loomybot.workers.dev"
    const val URL_PING = "$HOST/ping"

    sealed class Result {
        data class Up(val version: String, val ms: Long) : Result()
        object Unreachable : Result()     // no internet, blocked, timed out, not a 200
        object NotEllune : Result()       // something answered, but not her (a wifi login page, a reroute)
    }

    fun message(r: Result) = when (r) {
        is Result.Up -> "Ellune is up (v${r.version}, ${r.ms} ms)."
        Result.Unreachable -> "Couldn't reach Ellune. No internet, or this network blocks workers.dev."
        Result.NotEllune -> "Something answered, but it wasn't Ellune. Maybe a wifi login page, or the network is rerouting it."
    }

    // Is this Ellune's real ping reply? Her version (or "?" if it's odd), or null. Like ellune_check() on the PC:
    // needs "ok": true and "service": "ellune", so a captive portal's 200 doesn't count as up.
    private val OK = Regex("\"ok\"\\s*:\\s*true(?![A-Za-z0-9_])")
    private val SERVICE = Regex("\"service\"\\s*:\\s*\"ellune\"")
    private val VERSION = Regex("\"version\"\\s*:\\s*\"([^\"\\\\]*)\"")
    private val TAG = Regex("[0-9A-Za-z.-]{1,15}")

    fun check(body: String?): String? {
        val b = body?.trim() ?: return null
        if (!b.startsWith("{") || !b.endsWith("}")) return null
        if (!OK.containsMatchIn(b) || !SERVICE.containsMatchIn(b)) return null
        val v = VERSION.find(b)?.groupValues?.get(1)
        return if (v != null && TAG.matches(v)) v else "?"
    }

    // Blocking; call it off the main thread (ping() does). Fresh connection each time, so the time includes
    // DNS + the secure handshake, same as the PC.
    fun pingNow(): Result {
        val t0 = System.nanoTime()
        var c: HttpsURLConnection? = null
        try {
            c = URL(URL_PING).openConnection() as HttpsURLConnection
            c.connectTimeout = 8000
            c.readTimeout = 8000
            c.useCaches = false
            c.instanceFollowRedirects = false          // a redirect is a login page, not her
            c.setRequestProperty("Accept", "application/json")
            c.setRequestProperty("Connection", "close")
            if (c.responseCode != 200) return Result.Unreachable
            val buf = ByteArray(512)                   // her reply is ~50 bytes; never read more than this
            var n = 0
            c.inputStream.use { s ->
                while (n < buf.size) { val r = s.read(buf, n, buf.size - n); if (r < 0) break; n += r }
            }
            val ms = (System.nanoTime() - t0) / 1_000_000
            val v = check(String(buf, 0, n, Charsets.UTF_8)) ?: return Result.NotEllune
            return Result.Up(v, ms)
        } catch (_: Exception) {
            return Result.Unreachable
        } finally {
            c?.disconnect()
        }
    }


    // ------------------------------------------------------------------ the guest list
    private val b64 = Base64.getUrlEncoder().withoutPadding()

    // 65 bytes: 04 || X || Y (what Ellune's list holds, base64url)
    fun rawPublic(pub: ECPublicKey): ByteArray {
        fun fixed(v: java.math.BigInteger): ByteArray {
            val b = v.toByteArray().dropWhile { it == 0.toByte() }.toByteArray()
            return ByteArray(32 - b.size) + b
        }
        return byteArrayOf(4) + fixed(pub.w.affineX) + fixed(pub.w.affineY)
    }

    // Java/Android sign ECDSA as DER; Ellune (Web Crypto) wants raw r || s, 32 bytes each. null if it isn't DER.
    fun derToRaw(der: ByteArray): ByteArray? {
        if (der.size < 8 || der[0] != 0x30.toByte() || (der[1].toInt() and 0xff) != der.size - 2) return null
        var i = 2
        fun int(): ByteArray? {
            if (i + 2 > der.size || der[i] != 0x02.toByte()) return null
            val n = der[i + 1].toInt() and 0xff
            if (n == 0 || n > 33 || i + 2 + n > der.size) return null
            var v = der.copyOfRange(i + 2, i + 2 + n)
            i += 2 + n
            v = v.dropWhile { it == 0.toByte() }.toByteArray()
            return if (v.size > 32) null else ByteArray(32 - v.size) + v
        }
        val r = int() ?: return null
        val s = int() ?: return null
        return if (i == der.size) r + s else null
    }

    private fun hex(b: ByteArray) = b.joinToString("") { "%02x".format(it.toInt() and 0xff) }

    // The three headers Ellune checks (see ellune/src/index.js)
    fun signedHeaders(priv: PrivateKey, pub: ByteArray, method: String, path: String, time: Long,
                      body: ByteArray = ByteArray(0)): Map<String, String> {
        val text = "ellune1\n$method\n$path\n$time\n" + hex(MessageDigest.getInstance("SHA-256").digest(body))
        val der = Signature.getInstance("SHA256withECDSA").run { initSign(priv); update(text.toByteArray()); sign() }
        val raw = derToRaw(der) ?: throw IllegalStateException("odd signature")
        return mapOf("x-ellune-key" to b64.encodeToString(pub), "x-ellune-time" to time.toString(),
                     "x-ellune-sig" to b64.encodeToString(raw))
    }

    // Short ID like the pairing one: 40 bits of SHA-256, Crockford base32, XXXX-XXXX. Also its name on the list.
    fun deviceId(pub: ByteArray): String {
        val h = MessageDigest.getInstance("SHA-256").digest(byteArrayOf(*"Ellune id".toByteArray()) + pub)
        var v = 0L
        for (k in 0 until 5) v = (v shl 8) or (h[k].toLong() and 0xff)
        val abc = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"
        val s = (0 until 8).map { abc[((v shr (35 - 5 * it)) and 31).toInt()] }.joinToString("")
        return s.substring(0, 4) + "-" + s.substring(4)
    }

    /** remote (2.22): the relay room both ends know from the pairing: hex(SHA-256("Loomy room" + the PC's key))[0..32] */
    fun room(pcPk: ByteArray): String =
        hex(MessageDigest.getInstance("SHA-256").digest("Loomy room".toByteArray() + pcPk)).substring(0, 32)

    /** is the PC in its room right now? null = couldn't ask. Blocking. */
    fun pcThere(priv: PrivateKey, pub: ByteArray, room: String): Boolean? {
        var c: HttpsURLConnection? = null
        try {
            val path = "/status?room=$room"
            c = URL(HOST + path).openConnection() as HttpsURLConnection
            c.connectTimeout = 6000
            c.readTimeout = 6000
            c.useCaches = false
            c.instanceFollowRedirects = false
            for ((k, v) in signedHeaders(priv, pub, "GET", path, System.currentTimeMillis())) c.setRequestProperty(k, v)
            if (c.responseCode != 200) return null
            val body = c.inputStream.use { String(it.readBytes().take(256).toByteArray(), Charsets.UTF_8) }
            return when {
                Regex("\"pc\"\\s*:\\s*true").containsMatchIn(body) -> true
                Regex("\"pc\"\\s*:\\s*false").containsMatchIn(body) -> false
                else -> null
            }
        } catch (_: Exception) {
            return null
        } finally {
            c?.disconnect()
        }
    }

    // The line to paste into Ellune's DEVICES variable
    fun guestLine(pub: ByteArray) = deviceId(pub) + "=" + b64.encodeToString(pub)

    sealed class Me {
        data class Guest(val name: String) : Me()
        data class NotGuest(val why: String) : Me()
        object Unreachable : Me()
    }

    fun meMessage(m: Me) = when (m) {
        is Me.Guest -> "This phone is on her guest list (${m.name})."
        is Me.NotGuest -> if (m.why == "not on the guest list") "This phone isn't on her guest list yet."
                          else "She didn't accept this phone: ${m.why}."
        Me.Unreachable -> "Couldn't check the guest list."
    }

    private val WHY = Regex("\"error\"\\s*:\\s*\"([A-Za-z0-9 ().,-]{1,60})\"")
    private val NAME = Regex("\"name\"\\s*:\\s*\"([A-Za-z0-9._-]{1,32})\"")

    // What Ellune makes of this phone. Blocking; call it off the main thread.
    fun meNow(priv: PrivateKey, pub: ByteArray, now: Long = System.currentTimeMillis()): Me {
        var c: HttpsURLConnection? = null
        try {
            c = URL("$HOST/me").openConnection() as HttpsURLConnection
            c.connectTimeout = 8000
            c.readTimeout = 8000
            c.useCaches = false
            c.instanceFollowRedirects = false
            for ((k, v) in signedHeaders(priv, pub, "GET", "/me", now)) c.setRequestProperty(k, v)
            val code = c.responseCode
            if (code != 200 && code != 401) return Me.Unreachable
            val buf = ByteArray(512)
            var n = 0
            (if (code == 200) c.inputStream else c.errorStream)?.use { s ->
                while (n < buf.size) { val r = s.read(buf, n, buf.size - n); if (r < 0) break; n += r }
            }
            val body = String(buf, 0, n, Charsets.UTF_8)
            if (!body.trim().startsWith("{") || !body.contains("\"guest\"")) return Me.Unreachable   // a login page, not her
            return if (code == 200) NAME.find(body)?.let { Me.Guest(it.groupValues[1]) } ?: Me.Unreachable
                   else Me.NotGuest(WHY.find(body)?.groupValues?.get(1) ?: "refused")
        } catch (_: Exception) {
            return Me.Unreachable
        } finally {
            c?.disconnect()
        }
    }

    // ------------------------------------------------------------------ listening (the brain, 2.21)
    // /hear: a WAV clip -> what Whisper heard. /understand: words the phone's rules don't know -> a Loomy command.
    sealed class Answer {
        data class Ok(val text: String) : Answer()
        data class Down(val why: String, val notGuest: Boolean = false) : Answer()
    }

    /** "text" -> a JSON string literal */
    fun jsonQuote(t: String): String {
        val b = StringBuilder("\"")
        for (c in t) when {
            c == '"' -> b.append("\\\"")
            c == '\\' -> b.append("\\\\")
            c < ' ' -> b.append("\\u%04x".format(c.code))
            else -> b.append(c)
        }
        return b.append('"').toString()
    }

    /** The string value of "key" in a small flat JSON object, unescaped; null if it isn't there */
    fun jsonString(body: String, key: String): String? {
        val k = body.indexOf("\"$key\"")
        if (k < 0) return null
        var i = k + key.length + 2
        while (i < body.length && body[i].isWhitespace()) i++
        if (i >= body.length || body[i] != ':') return null
        i++
        while (i < body.length && body[i].isWhitespace()) i++
        if (i >= body.length || body[i] != '"') return null
        i++
        val out = StringBuilder()
        while (i < body.length) {
            val c = body[i++]
            if (c == '"') return out.toString()
            if (c != '\\') { out.append(c); continue }
            if (i >= body.length) return null
            when (val e = body[i++]) {
                '"', '\\', '/' -> out.append(e)
                'n' -> out.append('\n'); 't' -> out.append('\t'); 'r' -> out.append('\r')
                'b' -> out.append('\b'); 'f' -> out.append('\u000c')
                'u' -> { if (i + 4 > body.length) return null
                         out.append(body.substring(i, i + 4).toIntOrNull(16)?.toChar() ?: return null); i += 4 }
                else -> return null
            }
        }
        return null
    }

    private fun post(priv: PrivateKey, pub: ByteArray, path: String, body: ByteArray, type: String,
                     key: String, timeoutMs: Int, now: Long = System.currentTimeMillis()): Answer {
        var c: HttpsURLConnection? = null
        try {
            c = URL(HOST + path).openConnection() as HttpsURLConnection
            c.connectTimeout = 5000
            c.readTimeout = timeoutMs
            c.useCaches = false
            c.instanceFollowRedirects = false
            c.requestMethod = "POST"
            c.doOutput = true
            c.setFixedLengthStreamingMode(body.size)
            c.setRequestProperty("Content-Type", type)
            for ((k, v) in signedHeaders(priv, pub, "POST", path, now, body)) c.setRequestProperty(k, v)
            c.outputStream.use { it.write(body) }
            val code = c.responseCode
            val buf = ByteArray(2048)
            var n = 0
            (if (code == 200) c.inputStream else c.errorStream)?.use { s ->
                while (n < buf.size) { val r = s.read(buf, n, buf.size - n); if (r < 0) break; n += r }
            }
            val text = String(buf, 0, n, Charsets.UTF_8)
            if (code == 200) return jsonString(text, key)?.let { Answer.Ok(it) } ?: Answer.Down("odd answer")
            val why = jsonString(text, "error") ?: "HTTP $code"
            return Answer.Down(why, notGuest = code == 401)
        } catch (e: java.net.SocketTimeoutException) {
            return Answer.Down("too slow")
        } catch (_: Exception) {
            return Answer.Down("no connection")
        } finally {
            c?.disconnect()
        }
    }

    // ------------------------------------------------------------------ registration with Authy (0.5)
    /** a signed request; (HTTP status, body). 0 = couldn't reach her. Blocking. */
    private fun request(priv: PrivateKey, pub: ByteArray, method: String, path: String, body: ByteArray?): Pair<Int, String> {
        var c: HttpsURLConnection? = null
        try {
            c = URL(HOST + path).openConnection() as HttpsURLConnection
            c.connectTimeout = 6000; c.readTimeout = 8000; c.useCaches = false; c.instanceFollowRedirects = false
            c.requestMethod = method
            for ((k, v) in signedHeaders(priv, pub, method, path, System.currentTimeMillis(), body ?: ByteArray(0))) c.setRequestProperty(k, v)
            if (body != null) {
                c.doOutput = true
                c.setFixedLengthStreamingMode(body.size)
                c.setRequestProperty("Content-Type", "application/json")
                c.outputStream.use { it.write(body) }
            }
            val code = c.responseCode
            val text = (if (code < 400) c.inputStream else c.errorStream)?.use { String(it.readBytes().take(8192).toByteArray(), Charsets.UTF_8) } ?: ""
            return code to text
        } catch (_: Exception) {
            return 0 to ""
        } finally {
            c?.disconnect()
        }
    }

    private fun why(code: Int, text: String) = when (code) {
        403 -> "Wrong Authy code (or one already used): wait for the next one."
        429 -> "Locked for an hour after 5 wrong codes."
        503 -> "Ellune's registration isn't set up yet (the Authy secret)."
        0 -> "Couldn't reach Ellune."
        else -> jsonString(text, "error") ?: "Ellune said $code."
    }

    /** this device onto her guest list with the current Authy code; null = done, else why not */
    fun registerNow(priv: PrivateKey, pub: ByteArray, code: String, name: String): String? {
        val (st, text) = request(priv, pub, "POST", "/register", "{\"code\":${jsonQuote(code)},\"name\":${jsonQuote(name)}}".toByteArray())
        return if (st == 200) null else why(st, text)
    }

    class Guest(val name: String, val key: String, val fromApp: Boolean)

    /** everyone on her guest list; null = couldn't ask */
    fun devicesNow(priv: PrivateKey, pub: ByteArray): List<Guest>? {
        val (st, text) = request(priv, pub, "GET", "/devices", null)
        if (st != 200) return null
        return Regex("\\{\"name\":\"([A-Za-z0-9._-]{1,32})\",\"key\":\"([A-Za-z0-9_-]{80,100})\",\"from\":\"(app|dashboard)\"\\}")
            .findAll(text).map { Guest(it.groupValues[1], it.groupValues[2], it.groupValues[3] == "app") }.toList()
    }

    /** throw a device out (one added from the app); null = done */
    fun removeNow(priv: PrivateKey, pub: ByteArray, key: String, code: String): String? {
        val (st, text) = request(priv, pub, "POST", "/remove", "{\"key\":${jsonQuote(key)},\"code\":${jsonQuote(code)}}".toByteArray())
        return if (st == 200) null else why(st, text)
    }

    /** A clip -> what was said ("" = nothing). Blocking. */
    fun hearNow(priv: PrivateKey, pub: ByteArray, wav: ByteArray): Answer =
        post(priv, pub, "/hear", wav, "audio/wav", "heard", 8000)

    /** Words the phone's rules don't know -> a Loomy command ("" = not one). Blocking. */
    fun understandNow(priv: PrivateKey, pub: ByteArray, text: String): Answer =
        post(priv, pub, "/understand", "{\"text\":${jsonQuote(text.take(300))}}".toByteArray(), "application/json", "command", 6000)

    // ------------------------------------------------------------------ 2.24d: talking back (Ellune 0.9 talk mode)
    /** what she wants to do: "do" a command, "ask" a question, "say" something, or "none" */
    class Talk(val kind: String, val command: String, val text: String)

    /** the conversation so far: (what he said, her line "DO: x" / "ASK: ..." / "SAY: ...") */
    fun talkNow(priv: PrivateKey, pub: ByteArray, text: String, history: List<Pair<String, String>>): Pair<Talk?, String?> {
        val h = history.takeLast(6).joinToString(",") { "{\"u\":${jsonQuote(it.first.take(300))},\"a\":${jsonQuote(it.second.take(200))}}" }
        val body = "{\"text\":${jsonQuote(text.take(300))},\"talk\":true,\"history\":[$h]}"
        val (st, out) = request(priv, pub, "POST", "/understand", body.toByteArray())
        if (st != 200) return null to when (st) {
            0 -> "couldn't reach her"
            401 -> "this phone isn't on her guest list"
            503 -> jsonString(out, "error") ?: "busy"
            else -> jsonString(out, "error") ?: "error $st"
        }
        val kind = jsonString(out, "kind") ?: return Talk("do", jsonString(out, "command") ?: "", "") to null   // an older Ellune
        return Talk(kind, jsonString(out, "command") ?: "", jsonString(out, "text") ?: "") to null
    }

    // ------------------------------------------------------------------ 2.25: when did a game last update (0.8)
    /** a whole number at "key" (null if it's null or missing) */
    fun jsonNumber(body: String, key: String): Long? {
        val m = Regex("\"" + Regex.escape(key) + "\"\\s*:\\s*(-?\\d{1,16})").find(body) ?: return null
        return m.groupValues[1].toLongOrNull()
    }

    /** Ellune's answer about a game, or why not (left). Blocking. */
    fun gameNow(priv: PrivateKey, pub: ByteArray, q: String): Pair<Games.Info?, String?> {
        val (st, text) = request(priv, pub, "POST", "/game", "{\"q\":${jsonQuote(q.take(60))}}".toByteArray())
        if (st != 200) return null to when (st) {
            0 -> "Couldn't reach Ellune."
            401 -> "This phone isn't on Ellune's guest list."
            // a 404 without her "no game called" means she doesn't have /game at all: an older Ellune
            404 -> if (jsonString(text, "error")?.startsWith("no game") == true) "Ellune found no game called \u201C$q\u201D."
                   else "Ellune is out of date: she needs the 0.8 update (npx wrangler deploy in ellune/)."
            else -> jsonString(text, "error") ?: "Ellune said $st."
        }
        val name = jsonString(text, "name") ?: return null to "Ellune's answer didn't make sense."
        return Games.Info(name, jsonString(text, "kind") == "riot", jsonNumber(text, "time"),
                          jsonString(text, "version") ?: "", jsonNumber(text, "since")) to null
    }
}
