package io.github.jimmyuns.pocketloomy

import android.app.Service
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import java.text.DateFormat
import java.util.Date
import java.util.concurrent.Executors
import java.util.concurrent.ScheduledExecutorService
import java.util.concurrent.TimeUnit

// Runs while Pocket Loomy is on: keeps the "on" notification (Talk, Turn off) and checks in with the
// PC every 5 minutes, so /phone on the PC shows when the phone was last around. If the PC can't be
// reached 3 times in a row (15 min) it turns itself off. No microphone here: Talk opens a screen.
class PocketService : Service() {
    private var sched: ScheduledExecutorService? = null
    private var misses = 0

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val n = Notes.on(this, "Checking on ${Pocket.pcName(this)}...")
        try {
            if (Build.VERSION.SDK_INT >= 29) startForeground(Notes.ON_ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE)
            else startForeground(Notes.ON_ID, n)
        } catch (e: Exception) {
            Store(this).on = false
            Notes.off(this, "Android didn't let Pocket Loomy stay on. Open the app and try again.")
            stopSelf()
            return START_NOT_STICKY
        }
        running = true
        Store(this).on = true
        Pocket.changed()
        if (sched == null) {
            sched = Executors.newSingleThreadScheduledExecutor().also {
                it.scheduleWithFixedDelay({ ping() }, 0, 5, TimeUnit.MINUTES)
            }
        }
        return START_NOT_STICKY
    }

    private fun ping() {
        val why = Pocket.ready(this)
        if (why != null) { Pocket.turnOff(this, why); return }
        val device = Pocket.device(this) ?: return
        val trust = Store(this).trust() ?: return
        val pc = Pocket.pcName(this)
        Pocket.link.ping(device, trust, Pocket.myName(this)) { r ->
            if (r.isSuccess) {
                misses = 0
                Pocket.mark(true)
                Notes.updateOn(this, "$pc, checked in at " + DateFormat.getTimeInstance(DateFormat.SHORT).format(Date()))
            } else {
                misses++
                Pocket.mark(false)
                if (misses >= 3) Pocket.turnOff(this, "Turned off: couldn't reach $pc for 15 minutes.")
                else Notes.updateOn(this, "Can't reach $pc right now. Trying again in 5 minutes.")
            }
        }
    }

    override fun onDestroy() {
        sched?.shutdownNow()
        sched = null
        running = false
        Store(this).on = false
        Pocket.changed()
        super.onDestroy()
    }

    companion object { @Volatile var running = false }
}
