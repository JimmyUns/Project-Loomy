package io.github.jimmyuns.pocketloomy

import java.io.ByteArrayOutputStream
import java.io.IOException
import java.io.InputStream
import java.security.MessageDigest
import java.security.SecureRandom
import java.util.Base64

/** the WebSocket bits (RFC 6455), plain functions */
object Ws {
    private val rnd = SecureRandom()
    fun mask(): ByteArray = ByteArray(4).also { rnd.nextBytes(it) }

    /** one client frame (FIN, masked, as clients must) */
    fun frame(op: Int, payload: ByteArray, mask: ByteArray): ByteArray {
        val n = payload.size
        val head = when {
            n < 126 -> byteArrayOf((0x80 or op).toByte(), (0x80 or n).toByte())
            n < 65536 -> byteArrayOf((0x80 or op).toByte(), (0x80 or 126).toByte(), (n shr 8).toByte(), n.toByte())
            else -> throw IOException("too big")
        }
        val out = ByteArray(head.size + 4 + n)
        System.arraycopy(head, 0, out, 0, head.size)
        System.arraycopy(mask, 0, out, head.size, 4)
        for (i in 0 until n) out[head.size + 4 + i] = (payload[i].toInt() xor mask[i and 3].toInt()).toByte()
        return out
    }

    /** (opcode, payload) of the next whole message from the server; null at the end. Messages over 4 KB end it. */
    fun readFrame(inp: InputStream): Pair<Int, ByteArray>? {
        var op = -1
        val all = ByteArrayOutputStream()
        while (true) {
            val b0 = inp.read(); if (b0 < 0) return null
            val b1 = inp.read(); if (b1 < 0) return null
            var n = (b1 and 0x7f).toLong()
            if (n == 126L) n = ((readN(inp, 1) shl 8) or readN(inp, 1)).toLong()
            else if (n == 127L) { n = 0; repeat(8) { n = (n shl 8) or readN(inp, 1).toLong() } }
            if (n > 4096 || all.size() + n > 4096) throw IOException("message too big")
            val mk = if (b1 and 0x80 != 0) ByteArray(4).also { readFully(inp, it) } else null
            val p = ByteArray(n.toInt()).also { readFully(inp, it) }
            if (mk != null) for (i in p.indices) p[i] = (p[i].toInt() xor mk[i and 3].toInt()).toByte()
            val code = b0 and 0x0f
            if (code >= 8) return code to p                  // control frames stand alone
            if (op < 0) op = code
            all.write(p)
            if (b0 and 0x80 != 0) return op to all.toByteArray()
        }
    }

    private fun readN(inp: InputStream, n: Int): Int { val v = inp.read(); if (v < 0) throw IOException("cut off"); return v }
    private fun readFully(inp: InputStream, b: ByteArray) {
        var o = 0
        while (o < b.size) { val r = inp.read(b, o, b.size - o); if (r < 0) throw IOException("cut off"); o += r }
    }

    /** the HTTP answer's head (status line + headers), at most 4 KB */
    fun readHead(inp: InputStream): String? {
        val b = ByteArrayOutputStream()
        while (b.size() < 4096) {
            val c = inp.read(); if (c < 0) return null
            b.write(c)
            val a = b.toByteArray()
            if (a.size >= 4 && a[a.size - 4] == '\r'.code.toByte() && a[a.size - 3] == '\n'.code.toByte() &&
                a[a.size - 2] == '\r'.code.toByte() && a[a.size - 1] == '\n'.code.toByte()) return String(a, Charsets.ISO_8859_1)
        }
        return null
    }

    fun accept(key: String): String = Base64.getEncoder().encodeToString(
        MessageDigest.getInstance("SHA-1").digest((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").toByteArray()))
}
