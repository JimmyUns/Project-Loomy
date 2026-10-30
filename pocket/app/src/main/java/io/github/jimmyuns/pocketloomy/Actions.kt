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
            REPOST -> if (!Store(ctx).on) Notes.off(ctx)
        }
    }

    companion object {
        const val ON = "io.github.jimmyuns.pocketloomy.ON"
        const val OFF = "io.github.jimmyuns.pocketloomy.OFF"
        const val REPOST = "io.github.jimmyuns.pocketloomy.REPOST"
    }
}
