package io.github.jimmyuns.pocketloomy

import android.app.AlarmManager
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.graphics.drawable.Icon
import java.time.LocalDateTime
import java.time.ZoneId

// Her notes and reminders (2.24e, his ask: "remind me tomorrow to buy paper" -> a notification at 6 PM). All on
// the phone: they work with the PC off and without Ellune once set. A reminder is a notification with Done and
// "In 1 hour"; a note just sits in the list ("what are my notes"). Android may deliver a reminder a few minutes
// late (no exact-alarm permission asked); after a reboot Boot puts them back.
object Reminders {
    class Item(val id: Int, val text: String, val at: Long)       // at = 0: a note

    fun all(ctx: Context): List<Item> = Store(ctx).notesRaw.lines().mapNotNull { l ->
        val id = Regex("\"id\":(\\d+)").find(l)?.groupValues?.get(1)?.toIntOrNull() ?: return@mapNotNull null
        val at = Regex("\"at\":(\\d+)").find(l)?.groupValues?.get(1)?.toLongOrNull() ?: 0L
        val t = Ellune.jsonString(l, "t") ?: return@mapNotNull null
        Item(id, t, at)
    }

    private fun save(ctx: Context, items: List<Item>) {
        Store(ctx).notesRaw = items.takeLast(100).joinToString("\n") { "{\"id\":${it.id},\"at\":${it.at},\"t\":${Ellune.jsonQuote(it.text)}}" }
    }

    fun note(ctx: Context, text: String): Item {
        val items = all(ctx)
        val it = Item((items.maxOfOrNull { it.id } ?: 0) + 1, text, 0)
        save(ctx, items + it)
        return it
    }

    /** "2026-11-10T18:00" in the phone's zone -> a reminder; null if the time is odd or already past */
    fun remind(ctx: Context, text: String, local: String): Item? {
        val at = try { LocalDateTime.parse(local).atZone(ZoneId.systemDefault()).toInstant().toEpochMilli() } catch (_: Exception) { return null }
        if (at < System.currentTimeMillis() - 60_000) return null
        val items = all(ctx)
        val it = Item((items.maxOfOrNull { it.id } ?: 0) + 1, text, at)
        save(ctx, items + it)
        schedule(ctx, it)
        return it
    }

    fun done(ctx: Context, id: Int) {
        save(ctx, all(ctx).filter { it.id != id })
        (ctx.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager).cancel(1000 + id)
    }

    fun later(ctx: Context, id: Int) {
        val items = all(ctx)
        val old = items.firstOrNull { it.id == id } ?: return
        val again = Item(id, old.text, System.currentTimeMillis() + 3_600_000)
        save(ctx, items.map { if (it.id == id) again else it })
        schedule(ctx, again)
        (ctx.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager).cancel(1000 + id)
    }

    /** after a reboot: every reminder still ahead goes back on the clock; ones missed while off ring now */
    fun rescheduleAll(ctx: Context) { for (it in all(ctx)) if (it.at > 0) schedule(ctx, it) }

    private fun fire(ctx: Context, id: Int) = PendingIntent.getBroadcast(ctx, 2000 + id,
        Intent(ctx, Actions::class.java).setAction(Actions.REMIND).putExtra("id", id),
        PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)

    private fun schedule(ctx: Context, it: Item) {
        val am = ctx.getSystemService(Context.ALARM_SERVICE) as AlarmManager
        val at = maxOf(it.at, System.currentTimeMillis() + 5_000)
        am.setWindow(AlarmManager.RTC_WAKEUP, at, 5 * 60_000L, fire(ctx, it.id))     // within 5 minutes of the time
    }

    /** the reminder's moment: its notification */
    fun ring(ctx: Context, id: Int) {
        val it = all(ctx).firstOrNull { it.id == id && it.at > 0 } ?: return
        val nm = ctx.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        nm.createNotificationChannel(NotificationChannel("reminders", "Reminders", NotificationManager.IMPORTANCE_HIGH).apply {
            description = "Things you asked Loomy to remind you of"
        })
        fun act(label: String, action: String, code: Int) = Notification.Action.Builder(
            Icon.createWithResource(ctx, R.drawable.ic_stat), label,
            PendingIntent.getBroadcast(ctx, code, Intent(ctx, Actions::class.java).setAction(action).putExtra("id", id),
                PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)).build()
        val n = Notification.Builder(ctx, "reminders")
            .setSmallIcon(R.drawable.ic_stat)
            .setContentTitle("Loomy: did you do this?")
            .setContentText(it.text)
            .setStyle(Notification.BigTextStyle().bigText(it.text))
            .setAutoCancel(false)
            .addAction(act("Done", Actions.REMIND_DONE, 3000 + id))
            .addAction(act("In 1 hour", Actions.REMIND_LATER, 4000 + id))
            .build()
        nm.notify(1000 + id, n)
    }

    /** "what are my notes": a one-line summary */
    fun summary(ctx: Context): String {
        val items = all(ctx)
        if (items.isEmpty()) return "Nothing in your notes."
        val z = ZoneId.systemDefault()
        return items.takeLast(5).joinToString("; ") { i ->
            if (i.at > 0) "${Games.date(i.at, z)} ${Games.clock(i.at, z)}: ${i.text}" else i.text
        } + if (items.size > 5) " (+${items.size - 5} more)" else ""
    }
}
