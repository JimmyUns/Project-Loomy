package io.github.jimmyuns.pocketloomy

import android.Manifest
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Typeface
import android.graphics.drawable.Icon
import android.os.Build
import android.os.SystemClock

// One notification at a time, never a pile:
//   - while the service runs (Pocket Loomy on, listening, or the speed meter): THE notification, id 1.
//     With the speed meter on, its status-bar icon IS the speed and the title is "down / up", no name.
//   - when nothing runs: an "off" notification with Turn on (id 2), only if Notification is on in settings.
// Channels: "pocket" (quiet, shows in the status bar) and "pocket_min" (collapsed at the bottom, no icon)
// for when he turned the notification off but Android still needs one for the running service.
object Notes {
    const val ON_ID = 1
    private const val OFF_ID = 2
    private const val CHANNEL = "pocket"
    private const val CHANNEL_MIN = "pocket_min"
    private val ACCENT = Color.rgb(0xA8, 0x55, 0xF7)

    fun allowed(ctx: Context) = Build.VERSION.SDK_INT < 33 ||
        ctx.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED

    private fun nm(ctx: Context): NotificationManager = ctx.getSystemService(NotificationManager::class.java)!!

    private fun channels(ctx: Context) {
        nm(ctx).createNotificationChannel(NotificationChannel(CHANNEL, "Pocket Loomy", NotificationManager.IMPORTANCE_LOW).apply {
            description = "On/off, Talk, and the net speed meter"
            setShowBadge(false)
        })
        nm(ctx).createNotificationChannel(NotificationChannel(CHANNEL_MIN, "Pocket Loomy (hidden)", NotificationManager.IMPORTANCE_MIN).apply {
            description = "Used when the notification is turned off in the app (Android still needs one while it runs)"
            setShowBadge(false)
        })
    }

    private fun broadcast(ctx: Context, action: String, code: Int) = PendingIntent.getBroadcast(ctx, code,
        Intent(ctx, Actions::class.java).setAction(action), PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)

    private fun openApp(ctx: Context) = PendingIntent.getActivity(ctx, 10,
        Intent(ctx, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_SINGLE_TOP),
        PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)

    private fun talk(ctx: Context) = PendingIntent.getActivity(ctx, 11,
        Intent(ctx, TalkActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
        PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)

    private fun action(ctx: Context, label: String, pi: PendingIntent) =
        Notification.Action.Builder(Icon.createWithResource(ctx, R.drawable.ic_stat), label, pi).build()

    /** bytes per second -> ("1.2", "MB/s") */
    fun speedParts(bps: Long): Pair<String, String> {
        val (v, unit) = when {
            bps < 1024 -> bps.toDouble() to "B/s"
            bps < 1024L * 1024 -> bps / 1024.0 to "KB/s"
            else -> bps / (1024.0 * 1024.0) to "MB/s"
        }
        val num = if (v < 10 && unit != "B/s") String.format("%.1f", v) else minOf(v.toLong(), 999).toString()
        return num to unit
    }

    fun speedText(bps: Long) = speedParts(bps).let { "${it.first} ${it.second}" }

    // the status-bar icon: the number on top, the unit under it, like the speed meter apps
    private fun speedIcon(ctx: Context, bps: Long): Icon {
        val size = (24 * ctx.resources.displayMetrics.density).toInt().coerceAtLeast(24)
        val bmp = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888)
        val c = Canvas(bmp)
        val (num, unit) = speedParts(bps)
        val p = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.WHITE; textAlign = Paint.Align.CENTER
            typeface = Typeface.create("sans-serif-condensed", Typeface.BOLD) }
        p.textSize = size * 0.62f
        while (p.measureText(num) > size && p.textSize > 6) p.textSize -= 1
        c.drawText(num, size / 2f, size * 0.58f, p)
        p.typeface = Typeface.create("sans-serif-condensed", Typeface.NORMAL)
        p.textSize = size * 0.38f
        while (p.measureText(unit) > size && p.textSize > 6) p.textSize -= 1
        c.drawText(unit, size / 2f, size * 0.97f, p)
        return Icon.createWithBitmap(bmp)
    }

    /** the service's notification, from the current state. down/up = bytes per second, or -1 without the meter. */
    fun service(ctx: Context, down: Long = -1, up: Long = -1): Notification {
        channels(ctx)
        val s = Store(ctx)
        val meter = down >= 0
        val listening = Voice.state != Voice.State.OFF
        val chan = if (meter || s.showNotif || listening) CHANNEL else CHANNEL_MIN
        val b = Notification.Builder(ctx, chan)
            .setColor(ACCENT)
            .setContentIntent(openApp(ctx))
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setShowWhen(false)
        if (meter) b.setSmallIcon(speedIcon(ctx, down + up)) else b.setSmallIcon(R.drawable.ic_stat)

        val last = Voice.last()
        val recent = last != null && SystemClock.elapsedRealtime() - Voice.lastAt < 60_000
        val title = when {
            meter -> "\u2193 ${speedText(down)}   \u2191 ${speedText(up)}"
            listening && Voice.keep -> "Listening for \u201Cayo Loomy\u201D"
            listening -> "Listening..."
            s.on -> "Pocket Loomy is on"
            else -> "Pocket Loomy"
        }
        val text = when {
            Voice.sending && last != null -> "\u203A ${last.cmd}"
            listening && Voice.partial.isNotEmpty() -> Voice.partial
            recent && last != null -> "\u203A ${last.cmd}: ${last.answer.lineSequence().first()}"
            meter && listening && Voice.keep -> "Listening for \u201Cayo Loomy\u201D"
            meter && !s.on -> null
            s.on -> Pocket.status(ctx)
            else -> null
        }
        b.setContentTitle(title)
        if (text != null) b.setContentText(text)
        if (recent && last != null) b.setStyle(Notification.BigTextStyle().bigText("\u203A ${last.cmd}\n${last.answer}"))

        if (listening) b.addAction(action(ctx, "Stop listening", broadcast(ctx, Actions.STOP_LISTEN, 4)))
        else if (s.on) b.addAction(action(ctx, "Talk", talk(ctx)))
        if (s.on) b.addAction(action(ctx, "Turn off", broadcast(ctx, Actions.OFF, 1)))
        else if (s.showNotif) b.addAction(action(ctx, "Turn Loomy on", broadcast(ctx, Actions.ON, 2)))
        return b.build()
    }

    fun update(ctx: Context, down: Long = -1, up: Long = -1) {
        if (allowed(ctx) && PocketService.running) nm(ctx).notify(ON_ID, service(ctx, down, up))
    }

    /** the "off" notification: only when nothing runs and he wants the notification */
    fun off(ctx: Context, why: String? = null) {
        if (!allowed(ctx) || PocketService.running && Store(ctx).netSpeed) return
        if (!Store(ctx).showNotif && why == null) { clearOff(ctx); return }
        channels(ctx)
        val n = Notification.Builder(ctx, CHANNEL)
            .setSmallIcon(R.drawable.ic_stat)
            .setColor(ACCENT)
            .setContentTitle("Pocket Loomy is off")
            .setContentText(why ?: "Turn it on to talk to Loomy from here.")
            .setContentIntent(openApp(ctx))
            .setDeleteIntent(broadcast(ctx, Actions.REPOST, 3))    // swiped away: it comes back (if he wants it)
            .setShowWhen(false)
            .addAction(action(ctx, "Turn on", broadcast(ctx, Actions.ON, 2)))
            .build()
        nm(ctx).notify(OFF_ID, n)
    }

    /** show or clear the "off" notification to match the switches */
    fun syncOff(ctx: Context) {
        if (Pocket.wantsService(ctx)) clearOff(ctx) else off(ctx)
    }

    fun clearOff(ctx: Context) { nm(ctx).cancel(OFF_ID) }
}
