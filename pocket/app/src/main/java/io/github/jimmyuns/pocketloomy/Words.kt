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
    private val NAME = setOf("loomy", "loomie", "lumi", "lumie", "looney", "loony", "lumy", "loomi", "lumee", "loomey", "lumey",
                             "luumi", "lumia", "loomis", "lumis", "lomi", "lume", "lumii", "loomiy", "lumee's", "lumi's", "loomy's")
    private val FILL = setOf("okay", "ok", "um", "uh", "so", "and", "oh", "alright", "right", "hey", "yo")
    private val HAIL = setOf("yo", "ayo", "eyo", "ayoo", "ayyo", "hey", "ay", "aye", "a", "oi", "hayo", "yoo")

    // words, keeping things like "youtube.com" or "c:/games" whole but dropping sentence punctuation
    private fun words(s: String) = s.lowercase().replace('\u2019', '\'').split(Regex("[^a-z0-9/'._:-]+"))
        .map { it.trim('.', ':', '-', '_', '\'') }.filter { it.isNotEmpty() }

    private fun isName(x: String) = x in NAME || (x.startsWith("yo") && x.drop(2) in NAME) || (x.startsWith("ayo") && x.drop(3) in NAME)

    /**
     * Keep-listening mode: was this said TO Loomy? Her name has to open what was said ("Loomy show screen",
     * "okay Lumi, system info", "ayo Loomy ..."), or follow a greeting anywhere ("so anyway, yo Loomy ...").
     * A name in the middle of a sentence ("I told Loomy about it") doesn't count. Returns what came after
     * the name ("" = just the name: a follow-up is coming), or null when it wasn't for Loomy (then it's
     * ignored and never sent anywhere).
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
        "hide", "show", "web", "help", "stop listening", "slash")

    /** input mode: key words work without the greeting until it's turned off */
    fun isInputModeOn(cmd: String) = cmd in setOf("input mode", "input mode on", "enable input mode", "start input mode",
        "turn on input mode", "turn input mode on", "go into input mode", "enter input mode", "inputs mode")
    fun isInputModeOff(cmd: String) = cmd in setOf("input mode off", "disable input mode", "stop input mode", "exit input mode",
        "end input mode", "turn off input mode", "turn input mode off", "leave input mode", "close input mode", "no more input mode")

    // input mode: what counts as a key (said bare, no "ayo Loomy" needed). Anything else is ignored.
    private val KEYWORD = mapOf(
        "next" to "next", "next tab" to "next tab", "tab" to "next tab",
        "previous" to "previous", "previous tab" to "previous tab", "last tab" to "previous tab", "prev" to "previous",
        "back" to "back", "go back" to "back", "escape" to "back", "cancel" to "cancel", "close" to "close", "exit" to "back",
        "confirm" to "confirm", "ok" to "ok", "okay" to "ok", "enter" to "enter", "select" to "select", "yes" to "yes",
        "select it" to "select", "open it" to "enter", "pick it" to "select",
        "up" to "up", "down" to "down", "left" to "left", "right" to "right",
        "page up" to "page up", "page down" to "page down", "home" to "home", "top" to "home", "end" to "end", "bottom" to "end",
    )

    /** input mode: "next tab" -> "input next tab", "go down three" -> "input down 3"; not a key -> null */
    fun key(heard: String): String? {
        var w = words(heard)
        while (w.size > 1 && (w[0] in HAIL || w[0] in FILL) && w.drop(1).any { isName(it) }) w = w.drop(1)
        if (w.isNotEmpty() && isName(w[0])) w = w.drop(1)
        if (w.isNotEmpty() && w[0] == "input") w = w.drop(1)              // "input down" said in input mode
        while (w.isNotEmpty() && w[0] in setOf("press", "hit", "go", "move", "and", "then", "now", "the")) {
            if (w.size == 2 && w[0] == "go" && w[1] == "back") break
            w = w.drop(1)
        }
        while (w.isNotEmpty() && w.last() in setOf("please", "now", "times", "time")) w = w.dropLast(1)
        if (w.isEmpty()) return null
        var times = ""
        val n = NUM[w.last()] ?: w.last().takeIf { it.length == 1 && it[0] in '1'..'9' }
        if (n != null && w.size > 1) { times = " $n"; w = w.dropLast(1) }
        val k = KEYWORD[w.joinToString(" ")] ?: return null
        return "input $k$times"
    }

    /** solo mode: when he's alone, everything said is for Loomy (no name needed) until it's turned off */
    private fun solo(cmd: String) = cmd.replace("so low", "solo").replace("so lo", "solo").replace("sola", "solo")
    fun isSoloOn(cmd: String) = solo(cmd) in setOf("solo mode", "solo mode on", "enable solo mode", "turn on solo mode",
        "turn solo mode on", "start solo mode")
    fun isSoloOff(cmd: String) = solo(cmd) in setOf("solo mode off", "disable solo mode", "turn off solo mode",
        "turn solo mode off", "stop solo mode", "exit solo mode", "end solo mode", "leave solo mode", "no more solo mode")

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
        // his accent vs the recognizer: "hide" keeps coming out as "height"
        if (w.size == 1 && w[0] in setOf("height", "hight", "hyde", "hied", "heid")) return "hide"
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
        // "so low mode" is how "solo mode" often comes out
        w = w.joinToString(" ").replace("so low mode", "solo mode").replace("so lo mode", "solo mode").split(' ').filter { it.isNotEmpty() }
        while (w.size > 1 && (w[0] in HAIL || w[0] in FILL) && w.drop(1).any { isName(it) }) w = w.drop(1)   // "okay yo ..."
        if (w.isNotEmpty() && isName(w[0])) w = w.drop(1)                 // "loomy ..."
        if (w.size > 1 && w[0] == "run") w = w.drop(1)
        val parts = mutableListOf<MutableList<String>>(mutableListOf())
        for (x in w) if (x == "and" || x == "then") parts += mutableListOf<String>() else parts.last() += x
        return parts.map { one(it) }.filter { it.isNotEmpty() }.joinToString(" and ")
    }
}
