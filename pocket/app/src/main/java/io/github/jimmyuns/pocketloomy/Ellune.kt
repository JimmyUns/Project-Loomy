package io.github.jimmyuns.pocketloomy

import java.net.URL
import javax.net.ssl.HttpsURLConnection

// Pings Ellune, Loomy's online sister (ellune/ in the repo, a Cloudflare Worker), the same check as /ellune on
// the PC. The only thing in the app that uses the internet, and only when you tap "Ping Ellune": nothing
// about you or the PC is sent, it's a plain GET. Commands to the PC still only go over Bluetooth.
// Plain Java networking (no Android classes), so check() runs in the desktop tests (tests/jni).
object Ellune {
    const val URL_PING = "https://ellune.loomybot.workers.dev/ping"

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

    fun ping(done: (Result) -> Unit) { Thread({ done(pingNow()) }, "ellune-ping").start() }
}
