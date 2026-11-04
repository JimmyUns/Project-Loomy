package io.github.jimmyuns.pocketloomy

import java.io.ByteArrayOutputStream
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream
import java.net.InetSocketAddress
import java.net.Socket
import java.security.MessageDigest
import java.security.SecureRandom
import java.util.Base64
import javax.net.ssl.HttpsURLConnection
import javax.net.ssl.SSLSocket
import javax.net.ssl.SSLSocketFactory

/** the PC isn't in its room on Ellune (off, asleep, Loomy not running, or remote not allowed in /remote) */
class NoPcException : IOException("pc offline")

// Remote (2.22): the same Pipe as Bluetooth, but through Ellune's relay. A tiny WebSocket client (no libraries):
// wss://ellune.../phone?room=R, guest-list signed. R comes from the pairing (Ellune.room), so the PC is waiting in
// the same room. Bytes written are sent as one binary message per flush; bytes read are the PC's messages, end to
// end encrypted by pocket.c exactly as over Bluetooth: Ellune only passes them on. Plain Java, so tests/jni can
// check the frame code.
class EllunePipe private constructor(private val sock: Socket) : Pipe {
    private val rin = sock.getInputStream()
    private val rout = sock.getOutputStream()

    override val input: InputStream = object : InputStream() {
        private var cur = ByteArray(0)
        private var pos = 0
        override fun read(): Int { val b = ByteArray(1); return if (read(b, 0, 1) < 0) -1 else b[0].toInt() and 0xff }
        override fun read(b: ByteArray, off: Int, len: Int): Int {
            while (pos >= cur.size) cur = nextMessage() ?: return -1
            val n = minOf(len, cur.size - pos)
            System.arraycopy(cur, pos, b, off, n)
            pos += n
            if (pos >= cur.size) { cur = ByteArray(0); pos = 0 }
            return n
        }
    }

    override val output: OutputStream = object : OutputStream() {
        private val buf = ByteArrayOutputStream()
        override fun write(b: Int) { buf.write(b) }
        override fun write(b: ByteArray, off: Int, len: Int) { buf.write(b, off, len) }
        override fun flush() {
            if (buf.size() == 0) return
            synchronized(rout) { rout.write(Ws.frame(2, buf.toByteArray(), Ws.mask())); rout.flush() }
            buf.reset()
        }
    }

    override fun close() {
        try { synchronized(rout) { rout.write(Ws.frame(8, byteArrayOf(0x03, 0xE8.toByte()), Ws.mask())); rout.flush() } } catch (_: Exception) {}
        try { sock.close() } catch (_: Exception) {}
    }

    // the next binary message from the PC; null when the room closed. Pings are answered; "nopc" = the PC isn't there.
    private fun nextMessage(): ByteArray? {
        while (true) {
            val f = Ws.readFrame(rin) ?: return null
            when (f.first) {
                2 -> return f.second
                1 -> if (String(f.second, Charsets.UTF_8) == "nopc") throw NoPcException()
                8 -> return null
                9 -> synchronized(rout) { rout.write(Ws.frame(10, f.second, Ws.mask())); rout.flush() }
                else -> {}
            }
        }
    }

    companion object {
        /** connect + upgrade (blocking, about 10 s at most). PocketError when Ellune says no. */
        fun open(room: String, key: ElluneKey.Key): EllunePipe {
            val host = Ellune.HOST.removePrefix("https://")
            val raw = Socket()
            raw.connect(InetSocketAddress(host, 443), 8000)
            raw.soTimeout = 10000
            val s = (SSLSocketFactory.getDefault() as SSLSocketFactory).createSocket(raw, host, 443, true) as SSLSocket
            try {
                s.startHandshake()
                if (!HttpsURLConnection.getDefaultHostnameVerifier().verify(host, s.session)) throw IOException("wrong certificate")
                val path = "/phone?room=$room"
                val wsKey = Base64.getEncoder().encodeToString(ByteArray(16).also { SecureRandom().nextBytes(it) })
                val req = StringBuilder()
                    .append("GET $path HTTP/1.1\r\nHost: $host\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n")
                    .append("Sec-WebSocket-Key: $wsKey\r\nSec-WebSocket-Version: 13\r\nUser-Agent: PocketLoomy\r\n")
                for ((k, v) in Ellune.signedHeaders(key.priv, key.pub, "GET", path, System.currentTimeMillis())) req.append("$k: $v\r\n")
                req.append("\r\n")
                s.outputStream.write(req.toString().toByteArray(Charsets.US_ASCII))
                s.outputStream.flush()
                val head = Ws.readHead(s.inputStream) ?: throw IOException("no answer")
                val status = head.lineSequence().first().split(' ').getOrNull(1)?.toIntOrNull() ?: 0
                when (status) {
                    101 -> {}
                    401 -> throw PocketError("Ellune didn't let this phone in: add it to her guest list (Settings, Ellune).")
                    503 -> throw PocketError("Ellune can't do remote yet (she needs the 0.4 update).")
                    else -> throw IOException("Ellune said $status")
                }
                val accept = Regex("(?im)^sec-websocket-accept:\\s*(\\S+)").find(head)?.groupValues?.get(1)
                if (accept != Ws.accept(wsKey)) throw IOException("not a websocket")
                s.soTimeout = 0                              // from here Link's watchdog decides how long to wait
                return EllunePipe(s)
            } catch (e: Exception) {
                try { s.close() } catch (_: Exception) {}
                throw e
            }
        }
    }
}
