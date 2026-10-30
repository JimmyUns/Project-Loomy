package io.github.jimmyuns.pocketloomy

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothSocket
import java.io.IOException
import java.util.UUID
import java.util.concurrent.Executors
import java.util.concurrent.ScheduledFuture
import java.util.concurrent.TimeUnit

/** Something the person should read, already in plain words. */
class PocketError(msg: String) : Exception(msg)

// Talks to Loomy over Bluetooth on one background thread (blocking sockets are fine there). One
// connection at a time. After a command it stays open for a minute (the PC hangs up after 90 s of
// silence), so quick follow-ups skip the handshake.
@SuppressLint("MissingPermission")   // MainActivity gets BLUETOOTH_CONNECT before anything reaches here
class Link {
    private val worker = Executors.newSingleThreadScheduledExecutor()
    private val watchdog = Executors.newSingleThreadScheduledExecutor()
    private var sock: BluetoothSocket? = null
    private var h = 0L
    private var idle: ScheduledFuture<*>? = null

    fun pair(device: BluetoothDevice, code: String, myName: ByteArray, done: (Result<Trust>) -> Unit) {
        worker.execute { done(runCatching { doPair(device, code, myName) }) }
    }

    fun send(device: BluetoothDevice, trust: Trust, myName: ByteArray, text: String, done: (Result<String>) -> Unit) {
        worker.execute { done(runCatching { doSend(device, trust, myName, text) }) }
    }

    fun close() { worker.execute { hangUp() } }

    // ------------------------------------------------------------------ pairing
    private fun doPair(device: BluetoothDevice, code: String, name: ByteArray): Trust {
        hangUp()
        h = Native.create()
        try {
            Native.initPhone(h, code.toByteArray(Charsets.US_ASCII), null, null, null, name)
            open(device)
            pumpUntil(Native.EV_PAIRED, 20)
            val keys = Native.myKeys(h)
            val t = Trust(keys.copyOfRange(0, 32), keys.copyOfRange(32, 64), Native.peerKey(h),
                          System.currentTimeMillis(), String(Native.peerName(h), Charsets.UTF_8))
            keys.fill(0)
            return t
        } catch (e: IOException) {
            throw PocketError(unreachable(device))
        } finally {
            hangUp()                                    // pairing ends here; commands use a fresh connection
        }
    }

    // ------------------------------------------------------------------ commands
    private fun doSend(device: BluetoothDevice, trust: Trust, name: ByteArray, text: String): String {
        idle?.cancel(false)
        val bytes = text.toByteArray(Charsets.UTF_8)
        if (bytes.isEmpty()) throw PocketError("Say a command.")
        if (bytes.size > 400) throw PocketError("That command is too long.")
        for (attempt in 0..1) {
            val fresh = h == 0L || sock == null || !Native.ready(h)
            try {
                if (fresh) connect(device, trust, name)
                Native.sendCmd(h, bytes)
                pumpUntil(Native.EV_REPLY, 15)
                val reply = String(Native.text(h), Charsets.UTF_8)
                idle = worker.schedule({ hangUp() }, 60, TimeUnit.SECONDS)
                return reply
            } catch (e: PocketError) {
                hangUp()
                throw e
            } catch (e: IOException) {
                hangUp()
                // a kept connection can go stale (Loomy reloaded, PC slept): one retry on a fresh one
                if (fresh || attempt == 1) throw PocketError(unreachable(device))
            }
        }
        throw PocketError(unreachable(device))
    }

    private fun connect(device: BluetoothDevice, trust: Trust, name: ByteArray) {
        hangUp()
        h = Native.create()
        Native.initPhone(h, null, trust.sk, trust.pk, trust.pcPk, name)
        open(device)
        pumpUntil(Native.EV_READY, 15)
    }

    // ------------------------------------------------------------------ plumbing
    private fun open(device: BluetoothDevice) {
        val s = device.createRfcommSocketToServiceRecord(SERVICE)
        val dog = watchdog.schedule({ quietClose(s) }, 15, TimeUnit.SECONDS)
        try { s.connect() } finally { dog.cancel(false) }
        sock = s
    }

    // feed the engine whatever the PC sends until `wanted` shows up; send whatever the engine queues
    private fun pumpUntil(wanted: Int, secs: Long) {
        val s = sock ?: throw IOException("not connected")
        val dog = watchdog.schedule({ quietClose(s) }, secs, TimeUnit.SECONDS)
        try {
            flushOut()
            val buf = ByteArray(600)
            val input = s.inputStream
            while (true) {
                val n = input.read(buf)
                if (n < 0) throw IOException("hung up")
                var off = 0
                var hit = false
                while (off < n) {
                    val r = Native.feed(h, buf, off, n - off)
                    val used = (r ushr 8).toInt()
                    val ev = (r and 0xff).toInt()
                    off += used
                    if (ev == Native.EV_FAIL) throw PocketError(failMessage(Native.fail(h)))
                    flushOut()
                    if (ev == wanted) hit = true
                    if (used <= 0) break
                }
                if (hit) return
            }
        } finally {
            dog.cancel(false)
        }
    }

    private fun flushOut() {
        val out = Native.takeOut(h)
        if (out.isEmpty()) return
        val os = (sock ?: throw IOException("not connected")).outputStream
        os.write(out)
        os.flush()
    }

    private fun hangUp() {
        idle?.cancel(false)
        idle = null
        sock?.let { quietClose(it) }
        sock = null
        if (h != 0L) { Native.destroy(h); h = 0L }
    }

    private fun quietClose(s: BluetoothSocket) { try { s.close() } catch (_: IOException) {} }

    private fun unreachable(device: BluetoothDevice) =
        "Couldn't reach Loomy on ${device.name ?: device.address}. Check that Bluetooth is on, the PC is close, " +
        "and Loomy has the phone feature on (/pkg add phone). If Loomy shut listening down, Enter in /phone turns it back on."

    private fun failMessage(f: Int) = when (f) {
        Native.F_NOPAIRING -> "Loomy isn't showing a pairing code. Type /phone on the PC first."
        Native.F_CODE -> "Wrong code. Loomy used that one up: press N in /phone for a new one and try again."
        Native.F_NOTRUST -> "Loomy doesn't trust this phone (any more). Pair again."
        Native.F_AUTH -> "Loomy didn't recognise this phone. It may have been forgotten on the PC. Pair again."
        else -> "The connection failed a check (${Native.failText(f)}). Nothing was sent."
    }

    companion object {
        // Pocket Loomy's Bluetooth service; Loomy registers the same UUID (src/phonebt.c)
        val SERVICE: UUID = UUID.fromString("89609207-27e0-4126-90fe-40d16244b0cd")
    }
}
