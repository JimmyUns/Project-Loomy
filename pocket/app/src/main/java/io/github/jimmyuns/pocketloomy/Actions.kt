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
            REPOST -> Notes.syncOff(ctx)
        }
    }

    companion object {
        const val ON = "io.github.jimmyuns.pocketloomy.ON"
        const val OFF = "io.github.jimmyuns.pocketloomy.OFF"
        const val STOP_LISTEN = "io.github.jimmyuns.pocketloomy.STOP_LISTEN"
        const val REPOST = "io.github.jimmyuns.pocketloomy.REPOST"
    }
}
