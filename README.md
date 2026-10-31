# Loomy 2.18
A summon-anywhere console for Windows, written in plain C with no runtime.

## Use it
Run `loomy.exe`. Press **Ctrl + Alt + Space** (changeable in `/config`) to show/hide it. **Esc** hides it too, and so does **double-clicking the clock** in the top right (handy when only your mouse hand is free).

Type **help** to see every command. Commands that change Loomy itself (settings, update, reload, shutdown, hide, clear) start with a **/** — like `/config` or `/update`. Commands that just do a task or print are plain — like `lp`, `net`, `help`.

In `lp` and `config`: ↑↓ choose, Enter picks, Esc goes back. In `lp`, → goes inside a folder and ← goes back out.

Loomy has three modes, **home**, **work** and **core** — the current one shows in the header. Type `/m` to pick one (switching reloads Loomy fresh). **core** is for Loomy itself: `/update`, `/version`, `/vh`, `/verc` and `github` only exist there (anywhere else they're unknown commands). Saved websites (`web`, `/webconfig`) are kept separately per mode; `web many` keeps the list open so you can open several.

`/config` has two sections, **General** and **Customization** (Tab switches; `/config custom` jumps straight there). Turn on **Remember position** under General to have Loomy reappear where you last hid it. `/resetwindow` puts it back in the middle.

Customization also holds the accent color and the animations: a dot wave in the halftone strip, a glow along the lines when Loomy opens, a gliding list highlight, and fading-in text (all on), plus a window fade-in (off) with a **Fade speed** slider (← → to adjust, or click the bar). **Animations** is `auto` by default: if your PC is busy when Loomy opens (CPU, nearly full RAM, or battery saver), it skips them that time. Set it to `on` or `off` to decide yourself. They only run while Loomy is on screen.

If you type a command wrong (extra or missing words), Loomy replies with its usage: `<this>` is required, `[this]` is optional.

Every time Loomy hides, it comes back with a clean screen.

`display` (or `dp`) switches your screens like Win+P: pick from the list, or type `display extend`, `second`, `pc` or `duplicate`. `dp off` puts the screens to standby without changing the layout (any key or mouse move wakes them).

`sysinfo` (or `si`) shows what your PC is doing right now. Pick a view, or type `si general`, `si cpu` or `si gpu 2`, and Loomy grows to about 1.5x to show live graphs: CPU load, speed and per-thread bars; every GPU (laptops with two get both) with its load, engines, VRAM, temperature and clock; memory, disk, network and the top apps. Each CPU and GPU also gets a **Flow** score: it ignores plain busyness and only drops for things you'd feel, like threads stuck waiting, clocks held back by heat or power, RAM full and paging, drivers hogging the CPU, a hot GPU or full VRAM. Tab or the arrow keys switch views, Backspace goes back to the list, Esc closes. It only reads sensors while it's open.

**Pick your features per PC with `/pkg`.** Like extensions, but nothing gets downloaded: every feature ships inside Loomy, and `/pkg` turns them on or off on this PC. Type `/pkg` for a list (Enter, Space or a click toggles), or `/pkg remove sysinfo` / `/pkg add sysinfo`. An off feature's commands disappear (not in `help`, no Tab) and its code never runs. Optional features: `web` (with `/webconfig`), `lp`, `net`, `rb`, `display`, `sysinfo`, `github`, and `phone` (off until you add it). Everything else is Loomy's core and always on. Your choice is saved in `loomy.cfg` on that PC only.

**Pocket Loomy: commands from your phone (PC side).** `/pkg add phone`, then `/phone` shows an 8-digit code; say it in the Pocket Loomy Android app (`pocket/`, the APK comes with each release) and the phone is trusted for 7 days. After that you talk to it ("ayo Loomy, turn off the screen", "system info and next tab") and Loomy runs any normal command like you typed it, never a `/` command; `input back` / `input next tab` / `input enter` work its open screens. Pairing uses CPace (a password-authenticated key exchange), so a recorded pairing can't be brute-forced, and a code is used up by its first try. Every connection after that is encrypted and authenticated. Every attempt from the phone shows in Loomy, including ones it didn't understand. Any failed check shuts the listener down until you turn it back on in `/phone`, and Loomy tells you which device tried. With the feature off, nothing listens. Crypto: [Monocypher](https://monocypher.org) 4.0.2, vendored in `src/` (BSD-2 / CC0).

Keys: **Tab** completes (commands, folder names after `lp`, site aliases, modes), **Up/Down** history, **Ctrl+L** clears, **Ctrl+Backspace** deletes a word, **Ctrl+Up/Down**, **PgUp/PgDn**, or the mouse wheel scrolls the chat.

Settings and your folders live in `%APPDATA%\Loomy`.

## Build it
Windows: install [w64devkit](https://github.com/skeeto/w64devkit), put its `bin` folder on PATH, run `build.bat`.
Linux: install `mingw-w64`, run `./build.sh`.

## Add a command
Open `src/commands.c`, write a function, add one row to `COMMANDS` (name, what it does, bare word, alias, sys flag, modes, usage, max args, function — sys 1 means it needs the `/`). `help`, Tab completion and the usage replies pick it up on their own. Mark it `.phone = 1` if Pocket Loomy may run it from the phone (plain commands only). If it's optional, add a feature to the `FEAT_` list in `loomy.h` and `src/features.c`, and put that feature last in its row; leave it out and the command is core.

## Versions and updates
Tags look like `v2.10`. A letter means a fix build of the same version with no new features: `v2.9` → `v2.9b` → `v2.9c`. `LOOMY_VERSION` in `src/loomy.h` must match the tag exactly (minus the `v`).

Switch to core mode (`/m switch core`) and type `/update`. It only installs a release published by the repo owner whose `loomy.exe` was uploaded by the GitHub Actions build.

## Changelog
See `CHANGELOG.md`, or type `/verc` in Loomy to browse what changed in each version.
