# Loomy 2.8
A summon-anywhere console for Windows, written in plain C with no runtime.

## Use it
Run `loomy.exe`. Press **Ctrl + Alt + Space** (changeable in `/config`) to show/hide it. **Esc** hides it too.

Type **help** to see every command. Commands that change Loomy itself (settings, update, reload, shutdown, hide, clear) start with a **/** — like `/config` or `/update`. Commands that just do a task or print are plain — like `lp`, `net`, `help`.

In `lp` and `config`: ↑↓ choose, Enter picks, Esc goes back. In `lp`, → goes inside a folder and ← goes back out.

Loomy has two modes, **home** and **work** — the current one shows in the header. Type `/m` to pick one. Saved websites (`web`, `/webconfig`) are kept separately per mode.

If you type a command wrong (extra or missing words), Loomy replies with its usage: `<this>` is required, `[this]` is optional.

Every time Loomy hides, it comes back with a clean screen.

Keys: **Tab** completes (commands, folder names after `lp`, site aliases, modes), **Up/Down** history, **Ctrl+L** clears, **Ctrl+Backspace** deletes a word, **Ctrl+Up/Down**, **PgUp/PgDn**, or the mouse wheel scrolls the chat.

Settings and your folders live in `%APPDATA%\Loomy`.

## Build it
Windows: install [w64devkit](https://github.com/skeeto/w64devkit), put its `bin` folder on PATH, run `build.bat`.
Linux: install `mingw-w64`, run `./build.sh`.

## Add a command
Open `src/commands.c`, write a function, add one row to `COMMANDS` (name, what it does, bare word, alias, sys flag, modes, usage, max args, function — sys 1 means it needs the `/`). `help`, Tab completion and the usage replies pick it up on their own.

## Changelog
See `CHANGELOG.md`, or type `/verc` in Loomy to browse what changed in each version.
