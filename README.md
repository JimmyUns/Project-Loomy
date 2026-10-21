# Loomy 2.6
A summon-anywhere console for Windows, written in plain C with no runtime.

## Use it
Run `loomy.exe`. Press **Ctrl + Alt + Space** (changeable in `/config`) to show/hide it. **Esc** hides it too.

Type **help** to see every command. Commands that change Loomy itself (settings, update, reload, shutdown, hide, clear) start with a **/** — like `/config` or `/update`. Commands that just do a task or print are plain — like `lp`, `net`, `help`.

In `lp` and `config`: ↑↓ choose, Enter picks, Esc goes back. In `lp`, → goes inside a folder and ← goes back out.

Every time Loomy hides, it comes back with a clean screen.

Keys: **Tab** completes (also folder names after `lp`), **Up/Down** history, **Ctrl+L** clears, **Ctrl+Backspace** deletes a word, **PgUp/PgDn** or the mouse wheel scrolls.

Settings and your folders live in `%APPDATA%\Loomy`.

## Build it
Windows: install [w64devkit](https://github.com/skeeto/w64devkit), put its `bin` folder on PATH, run `build.bat`.
Linux: install `mingw-w64`, run `./build.sh`.

## Add a command
Open `src/commands.c`, write a function, add one row to `COMMANDS` (name, what it does, bare word, sys flag, function — sys 1 means it needs the `/`). `help` and Tab completion pick it up on their own.
