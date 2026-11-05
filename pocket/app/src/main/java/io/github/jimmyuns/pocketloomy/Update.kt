package io.github.jimmyuns.pocketloomy

// The update button (2.23), the plain part (no Android classes, so tests/jni runs it): reading GitHub's
// "latest release" answer and deciding whether to trust it. Same rules as /update on the PC (version.c
// release_check), plus the checksum: the release must be published by JimmyUns, and pocket-loomy.apk on it must
// have been uploaded by GitHub Actions (his tag build), with GitHub's sha256 of the file. Updater.kt downloads it,
// checks the file against that sha256, and hands it to Android, which refuses anything not signed with the same
// key as the installed app: that's the real guard, the checks here just stop a wrong file early with a clear reason.
object Update {
    const val OWNER = "JimmyUns"
    const val REPO = "Project-Loomy"
    const val API = "https://api.github.com/repos/$OWNER/$REPO/releases/latest"
    const val APK = "pocket-loomy.apk"
    const val BUILDER = "github-actions[bot]"
    const val DOWNLOADS = "https://github.com/$OWNER/$REPO/releases/download/"
    const val MAX_APK = 64L * 1024 * 1024           // it's ~220 KB; anything near this is wrong

    sealed class Check {
        /** a release we trust: tag "v2.23", version "2.23", Android's versionCode for it, the APK and its sha256 */
        data class Ok(val tag: String, val version: String, val code: Int, val url: String, val size: Long, val sha256: String) : Check()
        data class Bad(val why: String) : Check()
    }

    private val VERSION = Regex("^[vV]?(\\d{1,3})\\.(\\d{1,2})([a-z]?)$")

    /** "2.23", "v2.9b" -> the versionCode the tag build gives the app (pocket.yml: major*10000 + minor*100 + letter,
     *  b = 2), so 2.9 < 2.9b < 2.10 like Loomy's versions. Anything else -> null. */
    fun code(version: String): Int? {
        val m = VERSION.matchEntire(version) ?: return null
        val (major, minor, letter) = m.destructured
        return major.toInt() * 10000 + minor.toInt() * 100 + (if (letter.isEmpty()) 0 else letter[0] - 'a' + 1)
    }

    /** GitHub's JSON for the latest release -> Ok or why not */
    fun check(json: String): Check {
        val r = Json.parse(json) as? Map<*, *> ?: return Check.Bad("GitHub's answer didn't make sense.")
        val tag = r["tag_name"] as? String ?: return Check.Bad("There's no release yet.")
        val code = code(tag) ?: return Check.Bad("The latest release has an odd name ($tag).")
        val version = tag.trimStart('v', 'V')
        if (r["draft"] == true || r["prerelease"] == true) return Check.Bad("The latest release ($tag) isn't a full release.")
        if ((r["author"] as? Map<*, *>)?.get("login") != OWNER) return Check.Bad("The latest release ($tag) wasn't published by $OWNER, so it isn't trusted.")
        val assets = r["assets"] as? List<*> ?: emptyList<Any?>()
        val a = assets.firstOrNull { (it as? Map<*, *>)?.get("name") == APK } as? Map<*, *>
            ?: return Check.Bad("The latest release ($tag) has no app on it yet. If it just came out, the build takes a few minutes: try again soon.")
        if ((a["uploader"] as? Map<*, *>)?.get("login") != BUILDER) return Check.Bad("The app on $tag wasn't put there by GitHub's build, so it isn't trusted.")
        if (a["state"] != "uploaded") return Check.Bad("The app on $tag is still uploading: try again in a minute.")
        val url = a["browser_download_url"] as? String
        if (url != "$DOWNLOADS$tag/$APK") return Check.Bad("The app on $tag has an odd download link, so it isn't trusted.")
        val size = (a["size"] as? Number)?.toLong() ?: 0L
        if (size < 1024 || size > MAX_APK) return Check.Bad("The app on $tag has an odd size ($size bytes).")
        val digest = a["digest"] as? String ?: ""
        if (!digest.startsWith("sha256:") || digest.length != 7 + 64 || !digest.drop(7).all { it in '0'..'9' || it in 'a'..'f' })
            return Check.Bad("GitHub didn't give a checksum for the app on $tag, so it can't be checked.")
        return Check.Ok(tag, version, code, url, size, digest.drop(7))
    }

    /** bytes -> lowercase hex (for comparing the sha256) */
    fun hex(b: ByteArray) = b.joinToString("") { "%02x".format(it.toInt() and 0xff) }

    fun size(bytes: Long) = if (bytes < 1024 * 1024) "${(bytes + 512) / 1024} KB" else "%.1f MB".format(bytes / 1048576.0)
}

/** Just enough JSON for GitHub's answers: objects -> Map, arrays -> List, strings, numbers (Long or Double),
 *  true/false/null. Throws on anything malformed (Update.check turns that into "didn't make sense"). */
object Json {
    fun parse(s: String): Any? = try {
        val p = P(s)
        val v = p.value(0)
        p.ws()
        if (p.i != s.length) null else v
    } catch (_: Exception) { null }

    private class P(val s: String) {
        var i = 0
        fun ws() { while (i < s.length && s[i] in " \t\r\n") i++ }
        fun value(depth: Int): Any? {
            if (depth > 32) throw IllegalStateException("too deep")
            ws()
            if (i >= s.length) throw IllegalStateException("end")
            return when (s[i]) {
                '{' -> {
                    i++
                    val m = LinkedHashMap<String, Any?>()
                    ws()
                    if (s[i] == '}') { i++; return m }
                    while (true) {
                        ws()
                        val k = str()
                        ws(); expect(':')
                        m[k] = value(depth + 1)
                        ws()
                        if (s[i] == ',') { i++; continue }
                        expect('}'); return m
                    }
                    @Suppress("UNREACHABLE_CODE") m
                }
                '[' -> {
                    i++
                    val l = ArrayList<Any?>()
                    ws()
                    if (s[i] == ']') { i++; return l }
                    while (true) {
                        l += value(depth + 1)
                        ws()
                        if (s[i] == ',') { i++; continue }
                        expect(']'); return l
                    }
                    @Suppress("UNREACHABLE_CODE") l
                }
                '"' -> str()
                't' -> word("true", true)
                'f' -> word("false", false)
                'n' -> word("null", null)
                else -> num()
            }
        }
        fun expect(c: Char) { if (i >= s.length || s[i] != c) throw IllegalStateException("expected $c"); i++ }
        fun word(w: String, v: Any?): Any? { if (!s.startsWith(w, i)) throw IllegalStateException(w); i += w.length; return v }
        fun num(): Any {
            val st = i
            while (i < s.length && (s[i].isDigit() || s[i] in "+-.eE")) i++
            val t = s.substring(st, i)
            return t.toLongOrNull() ?: t.toDoubleOrNull() ?: throw IllegalStateException("number")
        }
        fun str(): String {
            expect('"')
            val b = StringBuilder()
            while (true) {
                val c = s[i++]
                when {
                    c == '"' -> return b.toString()
                    c == '\\' -> when (val e = s[i++]) {
                        'n' -> b.append('\n'); 't' -> b.append('\t'); 'r' -> b.append('\r'); 'b' -> b.append('\b')
                        'f' -> b.append('\u000c'); 'u' -> { b.append(s.substring(i, i + 4).toInt(16).toChar()); i += 4 }
                        else -> b.append(e)
                    }
                    c < ' ' -> throw IllegalStateException("control char")
                    else -> b.append(c)
                }
            }
        }
    }
}
