package io.github.jimmyuns.pocketloomy

// Phrases.kt - the library of ways to say things. Words.kt cleans up what was heard (her name, "can you",
// "please"...) and then looks the whole phrase up here. To teach Loomy a new way of saying something,
// add a line: lowercase, no punctuation, fillers already gone ("can you hide please" arrives as "hide").
object Phrases {
    /** whole phrase -> the command Loomy runs */
    val COMMANDS: Map<String, String> = mapOf(
        // Loomy herself
        "hide" to "hide", "hide loomy" to "hide", "hide yourself" to "hide", "go away" to "hide", "minimize" to "hide",
        "minimise" to "hide", "off" to "hide", "height" to "hide", "hight" to "hide", "hyde" to "hide", "hied" to "hide",
        "show" to "open", "show loomy" to "open", "show yourself" to "open", "open loomy" to "open", "come back" to "open",
        "come here" to "open", "on" to "open", "bring loomy up" to "open", "pull loomy up" to "open", "wake up" to "open",
        // system info
        "system info" to "si", "sys info" to "si", "system information" to "si", "system stats" to "si", "pc stats" to "si",
        "how's my pc doing" to "si", "hows my pc doing" to "si", "how is my pc doing" to "si", "show me the stats" to "si",
        "performance" to "si", "cpu usage" to "si cpu", "cpu stats" to "si cpu", "processor" to "si cpu",
        "gpu usage" to "si gpu", "gpu stats" to "si gpu", "graphics card" to "si gpu", "graphics" to "si gpu",
        // screen layouts
        "extend" to "display extend", "extend the screens" to "display extend", "extend screens" to "display extend",
        "extend the display" to "display extend", "extend displays" to "display extend", "extend the displays" to "display extend",
        "duplicate" to "display duplicate", "duplicate the screens" to "display duplicate", "duplicate screens" to "display duplicate",
        "mirror" to "display duplicate", "mirror the screens" to "display duplicate", "mirror the screen" to "display duplicate",
        "second screen only" to "display second", "only the second screen" to "display second", "just the second screen" to "display second",
        "pc screen only" to "display pc", "main screen only" to "display pc", "only the main screen" to "display pc",
        "just the main screen" to "display pc", "just the pc screen" to "display pc",
        "screen layout" to "display", "screen layouts" to "display", "screens" to "display",
        // the rest
        "recycle bin" to "rb", "recycling bin" to "rb", "trash" to "rb", "the trash" to "rb", "bin" to "rb",
        "help" to "help", "what can you do" to "help", "what can i say" to "help", "commands" to "help",
        "network" to "net", "connections" to "net", "what's using the internet" to "net", "whats using the internet" to "net",
        "my folders" to "lp", "folders" to "lp", "loomy's path" to "lp", "loomys path" to "lp",
        "websites" to "web", "my sites" to "web", "my websites" to "web",
    )

    /** how people start a command name ("system info cpu", "recycle bin") -> what Loomy calls it */
    val STARTS: List<Pair<List<String>, String>> = listOf(
        "recycle bin" to "rb", "recycling bin" to "rb", "trash" to "rb",
        "system info" to "si", "sys info" to "si", "system information" to "si", "system stats" to "si",
        "web config" to "webconfig", "packages" to "pkg", "package" to "pkg",
        "d p" to "dp", "dee pee" to "dp", "reset window" to "resetwindow",
        "network" to "net", "loomy's path" to "lp", "loomys path" to "lp", "my folders" to "lp", "folders" to "lp",
    ).map { (said, cmd) -> said.split(' ') to cmd }

    /** Loomy's own command words: "open <one of these>" runs it; "open <anything else>" means a saved site */
    val KNOWN = setOf("help", "lp", "web", "net", "rb", "display", "dp", "sysinfo", "si", "gh", "github", "exe", "hide", "open")

    /** keys for Loomy's open screen: phrase -> the key ("input <key>" on the PC) */
    val KEYS: Map<String, String> = mapOf(
        "next" to "next", "next tab" to "next tab", "tab" to "next tab", "go to the next tab" to "next tab",
        "go to next tab" to "next tab", "next page" to "next tab", "switch tab" to "next tab", "tab right" to "next tab",
        "right tab" to "next tab", "go right one tab" to "next tab", "one tab right" to "next tab", "move one tab right" to "next tab",
        "forward" to "next tab", "next view" to "next tab", "the next one" to "next",
        "previous" to "previous", "prev" to "previous", "previous tab" to "previous tab", "last tab" to "previous tab",
        "go back one tab" to "previous tab", "back one tab" to "previous tab", "one tab back" to "previous tab",
        "tab left" to "previous tab", "left tab" to "previous tab", "go left one tab" to "previous tab",
        "one tab left" to "previous tab", "previous page" to "previous tab", "go to the previous tab" to "previous tab",
        "back" to "back", "go back" to "back", "escape" to "back", "exit" to "back", "close" to "close", "close it" to "back",
        "close that" to "back", "close this" to "back", "get out" to "back", "leave" to "back", "cancel" to "cancel",
        "ok" to "ok", "okay" to "ok", "confirm" to "confirm", "yes" to "yes", "enter" to "enter", "select" to "select",
        "select it" to "select", "select that" to "select", "pick it" to "select", "pick that" to "select",
        "choose it" to "select", "choose that" to "select", "that one" to "select", "open it" to "enter", "do it" to "enter",
        "up" to "up", "go up" to "up", "move up" to "up", "scroll up" to "up", "up one" to "up", "one up" to "up", "higher" to "up",
        "down" to "down", "go down" to "down", "move down" to "down", "scroll down" to "down", "down one" to "down",
        "one down" to "down", "lower" to "down",
        "left" to "left", "go left" to "left", "move left" to "left", "right" to "right", "go right" to "right", "move right" to "right",
        "page up" to "page up", "page down" to "page down",
        "home" to "home", "top" to "home", "go to the top" to "home", "to the top" to "home",
        "end" to "end", "bottom" to "end", "go to the bottom" to "end", "to the bottom" to "end",
    )

    /** mic off until Talk is tapped again (Loomy stays connected). Never a single word, so it can't slip out. */
    val MUTE = setOf("turn off the mic", "turn the mic off", "mic off", "mute the mic", "mute mic", "mute yourself",
        "turn off the microphone", "turn the microphone off", "microphone off", "stop listening", "stop listening to me",
        "turn off listening", "go to sleep", "that's all for now", "thats all for now")

    /** solo mode: back to waiting for her name ("thanks, that's it"), mic stays on */
    val SLEEP = setOf("thank you", "thanks", "thank you loomy", "thanks loomy", "stop listening", "that's it", "thats it",
        "that's all", "thats all", "that is it", "that is all", "we're done", "were done", "i'm done", "im done")

    /** turns Pocket Loomy's On switch off */
    val DISCONNECT = setOf("stop connection", "stop the connection", "end connection", "end the connection",
        "disconnect", "disconnect from the pc", "cut the connection", "cut connection", "close the connection",
        "close connection", "turn off connection", "turn off the connection", "turn the connection off",
        "connection off", "stop connecting", "kill the connection")

    val INPUT_ON = setOf("input mode", "input mode on", "enable input mode", "start input mode", "turn on input mode",
        "turn input mode on", "go into input mode", "enter input mode", "inputs mode")
    val INPUT_OFF = setOf("input mode off", "disable input mode", "stop input mode", "exit input mode", "end input mode",
        "turn off input mode", "turn input mode off", "leave input mode", "close input mode", "no more input mode")
    val SOLO_ON = setOf("solo mode", "solo mode on", "enable solo mode", "turn on solo mode", "turn solo mode on", "start solo mode")
    val SOLO_OFF = setOf("solo mode off", "disable solo mode", "turn off solo mode", "turn solo mode off", "stop solo mode",
        "exit solo mode", "end solo mode", "leave solo mode", "no more solo mode")
}
