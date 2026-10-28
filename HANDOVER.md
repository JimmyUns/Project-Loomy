# Loomy — Handover (v2.13b in progress)

Brief for a fresh chat. The code lives in the repo **github.com/JimmyUns/Project-Loomy**. Clone it with the bot
token (§2); don't ask for a zip. Read this first. Only open deeper files when the task needs them.

**This file lives in the repo, not in the chat project.** The project only holds `github-bot.txt` and a short
pointer. Read `HANDOVER.md` from the newest open `claude/v*` branch if there is one (its PR has the latest
notes), otherwise from `main`. **Keep it current:** every version branch updates this file (and
`ELLA-HANDOVER.md` when ELLA changes) as part of its PR, so merging the version also merges the handover.
The repo is **public**: never put secrets or anything private in here.

**Sister project: ELLA** (the online/server side) has its own `ELLA-HANDOVER.md`. Keep ELLA notes there, not here.

## 1. What Loomy is
A summon-anywhere command console for Windows (hotkey → dark overlay → short commands), built by the user,
**Jimmy Younes**. It's one tiny C program with no runtime (~146 KB exe from 2.13; sysinfo added ~35 KB). Values: **as light as possible**, local-first,
plum/purple theme with a halftone dot strip, accent themeable. Long-term: voice (SAPI) and optional AI, still light.
He runs it on his personal PC and his work laptop (no admin there; never help get around workplace security).

**How he likes to work:** straight-talking, concise, casual, no hype. Give real tradeoffs with a clear
recommendation; he makes the calls. Say plainly what you could and couldn't test. A little "why" helps him learn.
If something's unclear, ask (one question). When he asks "what do you think, don't implement yet", don't build.

## 2. Workflow — the bot opens PRs, he merges and releases
- Project file `github-bot.txt` holds the **LoomyBot** token (classic PAT, only `public_repo`) and its expiry
  (2027-01-20, **about 3 months out as of Oct 2026: warn him as it gets close**). Never print the token. Read it inside bash
  (`grep -o 'ghp_[A-Za-z0-9]*' /mnt/project/github-bot.txt`) and mask it in any output.
  Clone: `https://x-access-token:$TOKEN@github.com/JimmyUns/Project-Loomy.git`. Commit as `LoomyBot`.
- **Version branches:** one branch per version, `claude/vX.Y`. Open it as a **draft PR** on the first change and keep
  adding commits to it (and updating the PR description) until he says **"end version"**. Then mark it ready
  (REST can't un-draft: use GraphQL `markPullRequestReadyForReview` with the PR's `node_id`), retitle it, and make
  sure the changelog date is right. Don't start `vX.Y+1` before that.
- Then: he merges → he creates tag `vX.Y` → Actions builds (~30 s) → he runs `/update` (in **core mode** from 2.11 on).
  Delete merged branches after.
- **Hard rules:** never push to `main`, never create tags or releases, never touch `.github/workflows`.
  GitHub enforces all of this (rulesets "protect main" and "protect releases" on `refs/tags/v*`; token has no
  `workflow` scope). He rejected any setup where a token could ship without him.
- Repo must stay **public** (`/update` reads releases without auth). The sandbox's unauthenticated API calls get
  rate-limited (shared IP), so always send the token.
- No GitHub connector exists in chat. His "GitHub Integration" is a read-only file picker.
- Present the built `loomy.exe` in chat when he might want to test before merging.
- **If the session can't reach the repo** (network rules block GitHub or `JimmyUns/Project-Loomy`), don't try to get
  around it. Commit locally, `git format-patch` the work, **present the .patch file** (a file that isn't presented
  can't be downloaded), and tell him: start the chat inside the Loomy project, or add the repo as a source for the
  session. A chat that can push can apply the patch (`git am`) and push it (v2.13 was rescued that way).
- **He creates the release** (GitHub UI "Draft a new release" with tag `vX.Y`, which also creates the tag). The workflow
  then builds and attaches `loomy.exe` (and renames it "Loomy vX.Y"). Builds are flaky: see §6.
- **If he says "don't push until the version is done"**, keep commits local on `claude/vX.Y` (no branch push, no PR)
  and push + open the PR only when he ends the version. The sandbox can reset between sessions and wipe local work,
  so tell him in chat what the local branch contains (the repo handover can't hold it until it's pushed).

## 3. Build & test
- **No C runtime**: `-nostdlib -nostartfiles -e entry -Os`. Use the helpers in `util.c` (`s_cpy`, `s_cat`, `s_ieq`,
  `s_istarts`, `s_words`, `s_count_words`, ...), not libc. `memset`/`memcpy` are defined there.
- `build.sh` (Linux/CI, compiles `src/*.c`) and `build.bat` (lists files explicitly — **add new .c files there**).
  Install the compiler in the sandbox: `apt-get install -y gcc-mingw-w64-x86-64` (run as root, no sudo).
- **Never add `-fdata-sections`**: tested, the exe jumps to ~2 MB (mingw's linker writes the big zeroed static
  buffers into the file). `-flto` only saves ~3 KB; not worth the risk.
- **`tests/run.sh`** builds the real `modes/web/display/commands/config/util/version.c` on Linux against a fake
  `windows.h` (`tests/shim/`): ~140 checks covering strict args, modes + the core lock, web, display (fake screens),
  Tab, panel keys, version ordering, and the release check (against `tests/fixtures/release_v2.9b.json` plus tampered
  copies), plus sysinfo (picker/keys/args with fake sensors) and the Flow score math.
  **Run it after any change and extend it for new features.** Gotcha: on Linux a `\` in paths stays
  in the filename (files land as `/tmp/loomytest\x.txt`). `update.c` and `ui.c` aren't in the tests.
- You **can't run the exe** (no Windows; Wine is unreliable for the real app). He's the real tester.
- **Layout check for sysinfo (works):** `tests/render/render.c` draws the sysinfo views with fake data into BMPs.
  `apt-get install -y --no-install-recommends wine64`, build it with the mingw line at its top, then
  `WINEDEBUG=-all WINEPREFIX=/tmp/wp xvfb-run -a /usr/lib/wine/wine64 render.exe` and look at the images
  (Wine has no Bahnschrift/Cascadia, so fonts fall back, but sizes/overlaps are right). Only GDI, no real sensors.

## 4. Source map (`src/`)
`loomy.h` (version, colors, all decls — start here) · `ui.c` (window, paint, hotkey, tray, entry, panel router,
reload/position, `ui_flush`) · `commands.c` (command table, strict args, Tab) · `modes.c` · `web.c` · `display.c` ·
`version.c` (`ver_gt`, `release_check`) · `update.c` (WinHTTP, self-swap .bat) · `settings.c` (`/config`) ·
`browser.c` (`lp`) · `net.c` · `changelog.c` (`/verc`, baked-in `RELEASES[]`) · `config.c` · `util.c` ·
`sysinfo.c` (command, picker, keys, Flow score) · `sysdata.c` (all the Windows sensor reading) · `sysview.c` (the big view's drawing).

**Conventions**
- **Commands:** one row in `COMMANDS[]`: `{name, about, type, alias, sys, modes, usage, maxargs, fn}`.
  `sys=1` means it needs `/` (it changes Loomy); plain means a task or print. `usage` like `L"[alias | many]"`.
  `maxargs`: a number, `ARGS_TEXT` (free text), or `ARGS_SELF` (it parses its own subcommands and calls
  `cmd_usage(shape)`). Arg completion goes in `complete_args()`. `about` must fit help's column (≤35 chars).
- **Modes mask:** `ALL` or `CORE` (also `MODE_HOME`/`MODE_WORK`). A command outside the current mode is treated as
  nonexistent: `find_command` skips it, so it's "I don't know", not in `help`, no Tab, no slash hint.
- **Panels** (anything bigger than a line): a module with `_active/_open/_close/_paint/_key/_char/_click/_wheel`,
  wired into every dispatch spot in `ui.c` (screen_active, ui_hide, paint, edit_proc, click, dblclick, wheel).
  `display.c` is the cleanest small example (mirrors the mode picker).
- **Network** runs on the UI thread. `https_get` sets WinHTTP timeouts (5 s DNS/connect/send, 10 s per read).
  Call `ui_flush()` (not `ui_redraw()`) before any slow call so the "Checking..." line actually shows.
- **New version:** bump `LOOMY_VERSION` **and the `# Loomy X.Y` title in `README.md`** (he checks it to confirm a merge worked), and add the entry to **both** `CHANGELOG.md` and `changelog.c` `RELEASES[]`
  (max 7 lines; the array holds 8 with the 0). The tag must equal the version exactly.
- **Versions:** a letter is a fix build (no new features): `2.9 < 2.9b < 2.9c < 2.10`. No default "a".
- UI text: real apostrophes, `\x2191`-style arrows (careful: `\x201c` followed by a hex letter merges; use `\u201c`),
  color tokens (`g_accent`, `C_DIM`...), never a hardcoded purple.

## 5. Current features
Plain: `help` · `lp [folder]` · `web [alias|many]` · `display`/`dp [extend|second|pc|duplicate|off]` · `net` · `rb` · `exe`.
Slash (all modes): `/mode` `/m` · `/webconfig` `/webc` · `/config` · `/clear` · `/hide` · `/reload` · `/resetwindow` · `/shutdown`.
Slash (**core only**): `/update` · `/version` `/ver` · `/vh` · `/verc [version]`.
Plain (**core only**, from 2.12): `github` `gh` (opens the repo page, hides Loomy).
Plain (all modes, 2.13): `sysinfo` `si [general|cpu|gpu [n]]`.

- **Strict input:** extra/missing words print one `Usage: <typed> <shape>` line (`<>` required, `[]` optional).
- **Modes** `home`/`work`/`core` (hardcoded; he doesn't want user-made modes). **core** = Loomy's own upkeep
  (updates, versions). `/m switch|hide|show|main <mode>`. At least one stays visible; hiding the current/main mode
  moves them to the next visible one, **preferring home/work over core**. `mode_main` and `mode_hidden` are saved in
  `loomy.cfg` (per machine); the current mode is memory-only. **Switching = full reload**
  (`ui_restart_to(mode, note)` → `--restarted --mode X --note a|b`); `/reload` keeps the mode, and so does
  `/update` (its swap .bat passes `--mode`, from 2.11 on). Home vs work still only differ by saved sites.
- **web:** per-mode `%APPDATA%\Loomy\web-<mode>.txt` (tab-separated). `/webc add|remove|batch|info|rename <old> <new>|update`.
  Batches *copy* URLs; `batch ... false` folds the singles away. `true/false/many` are reserved aliases.
  `web many` keeps the panel open, marks opened rows, and asks for focus back (`ui_refocus_soon`).
- **display:** same call as Win+P (`SetDisplayConfig(SDC_APPLY | SDC_TOPOLOGY_*)`, no admin). Picker order:
  Extend, Second screen only, PC screen only, Duplicate; current one tagged; e/s/p/d jump. Hides Loomy after a switch;
  stays open with an error if Windows refuses (e.g. one screen). 5th row **Screens off** / `dp off` (2.12): not a
  layout; ~0.5 s after hiding (`TIMER_SCREENS`, cancelled if Loomy is summoned) it sends `SC_MONITORPOWER` 2 to
  Loomy's own window (not broadcast). Layout untouched; any key/mouse wakes. His PC has no Modern Standby
  (`powercfg /a`, Oct 2026); work laptop unchecked.
- **sysinfo (2.13):** bare = picker (General, CPU, one row per GPU). Picking a view calls `ui_set_big(1)`: the window
  grows to 1.5x (1080x720 logical) around its middle, clamped to the monitor's work area (a 150%-scaled laptop gets
  ~672 tall; the layout is fluid). Esc closes + shrinks + frees everything; Backspace goes back to the picker; Tab /
  arrows / g / c / 1-9 switch views; `ui_hide` closes it first so the remembered position is the normal-size one.
  **Dynamic everywhere:** GPUs come from `D3DKMTEnumAdapters2` (software/display-only adapters skipped, discrete first,
  up to 4), each gets a card (compact when 2+) and its own view; per-thread bars use the real logical CPU count.
  **Data** (no admin, all loaded by hand, only while open, 1 s timer shared with `net`): PDH English counters for CPU
  utility/performance %, queue length, DPC+interrupt %, hard faults (`Pages Input/sec`), disk, network, and GPU engine
  utilization + adapter memory (matched to adapters by LUID in the instance names); `NtQuerySystemInformation` for
  per-thread load and per-app CPU/private memory (grouped by name); `GetPerformanceInfo` for processes/threads/commit;
  D3DKMT `ADAPTERPERFDATA` (62, temp in deci-C), `NODEPERFDATA` (61, node 0 clock), `ADAPTERPERFDATA_CAPS` (63, the
  driver's warning temp). Temp/clock need WDDM 2.4+, else "n/a". The first second shows "Measuring..." / "calibrating".
  **Flow** (`flow_cpu`/`flow_gpu`, unit-tested): starts at 100, penalties only for felt problems: threads waiting per
  core, clock under 90% of base *while busy* (idle downclock is fine), RAM <10% free, paging >300/s, DPC >=8%, maxed;
  GPU: temp within 3 of the warn temp (85 if unknown), VRAM >90% (dedicated only), clock <70% of max while busy.
  >=85 smooth, >=60 strained, else choking; text = top two reasons.
- **Window:** `remember_position` (config, off by default) keeps the last hide position in memory only, so a
  reload/mode switch re-centers. `/resetwindow` re-centers now.
- **/update safety:** only trusts a release authored by `JimmyUns` whose `loomy.exe` was uploaded by
  `github-actions[bot]`, and downloads that exact tag. **Don't weaken it.** Possible upgrade: verify the
  asset `digest` (sha256).

## 6. Open items
- **v2.13b is on `claude/v2.13b`, draft PR** (fix build, no new features):
  - Input bar hides while a panel is open: `input_shown()` in `ui.c` = no panel, or settings is editing name/accent
    (`settings_wants_input()`). Hidden = the edit box is parked off-screen (it keeps focus, so panel keys still route
    through `edit_proc`), the bar isn't painted, and the panel area grows down. `paint()` re-runs `layout()` when it
    changes, so every panel open/close is covered with no per-panel wiring. **New panels get this for free.**
  - Mode switch / `/reload` focus: `ui_restart_to` calls `AllowSetForegroundWindow(ASFW_ANY)` before `cleanup()`
    (while Loomy is still the foreground app), so the new copy's `SetForegroundWindow` isn't blocked by Windows'
    focus-stealing rule. Not covered: the `/update` relaunch (started from a .bat, not from Loomy).
  - Neither can be tested off Windows. Ask him to check both.
- **v2.13 (sysinfo) is released** (PR #10, tag `v2.13`). Never confirmed on Windows. Ask him to check: the numbers vs
  Task Manager (CPU %, speed, GPU %, VRAM), that **both** laptop GPUs show up with the right names (and discrete first),
  whether temp/clock show or say n/a, that opening sysinfo **doesn't keep the NVIDIA GPU awake on battery**
  (D3DKMT queries every second while open; Task Manager does the same, but verify), the window fitting his laptop
  screen, Bahnschrift fonts loading, and how long the first open takes (PDH setup).
- **v2.12 is released** (PR #6 merged, tag `v2.12`, exe built by Actions on the 3rd attempt). Ask how the
  `/update` went (he hasn't said for 2.11 or 2.12), and whether `dp off` blanks his screens.
- **Release builds:** the "Install the Windows cross-compiler" step (`apt-get`) used to hang (v2.10, 2.11 and 2.12
  all needed re-runs; a good run takes ~40 s). Fixed on `main` via PR #9 (Oct 2026): the step retries up to 3 times
  (60 s update + 120 s install per try) and the job timeout is 10 min. A tag build uses the workflow file *at the
  tag*; v2.13 was the first build with it and went through first try in 37 s. Only he can edit workflows; `main`
  is protected, so his edits land as a PR he merges.
- **Never verified on his machine:** the WinHTTP timeouts, "Checking GitHub..." showing before the wait,
  the mode picker with 3 rows, `/update` relaunching into core, `display` actually switching + the "current" tag +
  the picker text fitting (now 5 rows), `github` opening the page, `dp off` actually blanking the screens. Older: v2.8/v2.9 UI (panels, header mode label, settings row),
  `web many` focus grab, batch tabs in one browser window, the remembered position.
- **He declined (don't re-pitch unless he brings it up):** an ideas/suggestions note command; syncing saved sites
  through his GitHub (work-laptop data on personal GitHub may break company policy). Anything online now goes
  through ELLA (see `ELLA-HANDOVER.md`).
- **Floated, not decided:** `audio` output switch (needs the undocumented IPolicyConfig COM call), `timer 10m`,
  `pc lock|sleep|restart`, per-mode command sets for work, voice/AI later, `/vh` change-notes.
- **Known small stuff from the 2.10 perf review (all fine for now):** `res/loomy.rc` version info still says 2.0
  (should come from `LOOMY_VERSION`); `net.c` header comment is stale (speeds/kill already exist); WINHTTP.dll loads
  at startup though only updates use it (could be lazy-loaded like `net.c` does); batch opening sleeps 150 ms per tab
  on the UI thread; the mode and display pickers duplicate code (share it if a third picker appears); icon BMP
  entries could be PNG (~5 KB). Idle cost is zero (no timers while hidden).
