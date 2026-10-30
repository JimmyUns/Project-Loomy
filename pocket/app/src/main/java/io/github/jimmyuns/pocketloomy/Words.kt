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

    private val WAKE = listOf("loomy", "loomie", "lumi", "lumie", "looney", "lumy", "loomi")

    /** "Loomy, display extend." -> "display extend"; "Loomy run web gmail" -> "web gmail" */
    fun command(heard: String): String {
        var words = heard.trim().trimEnd('.', '!', '?', ',').lowercase()
            .split(Regex("\\s+")).map { it.trim(',', '.') }.filter { it.isNotEmpty() }
        if (words.isNotEmpty() && words[0] in WAKE) words = words.drop(1)
        if (words.size > 1 && words[0] == "run") words = words.drop(1)
        return words.joinToString(" ")
    }
}
