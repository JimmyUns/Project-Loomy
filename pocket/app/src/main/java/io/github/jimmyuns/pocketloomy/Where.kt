package io.github.jimmyuns.pocketloomy

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.location.Location
import android.location.LocationManager
import android.os.Build
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

// A rough location for the weather (2.24e). Only when he turned it on in Settings, only when Ellune asks for one
// (the weather), never in the background, rounded to ~1 km before it leaves the phone (Ellune.talkNow), and Ellune
// passes it to the weather service without storing it. Coarse permission only.
object Where {
    fun allowed(ctx: Context) = Store(ctx).useLocation &&
        ctx.checkSelfPermission(Manifest.permission.ACCESS_COARSE_LOCATION) == PackageManager.PERMISSION_GRANTED

    /** blocking (call it off the main thread); null = none */
    fun rough(ctx: Context): Pair<Double, Double>? {
        if (!allowed(ctx)) return null
        val lm = ctx.getSystemService(Context.LOCATION_SERVICE) as? LocationManager ?: return null
        try {
            val fresh = listOf(LocationManager.NETWORK_PROVIDER, LocationManager.PASSIVE_PROVIDER)
                .mapNotNull { runCatching { lm.getLastKnownLocation(it) }.getOrNull() }
                .filter { System.currentTimeMillis() - it.time < 3 * 3_600_000L }
                .maxByOrNull { it.time }
            if (fresh != null) return fresh.latitude to fresh.longitude
            if (Build.VERSION.SDK_INT >= 30 && lm.isProviderEnabled(LocationManager.NETWORK_PROVIDER)) {
                var got: Location? = null
                val done = CountDownLatch(1)
                val ex = Executors.newSingleThreadExecutor()
                lm.getCurrentLocation(LocationManager.NETWORK_PROVIDER, null, ex) { got = it; done.countDown() }
                done.await(6, TimeUnit.SECONDS)
                ex.shutdown()
                got?.let { return it.latitude to it.longitude }
            }
        } catch (_: SecurityException) {}
        return null
    }
}
