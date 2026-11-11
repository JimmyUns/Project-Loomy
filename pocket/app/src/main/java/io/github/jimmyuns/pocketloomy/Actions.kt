package io.github.jimmyuns.pocketloomy

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent

// Buttons in the notifications
class Actions : BroadcastReceiver() {
    override fun onReceive(ctx: Context, intent: Intent) {
        when (intent.action) {
            ON -> Pocket.turnOn(ctx)
            OFF -> Pocket.turnOff(ctx)
            STOP_LISTEN -> Pocket.stopTalking(ctx)
            REMIND -> Reminders.ring(ctx, intent.getIntExtra("id", -1))
            REMIND_DONE -> Reminders.done(ctx, intent.getIntExtra("id", -1))
            REMIND_LATER -> Reminders.later(ctx, intent.getIntExtra("id", -1))
            TYPE -> {                                           // 2.24e: typed in the notification
                val t = android.app.RemoteInput.getResultsFromIntent(intent)?.getCharSequence(TYPED)?.toString()
                if (!t.isNullOrBlank()) Voice.typed(ctx, t)
                Pocket.changed()                                // re-posts the notification (ends Android's spinner)
            }
            REPOST -> Notes.syncOff(ctx)
        }
    }

    companion object {
        const val ON = "io.github.jimmyuns.pocketloomy.ON"
        const val OFF = "io.github.jimmyuns.pocketloomy.OFF"
        const val STOP_LISTEN = "io.github.jimmyuns.pocketloomy.STOP_LISTEN"
        const val REPOST = "io.github.jimmyuns.pocketloomy.REPOST"
        const val TYPE = "io.github.jimmyuns.pocketloomy.TYPE"
        const val TYPED = "typed"
        const val REMIND = "io.github.jimmyuns.pocketloomy.REMIND"
        const val REMIND_DONE = "io.github.jimmyuns.pocketloomy.REMIND_DONE"
        const val REMIND_LATER = "io.github.jimmyuns.pocketloomy.REMIND_LATER"
    }
}
