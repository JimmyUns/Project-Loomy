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
    private fun words(s: String) = joinName(s.lowercase().replace('\u2019', '\'').split(Regex("[^a-z0-9/'._:-]+"))
        .map { it.trim('.', ':', '-', '_', '\'') }.filter { it.isNotEmpty() })

    // 2.22: the recognizer writes her name a hundred ways with his accent ("lumi", "lumie", "loomino", "lou me").
    // Anything shaped like l + oo/u/ou/o + m + a short ending counts, and "lou me" / "lu me" said as two words.
    private val NAMEISH = Regex("^(?:yo|ayo)?l(?:oo|ou|u|o|ew)m{1,2}(?:y|i|ie|ee|ey|ay|e|ia|io|ino|ina|inos|is|ies|i's|y's|ie's|a|ah)?$")
    private val FIRST = setOf("lou", "lu", "loo", "lew", "luu")
    private val SECOND = setOf("me", "mi", "mee", "may", "my", "mie")
    private fun joinName(w: List<String>): List<String> {
        val out = ArrayList<String>(w.size)
        var i = 0
        while (i < w.size) {
            if (i + 1 < w.size && w[i] in FIRST && w[i + 1] in SECOND) { out += "loomy"; i += 2; continue }
            out += w[i]; i++
        }
        return out
    }

    private fun isName(x: String) = x in NAME || (x.startsWith("yo") && x.drop(2) in NAME) || (x.startsWith("ayo") && x.drop(3) in NAME) ||
        (NAMEISH.matches(x) && x != "loom" && x != "lome")

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
        // 2.23: what you mean, not the technical word: "show me only the first monitor" -> display pc,
        // "open the general system information" -> si general
        phoneScreen(w)?.let { return it }
        layout(w)?.let { return it }
        sysinfoView(w)?.let { return it }
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

    /** Does every part of this command start with a word Loomy knows? ("display off and si" yes, "make the
     *  screens dark" no). The ones that don't go to Ellune's AI when she's reachable (Voice). */
    fun knows(cmd: String): Boolean {
        if (cmd.isBlank()) return false
        return cmd.split(" and ").all { p ->
            val w = p.trim().split(' ').filter { it.isNotEmpty() }
            val first = w.firstOrNull() ?: ""
            val args = w.drop(1)
            when {
                first.startsWith("/") || first in setOf("input", "web", "lp") -> true
                // 2.23: the arguments have to be real too, so an accent slip ("display extent", "si cpus") goes to
                // Ellune's AI instead of reaching the PC as a command it refuses
                first == "display" || first == "dp" -> args.isEmpty() || (args.size == 1 && args[0] in DISPLAY_ARGS)
                first == "si" || first == "sysinfo" -> args.isEmpty() || (args.size == 1 && args[0] in setOf("general", "cpu", "gpu")) ||
                    (args.size == 2 && args[0] == "gpu" && args[1].length == 1 && args[1][0] in '1'..'9')
                first in Phrases.KNOWN -> args.isEmpty()                    // help, net, rb, hide, open...: no words after
                else -> false
            }
        }
    }
    private val DISPLAY_ARGS = setOf("extend", "second", "pc", "duplicate", "off", "on")

    // what Whisper writes when nobody really said anything (typing, a cough, music, silence)
    private val NOISE_TAGS = Regex("\\[[^\\]]*]|\\([^)]*\\)|\\*[^*]*\\*|[\u266A\u266B\u266C]")
    private val NOISE_LINES = setOf("you", "thank you", "thanks", "thanks for watching", "thank you for watching",
        "bye", "bye bye", "uh", "um", "hmm", "mm", "oh", "ah", "so", "the", "i", "a",
        "please subscribe", "subscribe", "silence", "music", "applause", "laughter")

    /** 2.22: is this just Whisper hearing a noise? (nothing but tags like "[typing]", one filler word, a classic
     *  "Thanks for watching!", or no letters at all). Real commands are never this. */
    fun isNoise(heard: String): Boolean {
        val t = NOISE_TAGS.replace(heard, " ").lowercase().replace(Regex("[^a-z' ]"), " ").replace(Regex("\\s+"), " ").trim()
        return t.length < 2 || t in NOISE_LINES
    }

    private val THANKS = Regex("\\b(thank you|thank u|thanks|thx)\\b", RegexOption.IGNORE_CASE)

    /** 2.22: "thank you" ends a sentence. What came before it, and whether it was there at all.
     *  "turn off the screens thank you" -> ("turn off the screens", true). "please" is never an end. */
    fun untilThanks(heard: String): Pair<String, Boolean> {
        val m = THANKS.find(heard) ?: return heard to false
        return heard.substring(0, m.range.first).trim().trimEnd(',', '.', '!', '?').trim() to true
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

    // ------------------------------------------------------------------ 2.23: saying what you mean
    // (his ask: "show me only the first monitor" should just work, without "PC screen only"). Each rule only fires
    // when every word is one it understands; anything else goes to Ellune's AI as before.
    private val SCREENW = setOf("screen", "screens", "monitor", "monitors", "display", "displays")
    private val ORD1 = setOf("first", "1st", "main", "primary", "pc", "laptop", "left", "computer", "computer's", "pc's")
    private val ORD2 = setOf("second", "2nd", "two", "2", "other", "external", "right", "tv")
    private val EXTENDW = setOf("both", "all", "extend", "extended", "across", "every")
    private val DUPW = setOf("same", "mirror", "mirrored", "duplicate", "duplicated", "copy")
    private val LAYOUT_FILL = setOf("show", "me", "only", "just", "the", "my", "switch", "to", "use", "put", "it", "on", "set",
        "go", "change", "make", "a", "i", "want", "need", "keep", "everything", "stuff", "one", "of", "over", "onto", "with",
        "turn", "off", "get", "rid", "be", "thing", "that", "this", "is", "back", "screen", "screens", "monitor",
        "monitors", "display", "displays", "and", "in", "mode", "can", "you")

    /** a screen layout said in plain words, or null */
    fun layout(w: List<String>): String? {
        val named = w.any { it in SCREENW }
        val second = w.any { it in ORD2 }
        val first = w.any { it in ORD1 } || (!second && w.any { it == "one" || it == "1" })
        if (!named && !(w.firstOrNull() == "switch" && (first || second))) return null      // "switch to the second"
        if (!w.all { it in LAYOUT_FILL || it in ORD1 || it in ORD2 || it in EXTENDW || it in DUPW || it == "1" }) return null
        val off = "off" in w || "rid" in w
        val turnOn = "turn" in w && "on" in w && !off
        return when {
            w.any { it in DUPW } -> "display duplicate"
            first && second -> if (off) null else "display extend"                 // "the first and second screen"
            (first || second) && turnOn -> "display extend"                        // "turn on the second screen" = both
            first -> if (off) "display second" else "display pc"                   // "turn off the first screen" = only the 2nd
            second -> if (off) "display pc" else "display second"
            w.any { it in EXTENDW } && !off -> "display extend"
            else -> null
        }
    }

    private val SI_NAME = setOf("info", "information", "stats", "statistics", "details", "specs")
    private val SI_FILL = setOf("open", "show", "me", "the", "my", "view", "page", "tab", "of", "for", "about", "go", "to",
        "switch", "bring", "up", "pull", "a", "system", "sys", "pc", "computer", "on", "part", "section", "card", "video",
        "can", "you", "i", "want", "see", "check", "in", "and", "number", "whats", "what's", "how", "is", "doing", "usage")
    private val SI_VIEW = mapOf("general" to "general", "overview" to "general", "summary" to "general", "basic" to "general",
        "basics" to "general", "cpu" to "cpu", "processor" to "cpu", "gpu" to "gpu", "graphics" to "gpu")

    /** a system info view said in plain words ("open the general system information"), or null */
    fun sysinfoView(w: List<String>): String? {
        val named = w.any { it in SI_NAME } && w.any { it == "system" || it == "sys" || it == "pc" || it == "computer" } ||
                    w.firstOrNull() == "si" || w.firstOrNull() == "sysinfo"
        if (!named) return null
        val views = w.mapNotNull { SI_VIEW[it] }.distinct()
        if (views.size != 1) return null
        val n = w.firstNotNullOfOrNull { NUM[it] ?: it.takeIf { x -> x.length == 1 && x[0] in '1'..'9' } }
            ?: if ("second" in w) "2" else null
        if (!w.all { it in SI_FILL || it in SI_NAME || it in SI_VIEW || it in NUM || it == "si" || it == "sysinfo" ||
                     it == "second" || it == "first" || (it.length == 1 && it[0] in '1'..'9') }) return null
        return if (views[0] == "gpu" && n != null) "si gpu $n" else "si ${views[0]}"
    }

    // ------------------------------------------------------------------ 2.23: "Which one?" (a list opened on the PC)
    /** the PC's question in a reply: "Which one (si)? general, cpu, gpu 1." -> ("si", [general, cpu, gpu 1]) */
    fun question(reply: String): Pair<String, List<String>>? {
        for (line in reply.lines()) {
            val m = QUESTION.matchEntire(line.trim()) ?: continue
            val opts = m.groupValues[2].trimEnd('.').split(", ").map { it.trim().lowercase() }.filter { it.isNotEmpty() && it != "..." }
            if (opts.isNotEmpty()) return m.groupValues[1] to opts
        }
        return null
    }
    private val QUESTION = Regex("^Which one \\(([a-z]{1,12})\\)\\? (.{1,390})$")

    /** the reply to show, with the question line put the phone's way and a lone "Done." dropped */
    fun withoutQuestion(reply: String): String =
        reply.lines().filter { QUESTION.matchEntire(it.trim()) == null && it.trim() != "Done." }.joinToString("\n").trim()

    private val CANCEL = setOf("cancel", "never mind", "nevermind", "forget it", "forget about it", "go back", "back", "no",
        "nope", "nah", "none", "nothing", "stop", "exit", "close", "close it", "leave it", "skip", "skip it", "no thanks",
        "not now", "none of them", "neither", "cancel that", "cancel it", "dont", "don't", "i'm good", "im good", "all good")
    private val CANCEL_FILL = setOf("ok", "okay", "oh", "uh", "um", "actually", "please", "just", "alright", "wait", "then", "thanks", "thank", "you")

    /** "ok cancel", "never mind", "go back": drop the question (the PC isn't touched: you may have picked there) */
    fun isCancel(heard: String): Boolean {
        var w = unname(words(heard))
        while (w.size > 1 && w[0] in CANCEL_FILL) w = w.drop(1)
        while (w.size > 1 && w.last() in CANCEL_FILL) w = w.dropLast(1)
        return w.joinToString(" ") in CANCEL
    }

    private val PICK_FILL = setOf("the", "a", "an", "please", "i", "want", "show", "me", "open", "go", "to", "with", "let's", "lets",
        "um", "uh", "ok", "okay", "number", "option", "just", "pick", "choose", "select", "can", "you", "that", "this", "for",
        "thanks", "thank", "it", "view", "page", "tab", "use", "one", "take", "give", "do", "yeah", "yes", "hmm", "how", "about", "on")
    private val ORDINAL = mapOf("first" to 0, "1st" to 0, "second" to 1, "2nd" to 1, "third" to 2, "3rd" to 2, "fourth" to 3,
        "4th" to 3, "fifth" to 4, "5th" to 4)
    private val DISPLAY_SAY = mapOf("both" to "extend", "all" to "extend", "extend" to "extend", "extended" to "extend",
        "across" to "extend", "mirror" to "duplicate", "same" to "duplicate", "copy" to "duplicate", "duplicate" to "duplicate",
        "first" to "pc", "1st" to "pc", "main" to "pc", "primary" to "pc", "pc" to "pc", "laptop" to "pc", "one" to "pc",
        "second" to "second", "2nd" to "second", "other" to "second", "external" to "second", "two" to "second",
        "off" to "off", "dark" to "off", "sleep" to "off")

    /**
     * The answer to "Which one?": "general please" -> "si general", "the cpu one" -> "si cpu", "graphics" -> "si gpu 1",
     * "the second one" -> the 2nd choice, "first screen" (layouts) -> "display pc". Null when it isn't one of them.
     */
    fun pick(heard: String, cmd: String, options: List<String>): String? {
        val raw = unname(words(heard))
        val w = raw.mapIndexed { i, x -> if (i > 0 && raw[i - 1] == "gpu") NUM[x] ?: x else x }   // "gpu one" -> "gpu 1"
        if (w.isEmpty()) return null
        // 0. layouts in plain words first: "turn off the second screen" means the first one only
        if (cmd == "display") layout(w)?.removePrefix("display ")?.takeIf { it in options }?.let { return "display $it" }
        // 1. an option said as is ("general", "gpu 2", "youtube"), longest first
        for (o in options.sortedByDescending { it.length }) {
            val ow = o.split(' ')
            if (w.windowed(ow.size).any { it == ow }) return "$cmd $o"
        }
        // 2. other words for them
        val rest = w.filter { it !in PICK_FILL && it !in SCREENW }
        if (rest.isEmpty() || rest.size > 3) return null
        if (cmd == "display") rest.firstNotNullOfOrNull { DISPLAY_SAY[it] }?.takeIf { it in options }?.let { return "display $it" }
        if (cmd == "si") {
            val v = rest.firstNotNullOfOrNull { SI_VIEW[it] }
            if (v == "gpu") {
                val gpus = options.filter { it.startsWith("gpu") }
                val n = rest.firstOrNull { it.length == 1 && it[0] in '1'..'9' } ?: if ("second" in rest) "2" else null
                if (n != null && "gpu $n" in options) return "si gpu $n"
                if (n == null && gpus.size == 1) return "si ${gpus[0]}"
            } else if (v != null && v in options) return "si $v"
        }
        // 3. by place: "the first one", "the second", "number 2", "the last one"
        if (rest.size == 1) {
            val r = rest[0]
            val i = ORDINAL[r] ?: (if (r == "last") options.size - 1 else null) ?: r.toIntOrNull()?.minus(1)
            if (i != null && i in options.indices) return "$cmd ${options[i]}"
        }
        return null
    }

    /** how a choice is shown: "cpu" -> "CPU", "gpu 1" -> "GPU 1", layouts in words */
    fun pretty(cmd: String, o: String): String = when {
        cmd == "display" -> mapOf("extend" to "Extend (both)", "second" to "Second screen only", "pc" to "First screen only",
                                  "duplicate" to "Duplicate", "off" to "Screens off")[o] ?: o
        o == "cpu" || o.startsWith("gpu") || o == "pc" -> o.uppercase()
        else -> o.replaceFirstChar { it.uppercase() }
    }

    /** the question, the phone's way */
    fun ask(cmd: String) = when (cmd) { "si" -> "Which view?"; "display" -> "Which layout?"; "web" -> "Which site?"; else -> "Which one?" }

    // ------------------------------------------------------------------ 2.24: the Phone Screen package (scrcpy)
    private val PHONEW = setOf("phone", "phones", "mobile", "android", "cellphone", "cell", "samsung", "pixel", "phone's")
    private val PHONE_DO = setOf("mirror", "show", "display", "cast", "copy", "share", "see", "view", "plugged", "plug",
        "screen", "put", "open", "bring", "stream")
    private val PHONE_FILL = PHONEW + PHONE_DO + setOf("it", "on", "the", "my", "screens", "pc", "computer", "monitor", "please",
        "i", "in", "just", "up", "of", "to", "a", "now", "can", "you", "me", "let", "lets", "let's", "have", "want", "here",
        "this", "that", "its", "it's", "over", "onto", "is", "so", "and", "into")

    /** "mirror my phone", "i plugged in my phone please display it", "show my phone screen" -> scrcpy. Needs a phone
     *  word and something to do with it, and nothing else ("pair my phone" isn't this). */
    fun phoneScreen(w: List<String>): String? {
        if (w.none { it in PHONEW } || w.none { it in PHONE_DO }) return null
        return if (w.all { it in PHONE_FILL }) "scrcpy" else null
    }
}
