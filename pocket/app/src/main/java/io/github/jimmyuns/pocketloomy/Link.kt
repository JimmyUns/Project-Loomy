package io.github.jimmyuns.pocketloomy

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothSocket
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream
import java.util.UUID
import java.util.concurrent.Executors
import java.util.concurrent.ScheduledFuture
import java.util.concurrent.TimeUnit

/** Something the person should read, already in plain words. */
class PocketError(msg: String) : Exception(msg)

enum class LinkState { IDLE, CONNECTING, CONNECTED }

/**
 * A pipe the encrypted bytes travel through. Link (and the security in pocket.c) never cares what it is:
 * Bluetooth (BluetoothPipe), or Ellune's relay (EllunePipe, remote, 2.22). The protocol never changes.
 */
interface Pipe {
    val input: InputStream
    val output: OutputStream
    fun close()
}

/** the Bluetooth pipe: RFCOMM to Loomy's service on the PC */
@SuppressLint("MissingPermission")
class BluetoothPipe private constructor(private val s: BluetoothSocket) : Pipe {
    override val input: InputStream get() = s.inputStream
    override val output: OutputStream get() = s.outputStream
    override fun close() { try { s.close() } catch (_: IOException) {} }

    companion object {
        /** connects (blocking); the watchdog closes it if that takes too long */
        fun open(device: BluetoothDevice, watchdog: (Pipe) -> () -> Unit): BluetoothPipe {
            val p = BluetoothPipe(device.createRfcommSocketToServiceRecord(Link.SERVICE))
            val cancel = watchdog(p)
            try { p.s.connect() } finally { cancel() }
            return p
        }
    }
}

// Talks to Loomy over Bluetooth on one background thread (blocking sockets are fine there), so the
// screen, the notification and the 5-minute ping never step on each other. One connection at a time.
// After use it stays open for a minute (the PC hangs up after 90 s of silence), so quick follow-ups
// skip the handshake.
@SuppressLint("MissingPermission")   // callers check BLUETOOTH_CONNECT before anything reaches here
class Link {
    private val worker = Executors.newSingleThreadScheduledExecutor()
    private val watchdog = Executors.newSingleThreadScheduledExecutor()
    private var pipe: Pipe? = null
    private var onAwait: (() -> Unit)? = null           // pairing: called once when the PC starts asking
    private var h = 0L
    private var idle: ScheduledFuture<*>? = null

    /** remote (2.22): set in Ellune mode, null in Bluetooth mode (Pocket.syncPipe). Pairing is always Bluetooth. */
    @Volatile var viaEllune: (() -> Pipe)? = null

    /** called on the worker thread whenever the connection changes */
    var onState: (LinkState) -> Unit = {}

    /** waiting(id): the code was right and the PC now asks you to confirm; show this phone's ID */
    /** pairing: over Bluetooth, or (2.22, QR) through Ellune's pairing room when `via` is given */
    fun pair(device: BluetoothDevice?, code: String, myName: ByteArray, waiting: (String) -> Unit, via: (() -> Pipe)? = null, done: (Result<Trust>) -> Unit) {
        worker.execute { done(runCatching { doPair(device, code, myName, waiting, via) }) }
    }

    /** a command; the result is Loomy's answer */
    fun send(device: BluetoothDevice?, trust: Trust, myName: ByteArray, text: String, done: (Result<String>) -> Unit) {
        worker.execute {
            done(runCatching {
                val bytes = text.toByteArray(Charsets.UTF_8)
                if (bytes.isEmpty()) throw PocketError("Say a command.")
                if (bytes.size > 480) throw PocketError("That command is too long.")
                session(device, trust, myName) {
                    Native.sendCmd(h, bytes)
                    pumpUntil(Native.EV_REPLY, 15)
                    String(Native.text(h), Charsets.UTF_8)
                }
            })
        }
    }

    /** "still here": the PC shows it as last seen */
    fun ping(device: BluetoothDevice?, trust: Trust, myName: ByteArray, done: (Result<Unit>) -> Unit) {
        worker.execute {
            done(runCatching { session(device, trust, myName) { Native.sendPing(h); pumpUntil(Native.EV_PONG, 15) } })
        }
    }

    /** a failed attempt, so the PC can show it too. Best effort: nobody waits for it. */
    fun note(device: BluetoothDevice?, trust: Trust, myName: ByteArray, heard: String) {
        worker.execute {
            runCatching {
                var b = heard.toByteArray(Charsets.UTF_8)
                if (b.size > 480) b = b.copyOf(480)
                session(device, trust, myName) { Native.sendNote(h, b); flushOut() }
            }
        }
    }

    /** tell the PC this phone forgot it, then hang up */
    fun forget(device: BluetoothDevice?, trust: Trust, myName: ByteArray, done: (Result<Unit>) -> Unit) {
        worker.execute {
            done(runCatching {
                try { session(device, trust, myName) { Native.sendForget(h); flushOut() } } finally { hangUp() }
            })
        }
    }

    fun close() { worker.execute { hangUp() } }

    // ------------------------------------------------------------------ pairing
    private fun doPair(device: BluetoothDevice?, code: String, name: ByteArray, waiting: (String) -> Unit, via: (() -> Pipe)? = null): Trust {
        hangUp()
        h = Native.create()
        var asked = false
        onAwait = { if (!asked && Native.awaitingPc(h)) { asked = true; waiting(Native.phoneId(h)) } }
        try {
            Native.initPhone(h, code.toByteArray(Charsets.US_ASCII), null, null, null, name)
            if (via == null) open(device, bluetooth = true)   // pairing in person: over Bluetooth, or the QR (via Ellune)
            else {
                onState(LinkState.CONNECTING)
                pipe = try { via() } catch (e: NoPcException) { throw PocketError("Your PC isn't waiting: open /remote on the PC and press N for a new code.") }
            }
            pumpUntil(Native.EV_PAIRED, 80)                  // includes up to a minute for Enter on the PC
            val keys = Native.myKeys(h)
            val t = Trust(keys.copyOfRange(0, 32), keys.copyOfRange(32, 64), Native.peerKey(h),
                          System.currentTimeMillis(), String(Native.peerName(h), Charsets.UTF_8))
            keys.fill(0)
            return t
        } catch (e: IOException) {
            if (asked) throw PocketError("Loomy didn't confirm it (Esc on the PC, or a minute went by). Pair again with a new code.")
            throw PocketError(unreachable(device))
        } finally {
            onAwait = null
            hangUp()                                    // pairing ends here; commands use a fresh connection
        }
    }

    // ------------------------------------------------------------------ a trusted connection
    // run `op` on a ready connection: reuse the kept one, or connect. A kept one can go stale
    // (Loomy reloaded, the PC slept), so an I/O error on it gets one retry on a fresh connection.
    private fun <T> session(device: BluetoothDevice?, trust: Trust, name: ByteArray, op: () -> T): T {
        idle?.cancel(false)
        for (attempt in 0..1) {
            val fresh = h == 0L || pipe == null || !Native.ready(h)
            try {
                if (fresh) connect(device, trust, name)
                val r = op()
                idle = worker.schedule({ hangUp() }, 60, TimeUnit.SECONDS)
                return r
            } catch (e: PocketError) {
                hangUp()
                throw e
            } catch (e: NoPcException) {
                hangUp()
                throw PocketError("Your PC isn't connected to Ellune: it's off or asleep, Loomy isn't running, or remote is off in /remote (R).")
            } catch (e: IOException) {
                hangUp()
                if (fresh || attempt == 1) throw PocketError(unreachable(device))
            }
        }
        throw PocketError(unreachable(device))
    }

    private fun connect(device: BluetoothDevice?, trust: Trust, name: ByteArray) {
        hangUp()
        h = Native.create()
        Native.initPhone(h, null, trust.sk, trust.pk, trust.pcPk, name)
        open(device)
        pumpUntil(Native.EV_READY, 15)
        onState(LinkState.CONNECTED)
    }

    // ------------------------------------------------------------------ plumbing
    private fun open(device: BluetoothDevice?, bluetooth: Boolean = false) {
        onState(LinkState.CONNECTING)
        val ellune = if (bluetooth) null else viaEllune
        if (ellune != null) { pipe = ellune(); return }
        if (device == null) throw PocketError("No PC chosen for Bluetooth: pick it in the app, or use Ellune mode.")
        pipe = BluetoothPipe.open(device) { p ->
            val dog = watchdog.schedule({ p.close() }, 15, TimeUnit.SECONDS)
            val cancel: () -> Unit = { dog.cancel(false) }
            cancel
        }
    }

    // feed the engine whatever the PC sends until `wanted` shows up; send whatever the engine queues
    private fun pumpUntil(wanted: Int, secs: Long) {
        val s = pipe ?: throw IOException("not connected")
        val dog = watchdog.schedule({ s.close() }, secs, TimeUnit.SECONDS)
        try {
            flushOut()
            val buf = ByteArray(600)
            val input = s.input
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
                    onAwait?.invoke()
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
        val os = (pipe ?: throw IOException("not connected")).output
        os.write(out)
        os.flush()
    }

    private fun hangUp() {
        idle?.cancel(false)
        idle = null
        val had = pipe != null
        pipe?.close()
        pipe = null
        if (h != 0L) { Native.destroy(h); h = 0L }
        if (had) onState(LinkState.IDLE)
    }

    private fun unreachable(device: BluetoothDevice?) = if (viaEllune != null)
        "Couldn't reach your PC through Ellune. Check the phone's internet, and that Loomy is running with remote allowed (/remote, R)." else
        "Couldn't reach Loomy on ${device?.name ?: device?.address ?: "your PC"}. Check that Bluetooth is on, the PC is close, " +
        "and Loomy has the phone feature on (/pkg add phone). If Loomy shut listening down, Enter in /remote turns it back on."

    private fun failMessage(f: Int) = when (f) {
        Native.F_NOPAIRING -> "Loomy isn't showing a pairing code. Type /remote on the PC first."
        Native.F_CODE -> "Wrong code. Loomy used that one up: press N in /remote for a new one and try again."
        Native.F_NOTRUST -> "Loomy doesn't trust this phone (any more). Pair again."
        Native.F_AUTH -> "Loomy didn't recognise this phone. It may have been forgotten on the PC. Pair again."
        Native.F_VERSION -> "Loomy on the PC and this app speak different versions. Update both to the same release."
        else -> "The connection failed a check (${Native.failText(f)}). Nothing was sent."
    }

    companion object {
        // Pocket Loomy's Bluetooth service; Loomy registers the same UUID (src/phonebt.c)
        val SERVICE: UUID = UUID.fromString("89609207-27e0-4126-90fe-40d16244b0cd")
    }
}
