package io.github.jimmyuns.pocketloomy

import java.time.Instant
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import java.time.temporal.ChronoUnit
import java.util.Locale

// "Did League drop an update today?" (2.25, his ask): asked, never pushed. Words.gameQuestion spots the question,
// Ellune's /game says when the game last updated (Steam: its public build time; Riot: her logbook, checked every
// 15 min, so those times are "around"), and this turns it into the line shown big above the mic, in the phone's
// own time zone (EET / EEST for him, switching by itself).
object Games {
    enum class Kind { TODAY, WHEN, LAST }
    class Question(val kind: Kind, val game: String)

    /** Ellune's answer: when (ms, null = no change seen), the version, when the Riot logbook started (ms) */
    class Info(val name: String, val riot: Boolean, val time: Long?, val version: String, val since: Long?)

    private val DATE = DateTimeFormatter.ofPattern("EEE d/M/yyyy", Locale.ENGLISH)
    private val CLOCK = DateTimeFormatter.ofPattern("h:mm a", Locale.ENGLISH)

    fun date(ms: Long, zone: ZoneId): String = DATE.format(Instant.ofEpochMilli(ms).atZone(zone))
    fun clock(ms: Long, zone: ZoneId): String = CLOCK.format(Instant.ofEpochMilli(ms).atZone(zone))

    /** "today", "yesterday", "3 days ago" (calendar days in his zone) */
    fun ago(ms: Long, now: Long, zone: ZoneId): String {
        val d = ChronoUnit.DAYS.between(Instant.ofEpochMilli(ms).atZone(zone).toLocalDate(), Instant.ofEpochMilli(now).atZone(zone).toLocalDate())
        return when {
            d <= 0L -> "today"
            d == 1L -> "yesterday"
            else -> "$d days ago"
        }
    }

    /** (the big line, the small line under it) */
    fun answer(q: Kind, i: Info, now: Long, zone: ZoneId): Pair<String, String> {
        val about = if (i.riot) "around " else ""
        val ver = when {
            i.version.isEmpty() -> ""
            i.riot -> "Version ${i.version}"
            else -> "Build ${i.version}"
        }
        val t = i.time
        if (t == null) {
            val since = i.since
            val line = if (since != null) "No new patch since Ellune started watching (${date(since, zone)})." else "Ellune just started watching it: ask again later."
            return (if (q == Kind.TODAY) "Not that Ellune saw" else "Nothing yet") to listOf(i.name + ": " + line, ver).filter { it.isNotEmpty() }.joinToString("\n")
        }
        val whenFull = "${date(t, zone)}, $about${clock(t, zone)}"
        val small = listOf(i.name, ver).filter { it.isNotEmpty() }.joinToString(" \u00B7 ")
        return when (q) {
            Kind.TODAY -> if (ago(t, now, zone) == "today") "Yes, at $about${clock(t, zone)}" to small
                          else "No" to "$small\nLast update: ${ago(t, now, zone)} ($whenFull)"
            Kind.WHEN -> {
                val a = ago(t, now, zone)
                (if (a == "today" || a == "yesterday") "$a at $about${clock(t, zone)}".replaceFirstChar { it.uppercase() }
                 else a.replaceFirstChar { it.uppercase() }) to "$small\n$whenFull"
            }
            Kind.LAST -> "Last update: $whenFull" to small
        }
    }
}
