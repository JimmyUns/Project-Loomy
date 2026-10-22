# Loomy — Handover Document

This is a complete brief for continuing the Loomy project in a fresh chat with no prior
context. Read it top to bottom before making changes. Attach `Loomy.zip` (the full source)
to the new chat so the assistant has the actual code.

---

## 1. What Loomy is

Loomy is a personal "command console" for Windows — a summon-anywhere overlay (like a game
dev console or Spotlight/Alfred) that the user, **Jimmy Younes**, built for himself. Press a
global hotkey, a dark window drops in, type short commands to open folders, see network usage,
change settings, etc. Press Esc or the hotkey to dismiss.

It started as a Windows .bat file, became a C#/WPF app, and is now a **single tiny C program
with no runtime dependency**. The whole point is to be **as lightweight as possible** (small
exe, tiny idle RAM) because the long-term vision is a "JARVIS-like" console the user grows over
time — eventually voice input/output and optional AI, but always staying light and local-first.

**Design values that matter to the user (keep these in mind):**
- Lightweight above almost everything. No runtime, no bloat, small exe.
- Local-first. Things work offline where possible.
- Clean, distinctive look. Deep plum/purple theme, a halftone dot strip under the header
  (an homage to the old logo's dither).
- The user is technically capable, learning as he goes, and wants honest tradeoffs — not
  cheerleading. Tell him when something can't be tested or when an approach is weaker.

---

## 2. Current state (v2.9)

Fully working, deployed through the user's own GitHub auto-update pipeline (see §6). The user
runs it on his **personal PC and his work laptop**. (Earlier the work laptop showed the blue
SmartScreen prompt; he clicked Run and IT had no issue. He has no admin there. Still: never help
him get around workplace security.)

### Commands (type `help` in Loomy to list them)

Two classes, separated by a `/` prefix:
- **Plain** (a task or a print — no slash): `help`, `lp`, `web`, `net`, `rb`, `exe`
- **Slash** (acts on Loomy itself — needs `/`): `/mode` (`/m`), `/webconfig` (`/webc`), `/config`,
  `/clear`, `/hide`, `/reload`, `/resetwindow`, `/update`, `/version` (`/ver`), `/vh`, `/verc`, `/shutdown`

Typing the wrong prefix nudges the user to the right one. The rule: **slash = changes Loomy,
plain = Loomy does something for you or just prints.**

| Command | Prefix | What it does |
|---|---|---|
| `help` | none | lists commands (shows each one's correct typed form) |
| `web` | none | saved sites for the current mode. `web` = list panel (Enter opens, Loomy hides); `web many` = same list but it stays open and marks what you opened; `web <alias>` opens it (a batch opens all its sites) |
| `/resetwindow` | `/` | re-centers the window and forgets the remembered position |
| `lp` | none | folder browser over `%APPDATA%\Loomy\Folders` (arrows, Enter opens, →/← in/out). `lp <name>` makes+opens a folder |
| `net` | none | live list of apps using the internet, grouped per app, with ↓/↑ speeds; Delete ends the whole app (confirm first); refreshes every 5s, R = refresh now |
| `rb` | none | opens the recycle bin |
| `exe` | none | opens the folder with loomy.exe highlighted |
| `/mode` `/m` | `/` | modes panel (Enter switches). `/m switch\|hide\|show\|main <mode>` |
| `/webconfig` `/webc` | `/` | `add <alias> <site>`, `remove <alias>`, `batch <name> <alias...> [true\|false]`, `info <alias>`, `rename <alias> <new>`, `update <alias> <site>` |
| `/config` | `/` | in-app settings panel: hotkey (press-to-capture), name, accent color, start-with-Windows, remember position |
| `/clear` | `/` | clears the chat |
| `/hide` | `/` | hides Loomy (also Esc) |
| `/reload` | `/` | fully restarts Loomy |
| `/update` | `/` | checks GitHub, downloads newest build, swaps itself, relaunches |
| `/version` `/ver` | `/` | shows current version + checks GitHub if it's latest |
| `/vh` | `/` | lists all versions with GitHub release date + local update date |
| `/verc` | `/` | changelog panel: list of versions, Enter opens one's changes. `/verc v2.6` jumps straight in |
| `/shutdown` | `/` | closes Loomy completely |

### Modes (v2.8)
- Hardcoded list: `home`, `work` (`NAMES[]` in `modes.c`). The user does NOT want a way to create
  modes from inside Loomy.
- Saved in `loomy.cfg`: `mode_main` (Loomy starts in it) and `mode_hidden` (comma list). The
  current mode is memory-only, so a fresh start goes to main.
- **Switching modes = a full reload (v2.9, the user asked for this).** `ui_restart_to(mode, note)`
  starts a new process with `--restarted --mode <name> --note <text>`. `--note` always comes
  last, and `|` in it means a new line. The new process starts in that mode and prints the note
  (e.g. "Hid work.|It was the main mode...|Switched to home mode."). A plain `/reload` passes the
  current mode with no note, so it keeps you in your mode. The `/update` relaunch passes no mode,
  so it starts in main.
- Rules: at least one mode stays visible. Hiding the current mode moves you to the next visible
  one; hiding the main mode moves main too. `show` never switches you. You can't switch to, or
  make main, a hidden mode.
- Each command has a `modes` mask. **Right now every command is `MODE_ALL`**; the user said no
  locking yet. The lock is already built (help/Tab hide it, typing it says which mode it's
  in), so locking a command later just means changing its mask.
- What the modes will differ in is still to be defined by the user. So far the only difference
  is that **saved sites are per mode** (`web-home.txt` / `web-work.txt`), so the same alias can
  open different sites at home and at work.

### Saved sites (`web`, v2.8)
- Stored in `%APPDATA%\Loomy\web-<mode>.txt`, one tab-separated line per entry
  (`site|batch, alias, created, changed, url...`). Reloaded on every use, so hand edits and mode
  switches just work.
- `youtube.com` gets `https://` added. Aliases are case-insensitive and can't be `true`/`false`.
- **Batches copy URLs** at creation. `false` removes the singles (only the batch keeps them),
  and removing the batch removes them for good. Updating a single later doesn't change a batch.
  Batches inside batches flatten, and duplicates are dropped. `update` on a batch is refused.
- Limits: 128 entries, 256 URLs total, 32 per batch, 512 chars per URL.

### Strict input (v2.8)
Every `COMMANDS[]` row declares `usage` + `maxargs`. Extra words now give a one-line
`Usage: <typed> <shape>` (`<x>` = required, `[x]` = optional, defaults written inline) instead
of being ignored. Commands with subcommands (`/mode`, `/webconfig`) use `ARGS_SELF` and print the
usage for the specific subcommand. `lp` is `ARGS_TEXT` (folder names can have spaces).

### Window position (v2.9)
- `remember_position = on|off` in `loomy.cfg`, toggled in `/config`. Off by default.
- The spot is captured in `ui_hide()` and kept **in memory only**. A reload (and therefore a mode
  switch) re-centers, which the user asked for. `/resetwindow` forgets it and re-centers now.
- `pos_ok()` refuses a spot whose header isn't on a connected monitor (unplugged screen),
  falling back to center.

### UI behaviors
- Global hotkey (default **Ctrl+Alt+Space**), set in `/config`. Single-instance (second launch
  pops the first).
- Hides to a tray icon; tray right-click menu: Open, Settings, Reload, Shutdown.
- **Clears the chat each time it hides** (comes back fresh).
- Command history with Up/Down. **Ctrl+Up/Ctrl+Down scrolls the chat** (also PgUp/PgDn, wheel).
- Tab completion for commands and `lp` folder names.
- Header shows "Loomy v2.9 · <mode>" (the mode is in the accent color).
- Panels (lp / web / mode / net / config / verc) capture the arrow keys for their own navigation, so
  Ctrl+scroll and history are correctly inactive inside them.

---

## 3. How it's built (environment)

The assistant works on **Linux** and cross-compiles to Windows. **It cannot run the exe** — no
real Windows. Testing options, in order of usefulness:
- **Unit-test logic in Linux.** `tests/run.sh` builds the real `modes.c`, `web.c`, `commands.c`,
  `config.c` and `util.c` against a fake `windows.h` (`tests/shim/`) and runs ~70 checks:
  strict args, modes, web/webconfig including the files on disk, Tab completion, and panel keys.
  It needs plain `gcc`. **Run it after touching any of those files, and extend it for new
  commands.** (Gotcha: on Linux the `\` in paths stays in the filename, so files land in `/tmp`
  as `loomytest\web-home.txt`.) Version-compare and changelog parsing were tested earlier with
  one-off harnesses.
- **Wine + Xvfb + openbox** for screenshots. **Unreliable** — it frequently fails to surface
  the window or kills the X server. Don't sink lots of time into it. When it works:
  ```
  pkill -9 Xvfb wine wineserver openbox; rm -f /tmp/.X99-lock
  Xvfb :99 -screen 0 1280x800x24 & sleep 4
  openbox & sleep 2
  WINEPREFIX=/tmp/wp DISPLAY=:99 wine ./loomy.exe & sleep 9
  # find window, windowactivate --sync, xdotool type, import -window root
  ```
  There's a demo build path (`-DLOOMY_NET_DEMO`) that injects fake `net` data so the net UI can
  be screenshotted without real connections.
- **The user is the real tester.** Each change ships and he reports back. Be upfront about what
  was and wasn't verified.

### Build
- Linux: `./build.sh` (needs `mingw-w64`). Produces `loomy.exe`.
- Windows: `build.bat` (needs w64devkit or similar on PATH).
- Flags: freestanding, no C runtime (`-nostdlib -nostartfiles -e entry`), `-Os`, gc-sections.
  That's why the exe is ~98 KB with no dependencies (77 KB at v2.7; v2.8's modes/web/strict
  args added ~20 KB, mostly UTF-16 strings and the two panels).
- **No C runtime** means some libc functions (`memset`, `memcpy`) are hand-defined in `util.c`,
  and string helpers are custom (`s_cpy`, `s_cat`, `s_ieq`, `s_istarts`, etc. — use these, not
  libc). Keep new code in this style.
- When adding a `.c` file, add it to **both** `build.sh` and `build.bat`, and add any new
  `-l<lib>` to both.

---

## 4. Source layout

All under `src/`. ~4000 lines total. `tests/` holds the Linux test harness (not built by CI).

- **`loomy.h`** — shared header: `LOOMY_VERSION`, colors (C_BG, C_ACCENT via g_accent, etc.),
  the `out()`/`outf()` print API, and every module's function decls. Start here.
- **`util.c`** — the hand-rolled libc + string helpers.
- **`config.c`** — loads/saves `%APPDATA%\Loomy\loomy.cfg` (hotkey, name, accent). Plain text.
- **`ui.c`** (biggest, ~660 lines) — the window, painting, the input box, hotkey, tray, entry
  point (`entry`), and the **screen router**. Panels plug in here: each panel exposes
  `_active/_open/_close/_paint/_key/_char/_click/_wheel` and `ui.c` dispatches to whichever is
  active. Also handles `--kill`, `--silent`, `--restarted` command-line modes.
- **`commands.c`** — the command table (`COMMANDS[]`), the parser, strict-arg checks, and Tab
  completion. Each row: `{ name, about, type, alias, sys, modes, usage, maxargs, fn }`. `sys=1`
  needs the `/`. Adding a command = a `c_foo` wrapper + one table row; help, completion and
  usage replies pick it up. Argument completion for a command goes in `complete_args()`.
  `cmd_usage(shape)` / `cmd_typed()` are for commands that check their own args.
- **`modes.c`** — modes list, `/mode` command, the mode picker panel.
- **`web.c`** — saved sites: per-mode file load/save, `web`, `/webconfig`, the sites panel.
- **`settings.c`** — the `/config` panel + start-with-Windows registry logic.
- **`browser.c`** — the `lp` folder browser panel.
- **`net.c`** (~630 lines) — the `net` panel. Enumerates TCP/UDP via IP Helper
  (`GetExtendedTcpTable`), groups processes by name into apps, reads per-process I/O counters
  for speed, kills whole app trees (with one-time UAC elevation via a `--kill` helper process).
- **`update.c`** — `/update`, `/version`, `/vh`. WinHTTP calls to the GitHub API. Self-replace
  via a generated `.bat` that waits for exit, swaps the exe, relaunches. Also the local update
  log (`updates.log`) feeding the `/vh` "you updated" column.
- **`changelog.c`** — the `/verc` panel. Changelog entries are **baked in** (static array
  `RELEASES[]`) so it's instant and offline. Mirror of `CHANGELOG.md`.

Other files: `res/` (icon built from `loomy_logo.png` by `make_icon.py`, manifest, .rc),
`.github/workflows/release.yml` (the CI), `README.md`, `SETUP.md`, `CHANGELOG.md`, `LICENSE` (MIT).

---

## 5. Conventions to follow

- **Keep it tiny and dependency-free.** No new runtime deps. Prefer Win32 APIs loaded
  dynamically where reasonable. Match the no-libc style.
- **Panels** (anything more than a line of output) follow the browser/net pattern: a module
  with the 8 `_active/_open/.../_wheel` functions, wired into `ui.c`'s router. Don't dump long
  content into the chat — use a panel with up/down + Enter.
- **Commands**: one `c_foo` wrapper, one `COMMANDS[]` row. Set `sys=1` if it changes Loomy.
- **When adding a version**: bump `LOOMY_VERSION` in `loomy.h`, add an entry to **both**
  `CHANGELOG.md` and the `RELEASES[]` array in `changelog.c` (keep them in sync), and tag the
  GitHub release to match (see §6).
- **Typography**: real apostrophes/quotes in user-facing text where appropriate; the code uses
  `\x2191` etc. for arrows. Accent color is themeable (`g_accent`) — don't hardcode purple for
  new UI, use the color tokens.
- **Deliverables**: build `loomy.exe`, put it + updated source in `Loomy.zip`, present both.
  The user deploys via GitHub.

---

## 6. The GitHub auto-update pipeline (IMPORTANT — it's live and working)

The user has a public repo: **github.com/JimmyUns/Project-Loomy**. It's wired so:

1. Source lives in the repo.
2. A GitHub Actions workflow (`.github/workflows/release.yml`) builds `loomy.exe` on GitHub's
   servers whenever a **version tag** (`v*`) is pushed/published, and attaches it to a Release.
3. `/update` in Loomy hits the GitHub API, compares the release tag to `LOOMY_VERSION`,
   downloads the new exe, and self-swaps.

**`GH_USER` and `GH_REPO` are already set in `update.c`** to `JimmyUns` / `Project-Loomy`.

**Deploy loop (the user does this):**
1. Upload changed `src/` files (and `CHANGELOG.md`) to the repo — he usually uses the GitHub
   web UI (drag-drop into the `src` folder to overwrite). He found browser upload easier than
   git CLI. Watch out: dragging the *folder* nests it; he uploads the *contents*.
2. Create a Release with tag `vX.Y` **matching `LOOMY_VERSION`**. (Mismatch = `/update` won't
   fire correctly.)
3. Actions builds it (~35s), attaches the exe.
4. He types `/update` in his running Loomy → it pulls the new version.

**This whole loop is tested and confirmed working end-to-end** (he updated 2.5→2.6→... live).

Notes:
- The assistant has **no GitHub connector** in-chat (checked repeatedly). It cannot push, open
  PRs, or touch the repo. It only produces files; the user moves them. Don't keep offering to
  do GitHub actions directly — you can't.
- There IS the separate `@claude` GitHub app (github.com/apps/claude) the user could install to
  get a "propose PR / click merge" flow, but it's a different Claude with no memory of context.
  For careful C work, the chat-based build-and-hand-off approach is better.
- Code signing / "unknown publisher": the blue SmartScreen warning is expected for an unsigned
  indie exe. On his personal PC it's a one-time "Run anyway." A real fix costs money (cert) and
  isn't worth it for a personal tool. VirusTotal does NOT whitelist anything — don't suggest it
  as a fix for warnings. Don't help bypass his employer's security.

---

## 7. What's verified vs. not, as of v2.9

- **v2.9, verified by `tests/run.sh`** (77 checks, 0 failures): switching reloads into the
  right mode with the right note, hide-current carries its messages through the reload, `show`
  and non-current `hide` don't reload, `/reload` keeps the mode, `/resetwindow` (plus strict
  args), `remember_position` save/load, `web many` (stays open, marks rows, asks for refocus,
  doesn't hide, `many` is a reserved alias, Tab offers it).
- **v2.9, NOT verified (needs his Windows):** the real restart handing over the hotkey (it
  reuses the same 20-retry path as `/reload`), the window actually reappearing at the
  remembered spot, and **whether Windows lets Loomy take focus back from the browser in
  `web many`**. It tries twice at 400 ms intervals; Windows only allows it if he hasn't clicked
  elsewhere since. If it doesn't stick, he'll have to click Loomy or press the hotkey (which
  refocuses a visible window).

### As of v2.8

- **v2.8, verified by `tests/run.sh`** (0 failures): all strict-arg cases, every `/m` rule
  (hide current/main, last-visible guard, hidden switch/main refused, cfg persistence and
  sanitizing), `web`/`/webconfig` end to end with real files (add, dup, reserved alias, batch
  keep and fold, atomic failure, nested flatten and dedupe, update, rename, remove), per-mode
  separation, Tab completion, and panel keys (A-Z order, Enter, letter jump, mode picker).
- **v2.8, NOT verified (needs his Windows):** how the two new panels and the header mode label
  *look*, real `ShellExecute` opening URLs in his default browser, and whether a batch's tabs
  land in one browser window. There's a 150 ms gap between opens to help with that; if the
  browser was closed, the first launch may still race.

### As of v2.7

- **Verified by unit test**: version comparison (handles 2.10 > 2.9), GitHub release-list
  parsing + ISO-date formatting, slash command routing (all prefix cases), net model grouping.
- **Verified live by the user**: the full `net` view (real data), the `/update` self-swap
  pipeline end to end.
- **Built clean but NOT visually tested this session** (Wine wouldn't cooperate): the v2.7
  additions — header version number, Ctrl+scroll, `/version`, `/vh`, and the `/verc` changelog
  panel. These are straightforward and follow proven patterns, but the user should eyeball them.
  If he reports a visual glitch, that's where to look first.
- **Never testable by the assistant** (needs real Windows): actual self-swap on disk, real UAC
  elevation, real per-app kill of multi-process apps, real network byte counters. All must be
  user-confirmed.

---

## 8. Likely next steps / ideas floated

- Confirm v2.8/v2.9 UI: panels, header, the settings row, remembered position, and the
  `web many` focus grab.
- The user will define what else differs between home and work (which commands get locked, etc.).
- The long-term vision: voice input/output (Windows SAPI is available, no big deps), optional
  AI, more system controls. Keep each addition light and panel-based.
- Possibly: show change-notes inside `/vh`, or richer changelog. Keep CHANGELOG.md and
  `changelog.c`'s `RELEASES[]` in sync whenever versions are cut.

---

## 9. Tone/working style the user likes

Straight-talking, concise, no fluff or hype. Give real tradeoffs and name limitations
(especially "I can't test this part — you'll need to"). He makes the final calls; offer options
with a clear recommendation rather than deciding silently. He's doing this for fun and to learn,
so a little teaching (why, not just what) lands well.
