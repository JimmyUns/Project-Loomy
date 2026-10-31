package io.github.jimmyuns.pocketloomy

// Turning what the speech recognizer heard into a pairing code or a Loomy command, the way people
// actually talk: "Loomy, can you turn off the screen please" -> "display off". The phrases themselves
// (what means what) live in Phrases.kt; this file does the cleanup and the rules around them.
object Words {
    private val DIGIT = mapOf(
        "zero" to '0', "oh" to '0', "o" to '0', "one" to '1', "won" to '1', "two" to '2', "to" to '2', "too" to '2',
        "three" to '3', "tree" to '3', "four" to '4', "for" to '4', "fore" to '4', "five" to '5', "six" to '6',
        "sicks" to '6', "seven" to '7', "eight" to '8', "ate" to '8', "nine" to '9', "nein" to '9',
    )

    /** "4821 0937", "four eight two one, zero nine three seven", "48-21-09-37" -> "48210937"; anything else -> null */
    fun code(heard: String): String? {
        val out = StringBuilder()
        for (word in heard.lowercase().split(Regex("[^a-z0-9]+"))) {
            if (word.isEmpty()) continue
            if (word.all { it in '0'..'9' }) out.append(word)
            else out.append(DIGIT[word] ?: return null)
        }
        return if (out.length == 8) out.toString() else null
    }

    // how recognizers write "Loomy", and the greetings / fillers that can come before it
    private val NAME = setOf("loomy", "loomie", "lumi", "lumie", "looney", "loony", "lumy", "loomi", "lumee", "loomey", "lumey",
                             "luumi", "lumia", "loomis", "lumis", "lomi", "lume", "lumii", "loomiy", "lumee's", "lumi's", "loomy's")
    private val HAIL = setOf("yo", "ayo", "eyo", "ayoo", "ayyo", "hey", "ay", "aye", "a", "oi", "hayo", "yoo")
    private val FILL = setOf("okay", "ok", "um", "uh", "so", "and", "oh", "alright", "right", "hey", "yo")

    // words, keeping things like "youtube.com" or "c:/games" whole but dropping sentence punctuation
    private fun words(s: String) = s.lowercase().replace('\u2019', '\'').split(Regex("[^a-z0-9/'._:-]+"))
        .map { it.trim('.', ':', '-', '_', '\'') }.filter { it.isNotEmpty() }

    private fun isName(x: String) = x in NAME || (x.startsWith("yo") && x.drop(2) in NAME) || (x.startsWith("ayo") && x.drop(3) in NAME)

    /**
     * Was this said TO Loomy? Her name has to open what was said ("Loomy show screen", "okay Lumi, system
     * info", "ayo Loomy ..."), or follow a greeting anywhere ("so anyway, yo Loomy ..."). A name in the
     * middle of a sentence ("I told Loomy about it") doesn't count. Returns what came after the name
     * ("" = just the name: a follow-up is coming), or null when it wasn't for Loomy (then it's ignored
     * and never sent anywhere).
     */
    fun addressed(heard: String): String? {
        val w = words(heard)
        for (i in w.indices) {
            if (!isName(w[i])) continue
            val opens = w.subList(0, i).all { it in HAIL || it in FILL }
            val greeted = i > 0 && w[i - 1] in HAIL
            if (opens || greeted) return w.drop(i + 1).joinToString(" ")
        }
        return null
    }

    /** "... okay Loomy": the name came last, the command is probably in the next breath */
    fun endsWithName(heard: String) = words(heard).lastOrNull()?.let { isName(it) } == true

    /** words the recognizer should expect (Android 13+ uses this as a hint) */
    val HINTS = arrayListOf("Loomy", "Lumi", "display", "extend", "screen on", "screen off", "system info", "recycle bin",
        "input mode", "solo mode", "next tab", "previous tab", "back", "confirm", "cancel", "okay", "down", "up",
        "hide", "show", "web", "help", "stop listening", "thank you", "turn off the mic", "slash")

    // the phone-side switches, by voice (each one is a set in Phrases.kt)
    fun isInputModeOn(cmd: String) = cmd in Phrases.INPUT_ON
    fun isInputModeOff(cmd: String) = cmd in Phrases.INPUT_OFF
    fun isSoloOn(cmd: String) = cmd in Phrases.SOLO_ON
    fun isSoloOff(cmd: String) = cmd in Phrases.SOLO_OFF
    fun isMute(cmd: String) = cmd in Phrases.MUTE
    fun isSleep(cmd: String) = cmd in Phrases.SLEEP
    fun isDisconnect(cmd: String) = cmd in Phrases.DISCONNECT

    private val KEYPHRASES = Phrases.KEYS.keys.map { it.split(' ') }.sortedByDescending { it.size }
    private val KEYFILL = setOf("press", "hit", "go", "move", "and", "then", "now", "the", "please", "times", "time", "it", "a")
    private val NUM = mapOf("one" to "1", "two" to "2", "three" to "3", "four" to "4", "five" to "5",
                            "six" to "6", "seven" to "7", "eight" to "8", "nine" to "9")
    private val TWICE = mapOf("twice" to 2, "thrice" to 3)

    /** drop greetings/fillers in front of her name, and the name itself (front or back) */
    private fun unname(words: List<String>): List<String> {
        var w = words
        while (w.size > 1 && (w[0] in HAIL || w[0] in FILL) && w.drop(1).any { isName(it) }) w = w.drop(1)
        if (w.isNotEmpty() && isName(w[0])) w = w.drop(1)
        if (w.size > 1 && isName(w.last())) w = w.dropLast(1)          // "thank you Loomy"
        return w
    }

    /**
     * Keys, one or several in one breath: "next tab" -> "input next tab"; "go down three" -> "input down 3";
     * "up up right confirm" -> "input up 2 and input right and input confirm" (Loomy runs them in order).
     * Every word has to be a key, a count or filler, otherwise it's not keys at all -> null.
     */
    fun key(heard: String): String? {
        val w = unname(words(heard))
        val keys = mutableListOf<Pair<String, Int>>()
        var i = 0
        while (i < w.size) {
            if (w[i] == "input") { i++; continue }                         // "input down" said in input mode
            val p = KEYPHRASES.firstOrNull { ph -> i + ph.size <= w.size && w.subList(i, i + ph.size) == ph }
            if (p != null) { keys += Phrases.KEYS.getValue(p.joinToString(" ")) to 1; i += p.size; continue }
            val n = NUM[w[i]]?.toInt() ?: TWICE[w[i]] ?: w[i].takeIf { it.length == 1 && it[0] in '1'..'9' }?.toInt()
            if (n != null && keys.isNotEmpty() && keys.last().second == 1) { keys[keys.size - 1] = keys.last().first to n; i++; continue }
            if (w[i] in KEYFILL) { i++; continue }
            return null
        }
        if (keys.isEmpty()) return null
        val merged = mutableListOf<Pair<String, Int>>()                 // "up up up" -> up 3
        for (k in keys) if (merged.isNotEmpty() && merged.last().first == k.first) merged[merged.size - 1] = k.first to (merged.last().second + k.second).coerceAtMost(9) else merged += k
        return merged.take(12).joinToString(" and ") { (k, n) -> "input $k" + if (n > 1) " $n" else "" }
    }

    private val SLASH = setOf("slash", "slesh", "slush", "flash")
    private val LEAD = listOf("please", "can you", "could you", "would you", "will you", "can u", "just", "go ahead and",
                              "i want to", "i wanna", "i want you to", "let's", "lets", "now", "okay", "ok", "so", "hey")
        .map { it.split(' ') }
    private val TAIL = listOf("please", "for me", "now", "thanks", "thank you", "real quick", "rn").map { it.split(' ') }
    private val OPEN = setOf("open", "launch", "start", "show", "bring", "pull", "go")   // "open system info", "bring up si"

    private val SCREEN = setOf("screen", "screens", "monitor", "monitors", "displays")
    private val SCREEN_FILL = setOf("turn", "switch", "shut", "put", "the", "my", "both", "all", "to", "up", "down", "off", "on",
                                    "sleep", "wake", "show", "back", "screen", "screens", "monitor", "monitors", "displays", "display")

    private fun startsWith(w: List<String>, p: List<String>) = w.size >= p.size && w.subList(0, p.size) == p
    private fun endsWith(w: List<String>, p: List<String>) = w.size >= p.size && w.subList(w.size - p.size, w.size) == p

    /** one command, said naturally -> what Loomy understands */
    private fun one(phrase: List<String>): String {
        var w = phrase
        // fillers around it: "can you ... please"
        var changed = true
        while (changed && w.isNotEmpty()) {
            changed = false
            for (p in LEAD) if (startsWith(w, p) && w.size > p.size) { w = w.drop(p.size); changed = true }
            for (p in TAIL) if (endsWith(w, p) && w.size > p.size) { w = w.dropLast(p.size); changed = true }
        }
        if (w.isEmpty()) return ""
        val said = w.joinToString(" ")
        // the library first: "go back one tab", "mirror the screens", "how's my pc doing"...
        Phrases.COMMANDS[said]?.let { return it }
        Phrases.KEYS[said]?.let { return "input $it" }
        // the screens, however it's put: "turn off the screen", "screen off", "show screen", "turn the display off"
        val screenish = w.any { it in SCREEN } || (w[0] != "display" && "display" in w)
        if (screenish && w.all { it in SCREEN_FILL }) {
            val off = w.any { it == "off" || it == "sleep" || it == "shut" || it == "down" }
            val on = w.any { it == "on" || it == "wake" || it == "show" || it == "back" } || (w.contains("up") && !off)
            if (off && !on) return "display off"
            if (on && !off) return "display on"
        }
        if (w[0] == "press" && w.size > 1) return "input " + w.drop(1).joinToString(" ")
        // "open system info", "bring up the recycle bin", "open loomy", "open gmail" (a saved site)
        if (w[0] in OPEN && w.size > 1) {
            var r = w.drop(1)
            if (r.firstOrNull() == "up" || r.firstOrNull() == "me" || r.firstOrNull() == "to") r = r.drop(1)
            if (r.firstOrNull() == "the" || r.firstOrNull() == "my") r = r.drop(1)
            val rs = r.joinToString(" ")
            if (r.isNotEmpty()) {
                if (r.size == 1 && isName(r[0])) return "open"
                Phrases.COMMANDS[rs]?.let { return it }
                if (r.size == 1 && r[0] == "it") return "input enter"
                if (Phrases.STARTS.any { (s, _) -> startsWith(r, s) } || r[0] in Phrases.KNOWN) w = r
                else if (r.size == 1 && w[0] in setOf("open", "launch")) return "web ${r[0]}"
            }
        }
        // a slash: "slash packages" -> "/pkg"
        var slash = false
        if (w[0] in SLASH) { slash = true; w = w.drop(1) }
        else if (w[0].startsWith("/") && w[0].length > 1) { slash = true; w = listOf(w[0].substring(1)) + w.drop(1) }
        if (w.isEmpty()) return ""
        for ((s, cmd) in Phrases.STARTS) if (startsWith(w, s)) { w = listOf(cmd) + w.drop(s.size); break }
        // "gpu one" -> "gpu 1"
        val ww = w
        w = ww.mapIndexed { i, x -> if (i > 0 && ww[i - 1] == "gpu") NUM[x] ?: x else x }
        val text = w.joinToString(" ")
        return if (slash) "/$text" else text
    }

    /**
     * Everything said -> what's sent to Loomy. "Loomy, display extend." -> "display extend";
     * "Loomy open system info and go to the next tab" -> "si and input next tab". Each part between
     * "and"s is cleaned on its own; Loomy on the PC decides where "and" really splits.
     */
    fun command(heard: String): String {
        var w = words(heard)
        // "so low mode" is how "solo mode" often comes out
        w = w.joinToString(" ").replace("so low mode", "solo mode").replace("so lo mode", "solo mode").split(' ').filter { it.isNotEmpty() }
        w = unname(w)
        if (w.size > 1 && w[0] == "run") w = w.drop(1)
        val parts = mutableListOf<MutableList<String>>(mutableListOf())
        for (x in w) if (x == "and" || x == "then") parts += mutableListOf<String>() else parts.last() += x
        return parts.map { one(it) }.filter { it.isNotEmpty() }.joinToString(" and ")
    }

    /** the switches and controls are matched on the cleaned phrase, before any "and" splitting */
    fun plain(heard: String): String {
        var w = words(heard)
        w = w.joinToString(" ").replace("so low mode", "solo mode").replace("so lo mode", "solo mode").split(' ').filter { it.isNotEmpty() }
        w = unname(w)
        while (w.size > 1 && w[0] in setOf("okay", "ok", "so", "alright", "please", "can", "you", "could", "just")) w = w.drop(1)
        while (w.size > 1 && w.last() in setOf("please", "now")) w = w.dropLast(1)
        return w.joinToString(" ")
    }
}
