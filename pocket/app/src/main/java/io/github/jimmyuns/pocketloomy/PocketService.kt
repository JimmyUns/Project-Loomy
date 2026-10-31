package io.github.jimmyuns.pocketloomy

import android.Manifest
import android.app.Service
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.net.TrafficStats
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.PowerManager
import android.os.SystemClock
import java.util.concurrent.Executors
import java.util.concurrent.ScheduledExecutorService
import java.util.concurrent.TimeUnit

// The one background piece. It runs while any of these is on, and owns the one notification:
//   - Pocket Loomy on: checks in with the PC every 5 minutes (turns itself off after 3 misses in a row)
//   - listening (Voice): the microphone is in use and shown as such
//   - the net speed meter: reads the phone's byte counters once a second, only while the screen is on
// Android needs a "type" for what a foreground service does; it's updated as things switch on and off.
class PocketService : Service() {
    private var pings: ScheduledExecutorService? = null
    private var misses = 0
    private val main = Handler(Looper.getMainLooper())
    private var lastRx = -1L
    private var lastTx = -1L
    private var lastAt = 0L
    private var down = -1L
    private var up = -1L
    private var ticking = false
    private var screenReceiver: BroadcastReceiver? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        running = true
        Voice.onChange = { main.post { sync() } }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        // listening first, so the microphone is part of the service's type from the very start
        // (Android only allows that while the app is on screen, which it is when Talk was pressed)
        if (intent?.action == LISTEN && checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED)
            Voice.start(this, intent.getBooleanExtra(KEEP, false))
        // Android wants the notification up at once, whatever happens next
        if (!foreground()) { stopSelf(); return START_NOT_STICKY }
        sync()
        return START_NOT_STICKY
    }

    // what's on right now -> the service's type, pings, the meter, or stopping
    private fun sync() {
        val s = Store(this)
        val listening = Voice.state != Voice.State.OFF
        if (!s.on && !s.netSpeed && !listening) { stopSelf(); return }
        foreground()
        if (s.on && pings == null) {
            misses = 0
            pings = Executors.newSingleThreadScheduledExecutor().also { it.scheduleWithFixedDelay({ ping() }, 0, 5, TimeUnit.MINUTES) }
        } else if (!s.on && pings != null) { pings?.shutdownNow(); pings = null }
        if (s.netSpeed) meterOn() else meterOff()
        Notes.update(this, if (s.netSpeed) down.coerceAtLeast(0) else -1, if (s.netSpeed) up.coerceAtLeast(0) else -1)
    }

    private fun foreground(): Boolean {
        val s = Store(this)
        val n = Notes.service(this, if (s.netSpeed) down.coerceAtLeast(0) else -1, if (s.netSpeed) up.coerceAtLeast(0) else -1)
        return try {
            if (Build.VERSION.SDK_INT < 30) { startForeground(Notes.ON_ID, n); return true }
            var t = 0
            val listening = Voice.state != Voice.State.OFF
            if ((s.on || listening) && Pocket.canConnect(this)) t = t or ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
            if (listening && checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED)
                t = t or ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE
            if (t == 0 && Build.VERSION.SDK_INT >= 34) t = ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE
            if (t == 0) startForeground(Notes.ON_ID, n) else startForeground(Notes.ON_ID, n, t)
            true
        } catch (e: Exception) {
            Voice.stop()
            Store(this).on = false
            Notes.off(this, "Android didn't let Pocket Loomy run in the background. Open the app and try again.")
            false
        }
    }

    // ------------------------------------------------------------------ check-ins with the PC
    private fun ping() {
        val why = Pocket.ready(this)
        if (why != null) { main.post { Pocket.turnOff(this, why) }; return }
        val device = Pocket.device(this) ?: return
        val trust = Store(this).trust() ?: return
        val pc = Pocket.pcName(this)
        Pocket.link.ping(device, trust, Pocket.myName(this)) { r ->
            main.post {
                if (r.isSuccess) { misses = 0; Pocket.mark(true) }
                else {
                    misses++
                    Pocket.mark(false)
                    if (misses >= 3) { Pocket.turnOff(this, "Turned off: couldn't reach $pc for 15 minutes."); return@post }
                }
                sync()
            }
        }
    }

    // ------------------------------------------------------------------ net speed meter
    private val tick = object : Runnable {
        override fun run() {
            if (!ticking) return
            val rx = TrafficStats.getTotalRxBytes()
            val tx = TrafficStats.getTotalTxBytes()
            val now = SystemClock.elapsedRealtime()
            if (rx >= 0 && tx >= 0 && lastRx >= 0 && now > lastAt) {
                val dt = (now - lastAt) / 1000.0
                down = ((rx - lastRx).coerceAtLeast(0) / dt).toLong()
                up = ((tx - lastTx).coerceAtLeast(0) / dt).toLong()
                Notes.update(this@PocketService, down, up)
            }
            lastRx = rx; lastTx = tx; lastAt = now
            main.postDelayed(this, 1000)
        }
    }

    private fun meterOn() {
        if (screenReceiver == null) {
            screenReceiver = object : BroadcastReceiver() {
                override fun onReceive(c: Context, i: Intent) {
                    if (i.action == Intent.ACTION_SCREEN_OFF) stopTicking() else startTicking()
                }
            }
            registerReceiver(screenReceiver, IntentFilter().apply { addAction(Intent.ACTION_SCREEN_ON); addAction(Intent.ACTION_SCREEN_OFF) })
        }
        if (getSystemService(PowerManager::class.java)?.isInteractive != false) startTicking()
    }

    private fun meterOff() {
        stopTicking()
        screenReceiver?.let { try { unregisterReceiver(it) } catch (_: Exception) {} }
        screenReceiver = null
        down = -1; up = -1
    }

    // nobody sees the status bar with the screen off: no work at all then
    private fun startTicking() { if (!ticking) { ticking = true; lastRx = -1; main.post(tick) } }
    private fun stopTicking() { ticking = false; main.removeCallbacks(tick) }

    override fun onDestroy() {
        pings?.shutdownNow(); pings = null
        meterOff()
        Voice.onChange = {}
        Voice.stop()
        running = false
        Store(this).on = false
        Pocket.changed()
        Notes.syncOff(this)
        super.onDestroy()
    }

    companion object {
        @Volatile var running = false
        const val SYNC = "io.github.jimmyuns.pocketloomy.SYNC"
        const val LISTEN = "io.github.jimmyuns.pocketloomy.LISTEN"
        const val KEEP = "keep"
    }
}
