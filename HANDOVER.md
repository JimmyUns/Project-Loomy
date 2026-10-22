# Loomy — Handover (v2.10)

Brief for a fresh chat. The code lives in the repo **github.com/JimmyUns/Project-Loomy**. Clone `main`
with the bot token (§2); don't ask for a zip. Read this first. Only open deeper files when the task needs them.

## 1. What Loomy is
A summon-anywhere command console for Windows (hotkey → dark overlay → short commands), built by the user,
**Jimmy Younes**. It's one tiny C program with no runtime (~104 KB exe). Values: **as light as possible**, local-first,
plum/purple theme with a halftone dot strip, accent themeable. Long-term: voice (SAPI) and optional AI, still light.
He runs it on his personal PC and his work laptop (no admin there; never help get around workplace security).

**How he likes to work:** straight-talking, concise, casual, no hype. Give real tradeoffs with a clear
recommendation; he makes the calls. Say plainly what you could and couldn't test. A little "why" helps him learn.
If something's unclear, ask (one question).

## 2. Workflow — the bot opens PRs, he merges and releases
- Project file `github-bot.txt` holds the **LoomyBot** token (classic PAT, only `public_repo`) and its expiry
  (2027-01-20). **Warn him when expiry is near.** Never print the token. Read it inside bash
  (`grep -o 'ghp_[A-Za-z0-9]*' /mnt/project/github-bot.txt`) and mask it in any output.
  Clone: `https://x-access-token:$TOKEN@github.com/JimmyUns/Project-Loomy.git`. Commit as `LoomyBot`.
- Loop: branch `claude/<topic>` → push → open a PR with *what changed / tested / he must eyeball* → he merges →
  he creates release tag `vX.Y` → Actions builds (~30 s) → he runs `/update`. Delete merged branches after.
- **Hard rules:** never push to `main`, never create tags or releases, never touch `.github/workflows`.
  GitHub enforces all of this, and it was live-tested: rulesets "protect main" (PR required) and
  "protect releases" (tag ruleset `refs/tags/v*`), plus the token has no `workflow` scope.
  He rejected any setup where a token could ship without him.
- Repo must stay **public** (`/update` reads releases without auth). The sandbox's unauthenticated API calls get
  rate-limited (shared IP), so always send the token.
- No GitHub connector exists in chat. His "GitHub Integration" is a read-only file picker.
- Also present the built `loomy.exe` in chat when he might want to test before merging.

## 3. Build & test
- **No C runtime**: `-nostdlib -nostartfiles -e entry -Os`. Use the helpers in `util.c` (`s_cpy`, `s_cat`, `s_ieq`,
  `s_istarts`, `s_words`, `s_count_words`, ...), not libc. `memset`/`memcpy` are defined there.
- `build.sh` (Linux/CI, compiles `src/*.c`) and `build.bat` (lists files explicitly — **add new .c files there**).
  Install the compiler in the sandbox: `apt-get install -y gcc-mingw-w64-x86-64` (run as root, no sudo).
- **`tests/run.sh`** builds the real `modes/web/commands/config/util/version.c` on Linux against a fake
  `windows.h` (`tests/shim/`): ~100 checks covering strict args, modes, web, Tab, panel keys, version
  ordering, and the release check (against `tests/fixtures/release_v2.9b.json` plus tampered copies).
  **Run it after any change and extend it for new features.** Gotcha: on Linux a `\` in paths stays
  in the filename (files land as `/tmp/loomytest\x.txt`).
- You **can't run the exe** (no Windows; Wine is unreliable, so don't bother). He's the real tester.

## 4. Source map (`src/`)
`loomy.h` (version, colors, all decls — start here) · `ui.c` (window, paint, hotkey, tray, entry, panel router,
reload/position) · `commands.c` (command table, strict args, Tab) · `modes.c` · `web.c` · `version.c`
(`ver_gt`, `release_check`) · `update.c` (WinHTTP, self-swap .bat) · `settings.c` (`/config`) ·
`browser.c` (`lp`) · `net.c` · `changelog.c` (`/verc`, baked-in `RELEASES[]`) · `config.c` · `util.c`.

**Conventions**
- **Commands:** one row in `COMMANDS[]`: `{name, about, type, alias, sys, modes, usage, maxargs, fn}`.
  `sys=1` means it needs `/` (it changes Loomy); plain means a task or print. `usage` like `L"[alias | many]"`.
  `maxargs`: a number, `ARGS_TEXT` (free text), or `ARGS_SELF` (it parses its own subcommands and calls
  `cmd_usage(shape)`). Arg completion goes in `complete_args()`.
- **Panels** (anything bigger than a line): a module with `_active/_open/_close/_paint/_key/_char/_click/_wheel`,
  wired into every dispatch spot in `ui.c` (screen_active, ui_hide, paint, edit_proc, click, dblclick, wheel).
- **New version:** bump `LOOMY_VERSION`, and add the entry to **both** `CHANGELOG.md` and `changelog.c` `RELEASES[]`
  (max 7 lines). The tag must equal the version exactly.
- **Versions:** a letter is a fix build (no new features): `2.9 < 2.9b < 2.9c < 2.10`. No default "a".
- UI text: real apostrophes, `\x2191`-style arrows, color tokens (`g_accent`, `C_DIM`...), never a hardcoded purple.

## 5. Current features
Plain: `help` · `lp [folder]` · `web [alias|many]` · `net` · `rb` · `exe`.
Slash: `/mode` `/m` · `/webconfig` `/webc` · `/config` · `/clear` · `/hide` · `/reload` · `/resetwindow` · `/update` ·
`/version` `/ver` · `/vh` · `/verc [version]` · `/shutdown`. Type `help` in Loomy for descriptions.

- **Strict input:** extra/missing words print one `Usage: <typed> <shape>` line (`<>` required, `[]` optional).
- **Modes** `home`/`work` (hardcoded; he doesn't want user-made modes). `/m switch|hide|show|main <mode>`.
  At least one stays visible; hiding the current/main mode moves them to the next visible one. `mode_main` and
  `mode_hidden` are saved in `loomy.cfg`; the current mode is memory-only. **Switching = full reload**
  (`ui_restart_to(mode, note)` → `--restarted --mode X --note a|b`); `/reload` keeps the mode. Every command
  has a `modes` mask; all are `MODE_ALL` for now (the lock is built but unused). The only real per-mode
  difference so far is saved sites. **He still has to define what else differs.**
- **web:** per-mode `%APPDATA%\Loomy\web-<mode>.txt` (tab-separated). `/webc add|remove|batch|info|rename <old> <new>|update`.
  Batches *copy* URLs; `batch ... false` folds the singles away. `true/false/many` are reserved aliases.
  `web many` keeps the panel open, marks opened rows, and asks for focus back (`ui_refocus_soon`).
- **Window:** `remember_position` (config, off by default) keeps the last hide position in memory only, so a
  reload/mode switch re-centers. `/resetwindow` re-centers now.
- **/update safety:** only trusts a release authored by `JimmyUns` whose `loomy.exe` was uploaded by
  `github-actions[bot]`, and downloads that exact tag. **Don't weaken it.** Possible upgrade: verify the
  asset `digest` (sha256).

## 6. Open items
- **v2.10 is released**, and he's about to `/update` to it (from v2.9, which is fine). The new download code
  (tag-specific URL) gets its first real test on the update **after** 2.10, so ask how it went.
- **Workflow has no `timeout-minutes`:** the v2.10 build hung for 7+ min on apt (re-run fixed it).
  Suggested he add `timeout-minutes: 10` under `runs-on`. He has to do it (the bot can't touch workflows).
  Check whether he did.
- Never verified on his machine: how v2.8/v2.9 UI looks (panels, header mode label, settings row),
  `web many` focus grab, batch tabs in one browser window, the remembered position.
- Ideas: what else differs between home/work, voice/AI later, `/vh` change-notes.
- Repo history quirk: v2.9 was uploaded wrong (files landed in the repo root); v2.9b was the fix
  re-upload, cleaned up in v2.10. He used to drag-drop uploads; that's no longer needed.
