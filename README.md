# Loomy 2.5
A summon-anywhere console for Windows, written in plain C with no runtime.

## Use it
Run `loomy.exe`. Press **Ctrl + Alt + Space** (changeable in `config`) to show/hide it. **Esc** hides it too.

| Command | What it does | Type |
|---|---|---|
| Help | show this list | `help` |
| Config | open settings (hotkey, name, color, start with Windows) | `config` |
| Loomy's Path | browse your folders (`lp <name>` makes and opens one) | `lp` |
| Network | see what's using your internet (speeds, end a task) | `net` |
| Recycle Bin | open the recycle bin | `rb` |
| Exe Location | show where loomy.exe lives | `exe` |
| Clear | clear the screen | `clear` |
| Hide | hide Loomy | `hide` |
| Update | download + install the newest version from GitHub | `update` |
| Reload | restart Loomy fresh | `reload` |
| Shutdown | close Loomy completely | `shutdown` |

In `lp` and `config`: ↑↓ choose, Enter picks, Esc goes back. In `lp`, → goes inside a folder and ← goes back out.

Every time Loomy hides, it comes back with a clean screen.

Keys: **Tab** completes (also folder names after `lp`), **Up/Down** history, **Ctrl+L** clears, **Ctrl+Backspace** deletes a word, **PgUp/PgDn** or the mouse wheel scrolls.

Settings and your folders live in `%APPDATA%\Loomy`.

## Build it
Windows: install [w64devkit](https://github.com/skeeto/w64devkit), put its `bin` folder on PATH, run `build.bat`.
Linux: install `mingw-w64`, run `./build.sh`.

## Add a command
Open `src/commands.c`, write a function, add one row to `COMMANDS` (name, what it does, what you type, function). `help` and Tab completion pick it up on their own.
