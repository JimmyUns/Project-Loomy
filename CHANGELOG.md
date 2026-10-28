# Changelog

All notable changes to Loomy, newest first.

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
