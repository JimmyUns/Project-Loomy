# Loomy 2.10
A summon-anywhere console for Windows, written in plain C with no runtime.

## Use it
Run `loomy.exe`. Press **Ctrl + Alt + Space** (changeable in `/config`) to show/hide it. **Esc** hides it too.

Type **help** to see every command. Commands that change Loomy itself (settings, update, reload, shutdown, hide, clear) start with a **/** — like `/config` or `/update`. Commands that just do a task or print are plain — like `lp`, `net`, `help`.

In `lp` and `config`: ↑↓ choose, Enter picks, Esc goes back. In `lp`, → goes inside a folder and ← goes back out.

Loomy has three modes, **home**, **work** and **core** — the current one shows in the header. Type `/m` to pick one (switching reloads Loomy fresh). **core** is for Loomy itself: `/update`, `/version`, `/vh` and `/verc` only exist there (anywhere else they're unknown commands). Saved websites (`web`, `/webconfig`) are kept separately per mode; `web many` keeps the list open so you can open several.

Turn on **Remember position** in `/config` to have Loomy reappear where you last hid it. `/resetwindow` puts it back in the middle.

If you type a command wrong (extra or missing words), Loomy replies with its usage: `<this>` is required, `[this]` is optional.

Every time Loomy hides, it comes back with a clean screen.

`display` (or `dp`) switches your screens like Win+P: pick from the list, or type `display extend`, `second`, `pc` or `duplicate`.

Keys: **Tab** completes (commands, folder names after `lp`, site aliases, modes), **Up/Down** history, **Ctrl+L** clears, **Ctrl+Backspace** deletes a word, **Ctrl+Up/Down**, **PgUp/PgDn**, or the mouse wheel scrolls the chat.

Settings and your folders live in `%APPDATA%\Loomy`.

## Build it
Windows: install [w64devkit](https://github.com/skeeto/w64devkit), put its `bin` folder on PATH, run `build.bat`.
Linux: install `mingw-w64`, run `./build.sh`.

## Add a command
Open `src/commands.c`, write a function, add one row to `COMMANDS` (name, what it does, bare word, alias, sys flag, modes, usage, max args, function — sys 1 means it needs the `/`). `help`, Tab completion and the usage replies pick it up on their own.

## Versions and updates
Tags look like `v2.10`. A letter means a fix build of the same version with no new features: `v2.9` → `v2.9b` → `v2.9c`. `LOOMY_VERSION` in `src/loomy.h` must match the tag exactly (minus the `v`).

Switch to core mode (`/m switch core`) and type `/update`. It only installs a release published by the repo owner whose `loomy.exe` was uploaded by the GitHub Actions build.

## Changelog
See `CHANGELOG.md`, or type `/verc` in Loomy to browse what changed in each version.
