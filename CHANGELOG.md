# Changelog

All notable changes to Loomy, newest first.

## v2.18
- **Pocket Loomy for Android** (first release of the app): pair by saying the code, then tap Talk (or type) to run Loomy commands from your phone. The screen follows what's happening: setup steps until paired, then the live transcript while it listens, what it sent, and Loomy's answer.
- **On/off from your notifications and a "Loomy" quick-settings tile.** While on, the notification has a Talk button (a small card pops up, listens, shows the answer) and the phone checks in with the PC every 5 minutes, so `/phone` shows when it was last seen. It turns itself off after 15 minutes without reaching the PC.
- **Every phone attempt shows in Loomy now**, including commands it refused and things the phone didn't catch ("didn't catch a command (heard ...)").
- **Forget on the phone tells the PC**, which forgets the phone right away.
- Saying "slash" works ("slash packages" becomes `/pkg`), and spoken names like "recycle bin" and "system info" map to `rb` and `si`. Slash commands still only run on the PC.
- Protocol v2 (the phone and Loomy must both be 2.18+; an older one says to update instead of failing a check).

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
