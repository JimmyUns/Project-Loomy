package io.github.jimmyuns.pocketloomy

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.service.quicksettings.TileService

// App-wide state and actions shared by the screen, the Talk popup, the notification, the tile and
// the service. One Link for everything, so they all queue on the same Bluetooth thread.
@SuppressLint("MissingPermission")   // ready() checks BLUETOOTH_CONNECT before any Bluetooth call
object Pocket {
    @Volatile var linkState = LinkState.IDLE
    @Volatile var lastSeen = 0L           // last time the PC answered anything
    @Volatile var reachable = true        // did the last try reach it?
    val link = Link().also { it.onState = { s -> linkState = s; changed() } }

    private val main = Handler(Looper.getMainLooper())
    private val listeners = mutableListOf<() -> Unit>()
    fun listen(l: () -> Unit) { listeners += l }
    fun unlisten(l: () -> Unit) { listeners -= l }
    fun changed() { main.post { for (l in listeners.toList()) l() } }

    fun mark(ok: Boolean) {
        if (ok) lastSeen = System.currentTimeMillis()
        reachable = ok
        changed()
    }

    fun myName(ctx: Context): ByteArray {
        var n = Settings.Global.getString(ctx.contentResolver, "device_name") ?: Build.MODEL ?: "Android phone"
        while (n.toByteArray(Charsets.UTF_8).size > 48) n = n.dropLast(1)
        return n.toByteArray(Charsets.UTF_8)
    }

    fun adapter(ctx: Context): BluetoothAdapter? = ctx.getSystemService(BluetoothManager::class.java)?.adapter

    fun canConnect(ctx: Context) = Build.VERSION.SDK_INT < 31 ||
        ctx.checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED

    /** null = good to go; otherwise why not, in words */
    fun ready(ctx: Context): String? {
        val s = Store(ctx)
        val t = s.trust()
        return when {
            s.pcAddress == null -> "Choose your PC in the app first."
            t == null -> "Pair with your PC in the app first."
            !t.valid() -> "The pairing ran out (7 days). Pair again in the app."
            !canConnect(ctx) -> "Allow Nearby devices for Pocket Loomy (open the app)."
            adapter(ctx) == null -> "This phone has no Bluetooth."
            adapter(ctx)?.isEnabled != true -> "Turn Bluetooth on."
            else -> null
        }
    }

    fun device(ctx: Context): BluetoothDevice? = Store(ctx).pcAddress?.let { adapter(ctx)?.getRemoteDevice(it) }

    fun pcName(ctx: Context) = Store(ctx).trust()?.pcName ?: Store(ctx).pcLabel ?: "your PC"

    /** the On switch. Returns why it couldn't, or null. */
    fun turnOn(ctx: Context): String? {
        ready(ctx)?.let { Notes.off(ctx, it); return it }
        try {
            ctx.startForegroundService(Intent(ctx, PocketService::class.java))
        } catch (e: Exception) {          // Android refuses some background starts
            val why = "Open Pocket Loomy to turn it on."
            Notes.off(ctx, why)
            return why
        }
        Notes.clearOff(ctx)
        refreshTile(ctx)
        return null
    }

    fun turnOff(ctx: Context, why: String? = null) {
        ctx.stopService(Intent(ctx, PocketService::class.java))
        Store(ctx).on = false
        Notes.off(ctx, why)
        refreshTile(ctx)
        changed()
    }

    /** the service died with the app (no onDestroy): don't pretend it's still on */
    fun fixStaleOn(ctx: Context) {
        val s = Store(ctx)
        if (s.on && !PocketService.running) s.on = false
    }

    /** a failed attempt: tell the PC so it shows there too */
    fun reportMiss(ctx: Context, heard: String) {
        if (ready(ctx) != null) return
        val d = device(ctx) ?: return
        val t = Store(ctx).trust() ?: return
        link.note(d, t, myName(ctx), heard)
    }

    fun refreshTile(ctx: Context) {
        try { TileService.requestListeningState(ctx, ComponentName(ctx, PocketTile::class.java)) } catch (_: Exception) {}
    }
}
