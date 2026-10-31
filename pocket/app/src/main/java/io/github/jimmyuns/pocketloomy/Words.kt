package io.github.jimmyuns.pocketloomy

// Turning what the speech recognizer heard into a pairing code or a Loomy command, the way people
// actually talk: "ayo Loomy, can you turn off the screen please" -> "display off".
object Words {
    private val DIGIT = mapOf(
        "zero" to '0', "oh" to '0', "o" to '0',
        "one" to '1', "won" to '1',
        "two" to '2', "to" to '2', "too" to '2',
        "three" to '3', "tree" to '3',
        "four" to '4', "for" to '4', "fore" to '4',
        "five" to '5',
        "six" to '6', "sicks" to '6',
        "seven" to '7',
        "eight" to '8', "ate" to '8',
        "nine" to '9', "nein" to '9',
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

    // how recognizers write "Loomy", and the greetings that make it clear you're talking to her
    private val NAME = setOf("loomy", "loomie", "lumi", "lumie", "looney", "loony", "lumy", "loomi", "lumee", "loomey", "lumey")
    private val HAIL = setOf("yo", "ayo", "eyo", "ayoo", "ayyo", "hey", "ay", "aye", "a", "oi", "hayo", "yoo")

    // words, keeping things like "youtube.com" or "c:/games" whole but dropping sentence punctuation
    private fun words(s: String) = s.lowercase().replace('\u2019', '\'').split(Regex("[^a-z0-9/'._:-]+"))
        .map { it.trim('.', ':', '-', '_', '\'') }.filter { it.isNotEmpty() }

    /**
     * Keep-listening mode: was this said TO Loomy? Only "yo/ayo/hey Loomy ..." counts, anywhere in what was
     * heard ("so anyway, ayo Loomy show screen"). Returns what came after it ("" = just the greeting),
     * or null when it wasn't for Loomy (then it's ignored and never sent anywhere).
     */
    fun addressed(heard: String): String? {
        val w = words(heard)
        for (i in w.indices) {
            val greeted = (w[i] in NAME && i > 0 && w[i - 1] in HAIL) ||
                          (w[i].startsWith("yo") && w[i].drop(2) in NAME) || (w[i].startsWith("ayo") && w[i].drop(3) in NAME)
            if (greeted) return w.drop(i + 1).joinToString(" ")
        }
        return null
    }

    /** "stop listening", "that's all", "go to sleep": ends keep-listening */
    fun isStop(cmd: String) = cmd in setOf("stop", "stop listening", "that's all", "thats all", "go to sleep",
                                            "stop it", "be quiet", "bye", "goodbye", "never mind", "nevermind")

    private val SLASH = setOf("slash", "slesh", "slush", "flash")

    // how people say a command -> what Loomy calls it (start of a phrase only)
    private val SPOKEN = listOf(
        "recycle bin" to "rb", "recycling bin" to "rb", "trash" to "rb",
        "system info" to "si", "sys info" to "si", "system information" to "si", "system stats" to "si",
        "web config" to "webconfig",
        "packages" to "pkg", "package" to "pkg",
        "d p" to "dp", "dee pee" to "dp",
        "reset window" to "resetwindow",
        "network" to "net", "connections" to "net",
        "loomy's path" to "lp", "loomys path" to "lp", "my folders" to "lp", "folders" to "lp",
        "what can you do" to "help", "commands" to "help",
    ).map { (said, cmd) -> said.split(' ') to cmd }

    private val LEAD = listOf("please", "can you", "could you", "would you", "will you", "can u", "just", "go ahead and",
                              "i want to", "i wanna", "i want you to", "let's", "lets", "now", "okay", "ok", "so")
        .map { it.split(' ') }
    private val TAIL = listOf("please", "for me", "now", "thanks", "thank you", "real quick", "rn").map { it.split(' ') }
    private val OPEN = setOf("open", "launch", "start", "show", "bring", "pull", "go")   // "open system info", "bring up si"

    private val SCREEN = setOf("screen", "screens", "monitor", "monitors", "displays")
    private val SCREEN_FILL = setOf("turn", "switch", "shut", "put", "the", "my", "both", "all", "to", "up", "down", "off", "on",
                                    "sleep", "wake", "show", "back", "screen", "screens", "monitor", "monitors", "displays", "display")

    private val NUM = mapOf("one" to "1", "two" to "2", "three" to "3", "four" to "4", "five" to "5",
                            "six" to "6", "seven" to "7", "eight" to "8", "nine" to "9")

    // what the PC's "input" understands, said naturally (whole phrase only)
    private val KEYS = mapOf(
        "go back" to "input back", "back" to "input back", "close it" to "input back", "close that" to "input back",
        "next tab" to "input next tab", "previous tab" to "input previous tab", "last tab" to "input previous tab",
        "go to the next tab" to "input next tab", "go to next tab" to "input next tab",
        "go to the previous tab" to "input previous tab",
        "next" to "input next", "previous" to "input previous",
        "go up" to "input up", "go down" to "input down", "go left" to "input left", "go right" to "input right",
        "move up" to "input up", "move down" to "input down", "scroll up" to "input up", "scroll down" to "input down",
        "select it" to "input enter", "select that" to "input enter", "pick it" to "input enter", "enter" to "input enter",
    ).map { (said, cmd) -> said.split(' ') to cmd }

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
        // the screens: "turn off the screen", "screen off", "show screen", "turn the display off"
        val screenish = w.any { it in SCREEN } || (w[0] != "display" && "display" in w)
        if (screenish && w.all { it in SCREEN_FILL }) {
            val off = w.any { it == "off" || it == "sleep" || it == "shut" || it == "down" }
            val on = w.any { it == "on" || it == "wake" || it == "show" || it == "back" } || (w.contains("up") && !off)
            if (off && !on) return "display off"
            if (on && !off) return "display on"
        }
        // keys for the open screen
        if (w[0] == "press" && w.size > 1) return "input " + w.drop(1).joinToString(" ")
        for ((said, cmd) in KEYS) if (w == said) return cmd
        // "open system info", "bring up the recycle bin", "show me the network", "open loomy"
        if (w[0] in OPEN && w.size > 1) {
            var r = w.drop(1)
            if (r.firstOrNull() == "up" || r.firstOrNull() == "me" || r.firstOrNull() == "to") r = r.drop(1)
            if (r.firstOrNull() == "the" || r.firstOrNull() == "my") r = r.drop(1)
            if (r.size == 1 && r[0] in NAME) return "open"
            if (r.size == 1 && r[0] == "it") return "input enter"
            for ((said, _) in SPOKEN) if (startsWith(r, said)) { w = r; break }
        }
        // a slash: "slash packages" -> "/pkg"
        var slash = false
        if (w[0] in SLASH) { slash = true; w = w.drop(1) }
        else if (w[0].startsWith("/") && w[0].length > 1) { slash = true; w = listOf(w[0].substring(1)) + w.drop(1) }
        if (w.isEmpty()) return ""
        for ((said, cmd) in SPOKEN) if (startsWith(w, said)) { w = listOf(cmd) + w.drop(said.size); break }
        // "gpu one" -> "gpu 1"
        val ww = w
        w = ww.mapIndexed { i, x -> if (i > 0 && ww[i - 1] == "gpu") NUM[x] ?: x else x }
        val text = w.joinToString(" ")
        return if (slash) "/$text" else text
    }

    /**
     * Everything said -> what's sent to Loomy. "Loomy, display extend." -> "display extend";
     * "ayo loomy open system info and go to the next tab" -> "si and input next tab". Each part between
     * "and"s is cleaned on its own; Loomy on the PC decides where "and" really splits.
     */
    fun command(heard: String): String {
        var w = words(heard)
        if (w.size > 1 && w[0] in HAIL && w[1] in NAME) w = w.drop(2)     // "yo loomy ..."
        if (w.isNotEmpty() && w[0] in NAME) w = w.drop(1)                 // "loomy ..."
        if (w.size > 1 && w[0] == "run") w = w.drop(1)
        val parts = mutableListOf<MutableList<String>>(mutableListOf())
        for (x in w) if (x == "and" || x == "then") parts += mutableListOf<String>() else parts.last() += x
        return parts.map { one(it) }.filter { it.isNotEmpty() }.joinToString(" and ")
    }
}
