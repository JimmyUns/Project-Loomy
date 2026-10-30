package io.github.jimmyuns.pocketloomy

import android.Manifest
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.drawable.Icon
import android.os.Build

// The notifications: "off" (with Turn on; it comes back if swiped away) and "on" (the service's,
// with Talk and Turn off). Quiet channel: no sound, no badge.
object Notes {
    const val ON_ID = 1
    private const val OFF_ID = 2
    private const val CHANNEL = "pocket"
    private val ACCENT = Color.rgb(0xA8, 0x55, 0xF7)

    fun allowed(ctx: Context) = Build.VERSION.SDK_INT < 33 ||
        ctx.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED

    private fun nm(ctx: Context): NotificationManager = ctx.getSystemService(NotificationManager::class.java)!!

    private fun channel(ctx: Context) {
        nm(ctx).createNotificationChannel(NotificationChannel(CHANNEL, "Pocket Loomy", NotificationManager.IMPORTANCE_LOW).apply {
            description = "The on/off switch and the Talk button"
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

    /** the service's notification while it's on */
    fun on(ctx: Context, text: String): Notification {
        channel(ctx)
        return Notification.Builder(ctx, CHANNEL)
            .setSmallIcon(R.drawable.ic_stat)
            .setColor(ACCENT)
            .setContentTitle("Pocket Loomy is on")
            .setContentText(text)
            .setContentIntent(openApp(ctx))
            .setOngoing(true)
            .setShowWhen(false)
            .addAction(action(ctx, "Talk", talk(ctx)))
            .addAction(action(ctx, "Turn off", broadcast(ctx, Actions.OFF, 1)))
            .build()
    }

    fun updateOn(ctx: Context, text: String) {
        if (allowed(ctx) && PocketService.running) nm(ctx).notify(ON_ID, on(ctx, text))
    }

    fun off(ctx: Context, why: String? = null) {
        if (!allowed(ctx)) return
        channel(ctx)
        val n = Notification.Builder(ctx, CHANNEL)
            .setSmallIcon(R.drawable.ic_stat)
            .setColor(ACCENT)
            .setContentTitle("Pocket Loomy is off")
            .setContentText(why ?: "Turn it on to talk to Loomy from here.")
            .setContentIntent(openApp(ctx))
            .setDeleteIntent(broadcast(ctx, Actions.REPOST, 3))    // swiped away: it comes back
            .setShowWhen(false)
            .addAction(action(ctx, "Turn on", broadcast(ctx, Actions.ON, 2)))
            .build()
        nm(ctx).notify(OFF_ID, n)
    }

    fun clearOff(ctx: Context) { nm(ctx).cancel(OFF_ID) }
}
