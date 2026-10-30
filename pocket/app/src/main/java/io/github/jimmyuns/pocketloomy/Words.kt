package io.github.jimmyuns.pocketloomy

// Turning what the speech recognizer heard into a pairing code or a Loomy command.
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

    private val WAKE = setOf("loomy", "loomie", "lumi", "lumie", "looney", "lumy", "loomi", "lumee")
    private val SLASH = setOf("slash", "slesh", "slush", "flash")   // recognizers hear "slash" a few ways

    // how people say a command -> what Loomy calls it (first words only)
    private val SPOKEN = listOf(
        listOf("recycle", "bin") to "rb",
        listOf("recycling", "bin") to "rb",
        listOf("system", "info") to "si",
        listOf("sys", "info") to "si",
        listOf("system", "information") to "si",
        listOf("web", "config") to "webconfig",
        listOf("packages") to "pkg",
        listOf("package") to "pkg",
        listOf("d", "p") to "dp",
        listOf("dee", "pee") to "dp",
        listOf("reset", "window") to "resetwindow",
    )

    /** "Loomy, display extend." -> "display extend"; "slash packages" -> "/pkg"; "recycle bin" -> "rb" */
    fun command(heard: String): String {
        var words = heard.trim().lowercase()
            .split(Regex("\\s+")).map { it.trim(',', '.', '!', '?') }.filter { it.isNotEmpty() }
        if (words.isNotEmpty() && words[0] in WAKE) words = words.drop(1)
        if (words.size > 1 && words[0] == "run") words = words.drop(1)
        var slash = false
        if (words.isNotEmpty() && words[0] in SLASH) { slash = true; words = words.drop(1) }
        else if (words.isNotEmpty() && words[0].startsWith("/") && words[0].length > 1) { slash = true; words = listOf(words[0].substring(1)) + words.drop(1) }
        for ((said, cmd) in SPOKEN) {
            if (words.size >= said.size && words.subList(0, said.size) == said) { words = listOf(cmd) + words.drop(said.size); break }
        }
        val text = words.joinToString(" ")
        return if (slash && text.isNotEmpty()) "/$text" else text
    }
}
