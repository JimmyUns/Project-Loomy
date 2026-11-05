package io.github.jimmyuns.pocketloomy

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.pm.PackageInfo
import android.content.pm.PackageInstaller
import android.content.pm.PackageManager
import android.os.Build
import java.io.File
import java.net.URL
import java.security.MessageDigest
import javax.net.ssl.HttpsURLConnection

// The update button (2.23, his ask): Settings -> Updates -> Check for updates.
//  1. asks GitHub for the latest release (Update.check decides if it's trusted: JimmyUns' release, the APK built by
//     GitHub Actions, GitHub's sha256 of it);
//  2. compares its versionCode with the installed app's;
//  3. downloads the APK into the app's cache, checking the size and the sha256 as it goes;
//  4. checks it's this app (package name, version, same signing key when Android lets us read it);
//  5. hands it to Android's installer (a PackageInstaller session). Android shows its own "Update this app?" and
//     refuses anything not signed with the installed app's key. The first time, Android asks him to allow Pocket
//     Loomy to install apps ("Install unknown apps"): once, in Android's settings.
// The dev app ("Pocket Loomy dev", a separate app) can't update itself from releases; instead it offers to install or
// update the release app, which is also the way to try the whole thing before a newer release exists.
object Updater {
    @Volatile var line = ""                 // what the Updates box says (MainActivity shows it)
    @Volatile var lineBad = false
    @Volatile var busy = false

    fun isDev(ctx: Context) = ctx.packageName.endsWith(".dev")
    /** the app the releases are for: this one, or the release app when this is the dev app */
    fun target(ctx: Context) = ctx.packageName.removeSuffix(".dev")

    /** versionName + versionCode of an installed app, or null if it isn't installed */
    fun installed(ctx: Context, pkg: String = ctx.packageName): Pair<String, Long>? = try {
        val p = ctx.packageManager.getPackageInfo(pkg, 0)
        (p.versionName ?: "?") to (if (Build.VERSION.SDK_INT >= 28) p.longVersionCode else @Suppress("DEPRECATION") p.versionCode.toLong())
    } catch (_: PackageManager.NameNotFoundException) { null }

    fun say(text: String, bad: Boolean = false) { line = text; lineBad = bad; Pocket.changed() }

    // ------------------------------------------------------------------ 1. ask GitHub (blocking)
    fun checkNow(): Update.Check {
        var c: HttpsURLConnection? = null
        try {
            c = URL(Update.API).openConnection() as HttpsURLConnection
            c.connectTimeout = 10000
            c.readTimeout = 10000
            c.useCaches = false
            c.setRequestProperty("Accept", "application/vnd.github+json")
            c.setRequestProperty("X-GitHub-Api-Version", "2022-11-28")
            c.setRequestProperty("User-Agent", "PocketLoomy")
            val code = c.responseCode
            if (code == 404) return Update.Check.Bad("There's no release yet.")
            if (code == 403 || code == 429) return Update.Check.Bad("GitHub is limiting checks from this network right now. Try again in an hour.")
            if (code != 200) return Update.Check.Bad("GitHub answered $code. Try again later.")
            val buf = ByteArray(512 * 1024)                       // the answer is ~6 KB
            var n = 0
            c.inputStream.use { s -> while (n < buf.size) { val r = s.read(buf, n, buf.size - n); if (r < 0) break; n += r } }
            if (n >= buf.size) return Update.Check.Bad("GitHub's answer was too big.")
            return Update.check(String(buf, 0, n, Charsets.UTF_8))
        } catch (_: java.net.SocketTimeoutException) {
            return Update.Check.Bad("GitHub was too slow to answer. Try again.")
        } catch (_: Exception) {
            return Update.Check.Bad("Couldn't reach GitHub. Is the phone online?")
        } finally {
            c?.disconnect()
        }
    }

    // ------------------------------------------------------------------ 3 + 4. download and check (blocking)
    private fun dir(ctx: Context) = File(ctx.cacheDir, "update")
    fun clean(ctx: Context) { dir(ctx).deleteRecursively() }

    /** the checked APK, or throws with what went wrong (for the Updates box) */
    fun downloadNow(ctx: Context, ok: Update.Check.Ok, progress: (Int) -> Unit): File {
        clean(ctx)
        val d = dir(ctx).apply { mkdirs() }
        val f = File(d, Update.APK)
        val sha = MessageDigest.getInstance("SHA-256")
        var c: HttpsURLConnection? = null
        var got = 0L
        try {
            c = URL(ok.url).openConnection() as HttpsURLConnection   // GitHub redirects to its file host (https)
            c.connectTimeout = 15000
            c.readTimeout = 20000
            c.useCaches = false
            c.setRequestProperty("Accept", "application/octet-stream")
            c.setRequestProperty("User-Agent", "PocketLoomy")
            if (c.responseCode != 200) throw Exception("The download failed (GitHub answered ${c.responseCode}).")
            if (c.url.protocol != "https") throw Exception("The download was sent somewhere insecure, so it was stopped.")
            val buf = ByteArray(64 * 1024)
            c.inputStream.use { s ->
                f.outputStream().use { out ->
                    var last = -1
                    while (true) {
                        val r = s.read(buf)
                        if (r < 0) break
                        got += r
                        if (got > ok.size) throw Exception("The download is bigger than GitHub said, so it was stopped.")
                        sha.update(buf, 0, r)
                        out.write(buf, 0, r)
                        val pct = (got * 100 / ok.size).toInt()
                        if (pct != last) { last = pct; progress(pct) }
                    }
                }
            }
        } catch (e: Exception) {
            f.delete()
            throw if (e.message?.startsWith("The download") == true) e
                  else if (e is java.net.SocketTimeoutException) Exception("The download stalled. Try again.")
                  else Exception("The download failed. Is the phone online?")
        } finally {
            c?.disconnect()
        }
        if (got != ok.size) { f.delete(); throw Exception("The download stopped early. Try again.") }
        if (Update.hex(sha.digest()) != ok.sha256) { f.delete(); throw Exception("The downloaded file doesn't match GitHub's checksum, so it was thrown away.") }

        // is it really this app, this version, signed like the installed one?
        val pm = ctx.packageManager
        val flags = if (Build.VERSION.SDK_INT >= 28) PackageManager.GET_SIGNING_CERTIFICATES else @Suppress("DEPRECATION") PackageManager.GET_SIGNATURES
        val apk = pm.getPackageArchiveInfo(f.path, flags)
        if (apk == null) { f.delete(); throw Exception("Android says the download isn't a valid app.") }
        if (apk.packageName != target(ctx)) { f.delete(); throw Exception("The download is a different app (${apk.packageName}), so it was thrown away.") }
        val apkCode = if (Build.VERSION.SDK_INT >= 28) apk.longVersionCode else @Suppress("DEPRECATION") apk.versionCode.toLong()
        if (apkCode != ok.code.toLong()) { f.delete(); throw Exception("The download says it's another version than ${ok.tag}, so it was thrown away.") }
        val mine = try { pm.getPackageInfo(target(ctx), flags) } catch (_: PackageManager.NameNotFoundException) { null }
        val a = signers(apk); val b = mine?.let { signers(it) }
        if (a != null && b != null && a != b) { f.delete(); throw Exception("The download isn't signed with the same key as the app on this phone, so it was thrown away.") }
        return f
    }

    // the signing certificates, as text so two sets compare (null when Android doesn't say)
    private fun signers(p: PackageInfo): Set<String>? {
        val certs = if (Build.VERSION.SDK_INT >= 28) p.signingInfo?.apkContentsSigners
                    else @Suppress("DEPRECATION") p.signatures
        if (certs.isNullOrEmpty()) return null
        return certs.map { it.toCharsString() }.toSet()
    }

    // ------------------------------------------------------------------ 5. hand it to Android
    /** null when Android has it (its confirm screen comes next), else why not */
    fun install(ctx: Context, f: File): String? = try {
        val pi = ctx.packageManager.packageInstaller
        val params = PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL)
        params.setAppPackageName(target(ctx))
        val id = pi.createSession(params)
        pi.openSession(id).use { s ->
            s.openWrite(Update.APK, 0, f.length()).use { out ->
                f.inputStream().use { it.copyTo(out) }
                s.fsync(out)
            }
            val flags = PendingIntent.FLAG_UPDATE_CURRENT or (if (Build.VERSION.SDK_INT >= 31) PendingIntent.FLAG_MUTABLE else 0)
            val done = PendingIntent.getBroadcast(ctx, id, Intent(ctx, UpdateDone::class.java).setPackage(ctx.packageName), flags)
            s.commit(done.intentSender)
        }
        null
    } catch (e: Exception) {
        "Android's installer didn't take it (${e.message ?: e.javaClass.simpleName})."
    }
}

/** Android's installer reports back here: its confirm screen to show, or how it went */
class UpdateDone : BroadcastReceiver() {
    override fun onReceive(ctx: Context, intent: Intent) {
        val status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE)
        val msg = intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE)
        when (status) {
            PackageInstaller.STATUS_PENDING_USER_ACTION -> {
                val confirm = if (Build.VERSION.SDK_INT >= 33) intent.getParcelableExtra(Intent.EXTRA_INTENT, Intent::class.java)
                              else @Suppress("DEPRECATION") intent.getParcelableExtra(Intent.EXTRA_INTENT)
                if (confirm == null) { Updater.busy = false; Updater.say("Android didn't show its install screen. Try again.", true); return }
                try {
                    ctx.startActivity(confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
                    Updater.say("Waiting for you to press Update...")
                } catch (_: Exception) {
                    Updater.busy = false
                    Updater.say("Android didn't let the install screen open. Keep Pocket Loomy open and try again.", true)
                }
            }
            PackageInstaller.STATUS_SUCCESS -> {                  // the dev app installing the release app gets here
                Updater.busy = false
                Updater.clean(ctx)
                Updater.say(if (Updater.isDev(ctx)) "Installed: the release app (Pocket Loomy) is up to date." else "Updated.")
            }
            else -> {
                Updater.busy = false
                Updater.clean(ctx)
                val why = when (status) {
                    PackageInstaller.STATUS_FAILURE_ABORTED -> "Cancelled."
                    PackageInstaller.STATUS_FAILURE_INCOMPATIBLE, PackageInstaller.STATUS_FAILURE_CONFLICT ->
                        "Android refused it: it isn't signed with the same key as the app on this phone, or it's older."
                    PackageInstaller.STATUS_FAILURE_STORAGE -> "Android refused it: not enough space."
                    PackageInstaller.STATUS_FAILURE_INVALID -> "Android says the file isn't a valid app."
                    PackageInstaller.STATUS_FAILURE_BLOCKED -> "This phone blocked the install."
                    else -> "The install failed."
                }
                Updater.say(if (msg.isNullOrBlank() || status == PackageInstaller.STATUS_FAILURE_ABORTED) why else "$why ($msg)", status != PackageInstaller.STATUS_FAILURE_ABORTED)
            }
        }
    }
}
