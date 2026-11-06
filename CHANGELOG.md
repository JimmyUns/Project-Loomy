# Changelog

All notable changes to Loomy, newest first.

## v2.23a
A test build on the way to v2.24.
- **External packages.** `/pkg` (now also `/packages`) has an **External** column: packages that aren't built into Loomy but downloaded from their makers' GitHub releases when you turn them on. Turning one off deletes its files. `/pkg update <name>` gets the newest release.
- **Phone Screen (scrcpy)**, the first one: your Android phone's screen on the PC, controlled with mouse and keyboard. Type `scrcpy` (or `mirror`), or tell Pocket Loomy "mirror my phone", "show my phone", "copy screen", "I plugged in my phone, display it". The phone needs USB debugging on and a USB cable. If it closes right away, Loomy says why ("No phone found...").
- **Picking the right file** from a release is automatic (Windows zips only, the one for your processor). If it can't tell, press **E** on the package and type or paste the file's name; the version number is ignored so it fits next time. Ellune remembers that choice for your other PCs (on PCs set up for remote).
- **I** on any package shows what it does and its commands.
- Only files from the package's own repo, matching GitHub's checksum, are unpacked (with Windows' own tar.exe, into `%LOCALAPPDATA%\Loomy\Packages`).
- A command whose package is off now says how to turn it on, instead of "I don't know" (on the phone too).
- Ellune 0.7: shared settings (`/prefs`) for that, and her AI knows the Phone Screen command.

## v2.23
- **Say what you mean.** "Show me only the first monitor" switches to the first screen only, "turn off the second screen" too; "use both monitors", "same thing on both screens", "switch to the second" all work. "Open the general system information" goes straight to General (CPU and GPU the same way). The phone knows these itself; Ellune's AI (0.6) knows them too for anything said differently.
- **"Which view?"** When a command opens a list on the PC (system info, screen layouts, your sites), Pocket Loomy asks which one, in big letters over the mic. Just say it ("general please", "the second one", "graphics"), no "Loomy" needed. "Never mind", "cancel" or "go back" drops the question and leaves the PC alone (you may have picked there). It's gone after 20 seconds or any other command.
- Loomy's answer to the phone for an open list is now that question ("Which one (si)? general, cpu, gpu 1.").

## v2.22b
- **Pocket Loomy updates itself**: Settings → Updates → **Check for updates**. It asks GitHub for the newest release, only trusts it if you published it and GitHub's build made the app, checks the file against GitHub's checksum, and hands it to Android, which only installs it if it's signed with your key. The first time, Android asks you to allow Pocket Loomy to install apps. In the dev app, the same button installs or updates the release app.
- **Listening is fast again**: the phone hears you by default (like Bluetooth mode), and when it hears something its set phrases don't know, Ellune's AI works out the command. If one of the phone's guesses is a real command ("display extend" next to "display extent"), that one wins without asking her.
- **"Ellune hears you"** (Whisper) is now a setting you tick, off by default. When it's on, it notices you've stopped talking sooner (0.7 s instead of 1 s) and an engine or a fan outside no longer keeps it listening for 10 seconds.

## v2.22
- **Pocket Loomy, redesigned.** A big mic in the middle with Loomy's dots rippling out of it (they move with your voice), the latest thing said at the top (tap it for the whole history), and everything else behind the gear: your PC and the On switch, Listening, Ellune and the app settings.
- **You can see what it's doing**: the mic breathes while it waits for "Loomy", turns purple when it's taking your command, shows a spinning ring while Ellune works it out (the mic is off then), and flashes green or red when the answer comes back. A faint grid sits behind it all.
- **Remote**: your phone reaches your PC through Ellune, from anywhere with internet. The PC connects out to her (no open ports) and she only passes on the same end-to-end encrypted messages as Bluetooth; she can't read or fake them. Pairing still happens in person, over Bluetooth.
- **Power button**: top left turns Pocket Loomy off; while it's off, a beating power button sits where the mic is (tap it to turn on).
- **The mic in Ellune mode**: tap it any time to send what you've said right away (it keeps listening after); hold it until the ring fills to mute.
- **Setup follows the mode**: purple = **Scan the QR**; blue = choose your PC and say or **type** the code.
- **Pair by QR, no Bluetooth**: `/remote` shows a QR under the pairing code. Scan it with your phone's camera, Pocket Loomy opens and pairs through Ellune (after the Authy code if the phone is new to her), and you press Enter on the PC like before. Bluetooth pairing still works behind Allow Bluetooth. Pairing still lasts 7 days.
- **Add devices with Authy**: a new phone (Add to guest list) or PC (`/remote add` and the code) puts itself on Ellune's guest list with the 6-digit code from your Authy app. No more copying lines into Cloudflare. **Devices on Ellune** in the app shows who's on it and removes one (with a code too).
- **`/phone` is now `/remote`**, with **Allow Bluetooth** (B) and **Allow remote** (R) to choose what works, and C to copy this PC's line for Ellune's guest list.
- **Online first.** Pocket Loomy now goes through Ellune by default and doesn't touch Bluetooth at all. The **Bluetooth button** over the mic switches to Bluetooth mode (the app turns blue): then it's Bluetooth only and Ellune is left alone. Only you switch it.
- **The bar on top shows the connection**: Offline, Ellune offline, PC offline, Online, or Online BT.
- **Solo ignores typing and noises**: a sound has to actually be a voice before it's sent anywhere, and things that aren't commands are dropped quietly.
- **Background grid strength** is a slider under Visuals in the settings.
- **"Loomy" is heard better**: "lumi", "lumie", "loomino", "lou me" and other ways the phone writes her name now count.
- **Two ways to listen**, picked right under the mic: **Say "Loomy"** (everything after her name is the command; tapping the mic counts as saying it) or **Solo** (everything you say is a command). In both, a pause or "thank you" ends the sentence; "please" can go anywhere.
- **No more silent switching:** if Ellune doesn't answer, Pocket Loomy says so and nothing is sent. Want only the phone's recognizer? Tick **Use the phone's recognizer**. With no internet it's the only option, and it tells you.

## v2.21
- **Ellune's guest list.** Ellune now only serves devices on her list. Pocket Loomy has its own Ellune key (kept in the phone's secure Keystore, it can't be copied out): **Add to guest list** copies the line you paste into Ellune's settings, and **Ping Ellune** now also says whether the phone is on the list. It's the groundwork for Ellune understanding your voice.
- **Ellune hears you** (Pocket Loomy, setting **Understand me through Ellune**, on by default). With internet and the phone on her guest list, a Talk tap, "Loomy" + a pause, and solo mode record what you say and Ellune turns it into text with Whisper (much better with accents). Waiting for "Loomy" still happens on the phone, so what isn't said to Loomy never leaves it.
- **Say it your way:** when the phone's phrase list doesn't know what you said ("make the screens go dark"), Ellune's AI picks the Loomy command, and the history shows *what you said → what was sent*.
- Offline or if Ellune doesn't answer, the phone's own recognizer takes over (set phrases only) and the screen says so.

## v2.20
- **Meet Ellune**, Loomy's online sister: a small server (`ellune/`, a Cloudflare Worker) that future online features will go through.
- **`/ellune`** (core mode) pings it and says whether it's up and how long the round trip took (that includes setting up the secure connection). A wifi login page or anything else answering doesn't count as up.
- **Pocket Loomy can ping Ellune too:** Ellune box, **Ping Ellune**, same answer as on the PC. That's the app's first use of the internet (a new permission), only when you tap it. Commands to the PC still only go over Bluetooth.

## v2.19
- **Pairing asks you first.** After a phone says the right code, Loomy shows its name and an ID like `K7P2-9QXD`, and the phone shows the same ID. It's only trusted once you press Enter on the PC; Esc, or a minute with no answer, says no. Someone who catches the code still can't pair without you seeing it.
- `/phone`'s list of what happened is now called **Logs**.
- Under the hood: the phone connection is split from the security on both sides (the PC and the app), so another way of connecting can be added later without touching the encryption.

## v2.18
- **Pocket Loomy for Android** (first release of the app). Pair by saying the code, then tap Talk or type to run Loomy commands from your phone. The screen follows what's happening: setup steps until paired, then the live transcript, what was sent, and Loomy's answer.
- **Talk like a person.** "Loomy, can you turn off the screen please" works ("yo/ayo Loomy" too). So do "show screen", "open system info and go to the next tab", "slash packages" and "recycle bin". Join commands with "and".
- **Keep listening** (setting): after Talk it keeps listening until you stop it, and only acts on what starts with "Loomy ...". Anything else it hears is dropped on the phone, never sent. If the phone cuts you off right after "Loomy", the next thing you say is taken as the command.
- **Solo mode** (setting, or "Loomy solo mode"): for when you're alone. Say "Loomy" once, then just talk: every sentence is a command until "ok thank you" / "stop listening", then she waits for her name again. "Solo mode off" ends it.
- **A phrase library** (`pocket/.../Phrases.kt`): lots of everyday ways to say things, like "go back one tab", "next page", "mirror the screens", "how's my PC doing", "open gmail" (a saved site), "go away" / "come back" (hide / show).
- **From the phone you can run any normal command now**, including `help` (it gets a short list back). Use `input back` / `input next tab` / `input down 2` / `input enter` for Loomy's open screen. A new command closes the open screen first, and screens like `si`, `display` and `web` tell the phone what you can pick. Slash commands still only run on the PC.
- **Input mode:** say "Loomy input mode", then plain "next", "next tab", "back", "ok", "cancel", "down three" work on their own until "input mode off", several in one go too ("up up right confirm"). Anything else said meanwhile is still ignored. ("Loomy down" works any time.)
- "Loomy off" / "Loomy on" hide and show Loomy. "Loomy, stop the connection" turns Pocket Loomy off. "Ok turn off the mic" / "turn the mic off" / "Loomy stop listening" turns the mic off until you tap Talk again.
- **`display on`** wakes the screens again after `display off` (handy from the phone).
- **One notification.** On/off and Talk live there, plus a "Loomy" quick-settings tile. While it's on, the phone checks in with the PC every 5 minutes (`/phone` shows last seen), and it turns itself off after 15 minutes without reaching the PC.
- Settings in the app: notification on/off, keep listening, the Talk card, and a **net speed meter** (your phone's down/up speed in the status bar, works with Pocket Loomy off, paused while the screen is off, under 1 KB/s shows as 0).
- An app icon: a rounded L with Loomy's halftone dots (also as a themed icon on Android 13+).
- **Every phone attempt shows in Loomy**, including commands it refused and things the phone didn't catch.
- **Forget on the phone makes Loomy forget it too.**
- Protocol v2: the phone and Loomy must both be 2.18+. An older one says to update instead of failing a check.

## v2.17
- **Pocket Loomy, PC side**: an Android phone can send Loomy commands over Bluetooth. Opt-in: `/pkg add phone` (off by default, nothing listens until then).
- `/phone` shows an 8-digit pairing code. Say it on the phone and it's trusted for 7 days. A code lives 2 minutes and is used up by its first try; N makes a new one. `/phone forget` (or F twice) drops the phone.
- From the phone: `open`, `hide`, `web`, `rb`, `display`/`dp`, `sysinfo`/`si`. Slash commands never run from the phone. Answers go back to it.
- Security: CPace pairing on the code (no offline guessing), then fresh keys every connection and every message encrypted and checked. Any failed check, bad message or unknown phone shuts listening down, prints who tried and shows a tray note; Enter in `/phone` turns it back on.
- Loomy only listens while a phone is trusted or a code is open, only for phones paired in Windows' Bluetooth settings, and costs nothing while it waits.

## v2.16
- **`/pkg`: pick your features per PC**, like extensions but nothing is downloaded. `/pkg` opens a list to switch features on and off; `/pkg add <feature>` and `/pkg remove <feature>` work too, with Tab completion.
- Optional features: Websites (`web` + `/webconfig`), Loomy's Path (`lp`), Network (`net`), Recycle Bin (`rb`), Display (`display`), System Info (`sysinfo`), GitHub (`github`). An off feature's commands act like they don't exist and its code never runs. Saved sites are kept when Websites is off.
- The core (help, modes, `/config`, updates, `/pkg` itself, hide, reload...) is always on. `help` tells you when some features are off.

## v2.15
- The window fade-in on open is slower and smoother, so you can actually see it (it used to be over in ~30 ms).
- New **Fade speed** slider under it in `/config` → Customization: 100–800 ms, ← → or click the bar.
- Double-click the clock in the top right to hide Loomy, for when only your mouse hand is free.

## v2.14
- Loomy comes alive a little, all optional: a slow wave drifts through the halftone dots, a glow sweeps along the purple lines when Loomy opens, the list highlight glides between rows, and new output lines fade in. A window fade-in on open is there too, off by default.
- **Animations: auto / on / off.** Auto skips them when your PC is busy as Loomy opens (CPU over 80 %, RAM over 90 %, or battery saver). Nothing runs while Loomy is hidden, and the dot wave only repaints its own thin strip.
- `/config` now has sections: **General** (hotkey, name, start with Windows, remember position) and **Customization** (accent color and every animation). Tab switches; `/config custom` opens Customization directly.

## v2.13b
- When a list or panel is open (web, display, sysinfo, the mode picker, `lp`, `net`, `/verc`...), the input bar hides, since you can't type there anyway. It comes back when you close the panel, and shows while you're editing your name or accent color in `/config`.
- Switching modes (and `/reload`) brings Loomy back in front with the input focused, instead of opening behind your other windows so your typing went to the app underneath.

## v2.13
- New `sysinfo` (`si`): live CPU, GPU, memory, disk, network and top apps. Bare `sysinfo` opens a picker; `si general`, `si cpu` and `si gpu [n]` go straight to a view. Loomy grows to about 1.5x for it (less if your screen is smaller) and goes back to normal size when you close it.
- Every GPU Windows lists gets its own card and view, so a laptop with integrated + dedicated graphics shows both (dedicated first).
- New **Flow** score per CPU and GPU: high usage alone doesn't lower it; waiting threads, heat or power limits, full RAM or VRAM, paging and driver time do, and it says which.
- Sensors are only read while sysinfo is open, once a second. Nothing runs while it's closed.

## v2.12
- New `github` (`gh`) in core mode: opens Loomy's GitHub page.
- New `dp off` and a "Screens off" row in the `display` picker: puts the screens to standby like their power button, so the layout doesn't change. Any key or mouse move wakes them.

## v2.11
- `/version`, `/update` and `/vh` give up on a dead or slow connection within seconds, instead of freezing Loomy for up to a minute.
- "Checking GitHub..." now actually shows up before the check starts.
- `/vh` can show the full history again (it used to cut off around 15 releases).
- New **core** mode for Loomy's own upkeep. `/update`, `/version`, `/vh` and `/verc` only exist there; in home or work they're unknown commands, aren't in `help`, and Tab won't offer them.
- Hiding the mode you're in never drops you into core if home or work is still visible.
- After `/update`, Loomy comes back in the mode you updated from.
- New `display` (`dp`): switch your screens like Win+P. Bare `display` opens a picker (Extend first, Second screen only second); `display extend|second|pc|duplicate` switches straight away. Loomy hides after switching.

## v2.10
- `/update` only installs a release that **you** published and whose `loomy.exe` was uploaded by **your GitHub Actions build**. Anything else (like a release or exe from another account) is refused, and `/version` warns about it.
- Fix builds with a letter are understood: 2.9 < 2.9b < 2.9c < 2.10.
- Fixed `/update` and `/version` freezing when the latest release had a letter (v2.9b).
- `/update` downloads the exact release it checked instead of whatever is "latest" a moment later.
- Repo cleanup: removed stray copies of source files from the repo root, and `loomy.exe` is no longer committed (it lives in Releases).

## v2.9
- Switching modes now reloads Loomy (clean chat, nothing stuck). Any note, like "Hid work.", carries over to the fresh window.
- `/reload` keeps you in your current mode. A fresh start still begins in the main mode.
- New setting in `/config`: **Remember position**. When it's on, Loomy shows up where you last hid it. A reload (and so a mode switch) re-centers it.
- New `/resetwindow`: moves Loomy back to the middle and forgets the remembered spot.
- New `web many`: the sites list stays open after you pick one, so you can open several. Opened rows get marked, and Loomy grabs focus back from the browser.

## v2.8
- Modes: `home` and `work`. `/mode` (or `/m`) opens a picker; `/m switch|hide|show|main <mode>` does the rest. At least one mode always stays visible, and Loomy starts in the main one.
- The header shows the current mode next to the version.
- New `web`: open a saved site by alias (`web yt`), or type `web` for a list to pick from.
- New `/webconfig` (`/webc`): `add`, `remove`, `batch`, `info`, `rename`, `update` for saved sites. A batch opens several sites at once; ending it with `false` folds the singles into it.
- Each mode keeps its own saved sites, so the same alias can open different things at home and at work.
- Input is strict now: extra or missing words get a one-line usage reply instead of being ignored.
- Tab completes mode names, site aliases, and subcommands.

## v2.7
- Show the version number next to the title in the header.
- Ctrl+Up / Ctrl+Down now scroll the chat (plain Up/Down stays command history).
- New `/version` (`/ver`) command: shows your version and checks GitHub for a newer one.
- New `/vh` (`/versionhistory`) command: lists every version with its release date and when you updated locally.
- New `/verc` command: a panel to browse the changelog for each version.

## v2.6
- Commands that change Loomy itself now need a `/` prefix (`/config`, `/update`, `/reload`, `/shutdown`, `/hide`, `/clear`).
- Commands that do a task or just print stay plain (`lp`, `net`, `rb`, `exe`, `help`).
- Typing a command with the wrong prefix now nudges you to the right one.
- Removed the command table from the README — type `help` to see commands.

## v2.5
- New `/update` command: checks GitHub, downloads the newest build, swaps itself, and relaunches.
- Set up automatic builds and releases on GitHub (push a version tag, the exe builds itself).
- Added an MIT license and a one-time setup guide.

## v2.4
- The `net` list now groups by app, so one row per app with its processes totalled.
- Ending a task closes the whole app (every process sharing the name, plus their children).
- The confirm shows how many processes it will close.

## v2.3
- New `net` command: live view of what's using your internet, with per-app download/upload speeds.
- Ends a task on Delete (with a confirm), elevating to admin only when a protected process needs it.

## v2.2
- `lp` now opens a folder browser: arrow keys to move, Enter to open, left/right to go in and out.
- Loomy comes back with a clean screen each time it hides.

## v2.1
- Slimmed down to the essentials and gave each command a single clear name.
- `config` is now an in-app settings screen (hotkey, name, accent color, start with Windows).
- `lp` opens your folders, `rb` the recycle bin, `exe` shows where Loomy lives.

## v2.0
- Rebuilt from the old batch script and WPF app into a single tiny C program, no runtime needed.
- Summon from anywhere with a global hotkey, with a tray icon and command console.
