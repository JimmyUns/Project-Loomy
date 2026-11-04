# Loomy — Handover (v2.19 ended, waiting for him to merge + release)

Brief for a fresh chat. The code lives in the repo **github.com/JimmyUns/Project-Loomy**. Clone it with the bot
token (§2); don't ask for a zip. Read this first. Only open deeper files when the task needs them.

**This file lives in the repo, not in the chat project.** The project only holds `github-bot.txt` and a short
pointer. Read `HANDOVER.md` from the newest open `claude/v*` branch if there is one (its PR has the latest
notes), otherwise from `main`. **Keep it current:** every version branch updates this file (and
`ELLUNE-HANDOVER.md` when Ellune changes) as part of its PR, so merging the version also merges the handover.
The repo is **public**: never put secrets or anything private in here.

**Sister project: Ellune** (the online/server side) has its own `ELLUNE-HANDOVER.md`. Keep Ellune notes there, not here.
**Pocket Loomy** (Android voice remote over Bluetooth, PC side = `/phone`) has its own `POCKET-HANDOVER.md`: the full
agreed design and security rules. Read it before touching voice/phone work.

## 1. What Loomy is
A summon-anywhere command console for Windows (hotkey → dark overlay → short commands), built by the user,
**Jimmy Younes**. It's one tiny C program with no runtime (~163 KB exe at 2.16; Pocket Loomy's PC side added ~43 KB in 2.17). Values: **as light as possible**, local-first,
plum/purple theme with a halftone dot strip, accent themeable. Long-term: voice (SAPI) and optional AI, still light.
It started as a tool for his personal PC and his work laptop, but it's grown well past what the work laptop needed
(Nov 2026): his **personal PC is the target**. Don't plan, test or ask around the work laptop unless he brings it
up. If he does use it there: no admin, and never help get around workplace security.

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
  **Cowork sessions (Nov 2026):** traffic to GitHub goes through a git proxy that only lets through repos in the
  session's sources ("not in this session's authorized repository set"). It blocks even with the LoomyBot token in
  the URL, and repos can't be added to a task after it starts: he has to start the task with the repo attached. He
  installed the GitHub connector on Nov 1. There, `/mnt/project` doesn't exist: read `github-bot.txt` with the
  Projects tool and keep the token in a scratch file, never in chat output. `gh` GraphQL is blocked; use REST.
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
- **`tests/run.sh`** first builds `tests/pocket_test.c` (Pocket Loomy's protocol + crypto, with ASan/UBSan), then builds the real `modes/web/display/commands/config/util/version.c` on Linux against a fake
  `windows.h` (`tests/shim/`): ~140 checks covering strict args, modes + the core lock, web, display (fake screens),
  Tab, panel keys, version ordering, and the release check (against `tests/fixtures/release_v2.9b.json` plus tampered
  copies), plus sysinfo (picker/keys/args with fake sensors), the Flow score math, and `phone.c` against a real
  phone-role engine over a fake Bluetooth radio (see `POCKET-HANDOVER.md` §10). `phone.c` has one test hook,
  `phone_code_for_tests()`, compiled only with `-DLOOMY_TESTS`. Output in `test.c` is `wprintf` only (a `printf`
  on the same stream silently vanishes).
  **Run it after any change and extend it for new features.** Gotcha: on Linux a `\` in paths stays
  in the filename (files land as `/tmp/loomytest\x.txt`). `update.c` and `ui.c` aren't in the tests.
- You **can't run the exe** (no Windows; Wine is unreliable for the real app). He's the real tester.
- **Layout check for sysinfo (works):** `tests/render/render.c` draws the sysinfo views with fake data into BMPs.
  `apt-get install -y --no-install-recommends wine64`, build it with the mingw line at its top, then
  `WINEDEBUG=-all WINEPREFIX=/tmp/wp xvfb-run -a /usr/lib/wine/wine64 render.exe` and look at the images
  (Wine has no Bahnschrift/Cascadia, so fonts fall back, but sizes/overlaps are right). Only GDI, no real sensors.
  `tests/render/render_phone.c` does the same for the `/phone` screen (build line in its header, no `-municode`).
  If `apt-get install wine64` fails to fetch, run `apt-get update` first.

## 4. Source map (`src/`)
`loomy.h` (version, colors, all decls — start here) · `ui.c` (window, paint, hotkey, tray, entry, panel router,
reload/position, `ui_flush`) · `commands.c` (command table, strict args, Tab) · `modes.c` · `web.c` · `display.c` ·
`version.c` (`ver_gt`, `release_check`) · `update.c` (WinHTTP, self-swap .bat) · `settings.c` (`/config`) ·
`browser.c` (`lp`) · `net.c` · `changelog.c` (`/verc`, baked-in `RELEASES[]`) · `config.c` · `util.c` ·
`sysinfo.c` (command, picker, keys, Flow score) · `sysdata.c` (all the Windows sensor reading) · `sysview.c` (the big view's drawing) ·
`phone.c` (`/phone`, codes, trust, lockout, running phone commands) · `phonebt.c` (Windows RFCOMM listener, SDP, random,
DPAPI, clock) · `pocket.c/h` (the wire protocol, both roles, shared with the Android app) · `monocypher*.c/h` (vendored
Monocypher 4.0.2, don't edit).

**Conventions**
- **Commands:** one row in `COMMANDS[]`: `{name, about, type, alias, sys, modes, usage, maxargs, fn, feature}`,
  plus `.phone = 1` (designated) if Pocket Loomy may run it; only plain (non-`/`) commands can be phone-safe.
  `sys=1` means it needs `/` (it changes Loomy); plain means a task or print. `usage` like `L"[alias | many]"`.
  `maxargs`: a number, `ARGS_TEXT` (free text), or `ARGS_SELF` (it parses its own subcommands and calls
  `cmd_usage(shape)`). Arg completion goes in `complete_args()`. `about` must fit help's column (≤35 chars).
- **Modes mask:** `ALL` or `CORE` (also `MODE_HOME`/`MODE_WORK`). A command outside the current mode is treated as
  nonexistent: `find_command` skips it, so it's "I don't know", not in `help`, no Tab, no slash hint.
- **Panels** (anything bigger than a line): a module with `_active/_open/_close/_paint/_key/_char/_click/_wheel`,
  wired into every dispatch spot in `ui.c` (screen_active, ui_hide, paint, edit_proc, click, dblclick, wheel).
  `display.c` is the cleanest small example (mirrors the mode picker).
  Draw the selected row with `ui_sel_row()` (list glide), not a fill of your own.
- **Network** runs on the UI thread. `https_get` sets WinHTTP timeouts (5 s DNS/connect/send, 10 s per read).
  Call `ui_flush()` (not `ui_redraw()`) before any slow call so the "Checking..." line actually shows.
- **New version:** bump `LOOMY_VERSION` **and the `# Loomy X.Y` title in `README.md`** (he checks it to confirm a merge worked), and add the entry to **both** `CHANGELOG.md` and `changelog.c` `RELEASES[]`
  (max 7 lines; the array holds 8 with the 0). The tag must equal the version exactly.
- **Versions:** a letter is a fix build (no new features): `2.9 < 2.9b < 2.9c < 2.10`. No default "a".
- UI text: real apostrophes, `\x2191`-style arrows (careful: `\x201c` followed by a hex letter merges; use `\u201c`),
  color tokens (`g_accent`, `C_DIM`...), never a hardcoded purple.

## 5. Current features
Plain: `help` · `lp [folder]` · `web [alias|many]` · `display`/`dp [extend|second|pc|duplicate|off]` · `net` · `rb` · `exe`.
Slash (all modes): `/mode` `/m` · `/pkg [add|remove <feature>]` · `/webconfig` `/webc` · `/config` · `/clear` · `/hide` · `/reload` · `/resetwindow` · `/shutdown`.
Slash (**core only**): `/update` · `/version` `/ver` · `/vh` · `/verc [version]` · `/ellune` (2.20).
Plain (**core only**, from 2.12): `github` `gh` (opens the repo page, hides Loomy).
Plain (all modes, 2.13): `sysinfo` `si [general|cpu|gpu [n]]`.
Slash (all modes, 2.17, opt-in feature `phone`): `/phone [forget]`.

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
- **v2.22 in progress (Nov 2), branch `claude/v2.22`.** Part 1 (items 1-3 below: redesign, modes, checkbox) built;
  he tried it, feedback rounds 1 + 2 done (`POCKET-HANDOVER.md` §11), round 2 not tried yet.
  **Round 2 made Ellune mode the default and Bluetooth an opt-in mode (blue theme)**: until remote exists, Ellune
  mode can't deliver commands, so **remote is now the urgent next step** (he called it "the mailing system").
  **Remote built Nov 2 (3 stages, `ELLUNE-HANDOVER.md` §2): Ellune 0.4 relay, the PC (`/phone` renamed `/remote`, his
  call, with Allow Bluetooth / Allow remote; `phonenet.c` + `ellune.c`), the app (`EllunePipe`). **Tested end to end
  Nov 4: works** (a command with the phone's Bluetooth off reached the PC). Ellune is on 0.4 (deployed with wrangler
  from his PC; Node installed). Still on his list: master-password registration. Popups on switching modes removed (his ask). (`POCKET-HANDOVER.md` §5 + §11). His list, in the order agreed (test each part first):
  1. **The app, redesigned.** Today's layout (buttons on top, then the log, settings at the very bottom) he called
     horrible. Wanted: a big mic in the middle with Loomy's halftone dots rippling out from its centre (calm idle,
     moving with the voice while listening); a cog (top right) holding all settings + the PC/pairing stuff; the
     latest log line at the top, tap it to open the whole log over the screen. His idea, we can rethink it to look
     better. **The notification stays exactly as it is (he loves it).**
  2. **One dropdown instead of the confusing switches:** **Say "Loomy"** (everything after her name is the command)
     or **Solo** (everything said is a command, no name). In both a sentence ends on a pause **or "thank you"**;
     "please" is just ignored, never an end ("can you please turn my screen on" / "... on please"). Told him the
     limit: Whisper gets the clip after you stop, so "thank you" can't cut the recording instantly (the pause does);
     it trims the command and ends it in the text. **The phone keeps listening for "Loomy"** (his OK: he isn't
     worried about privacy, he mutes the mic himself, but it's free and doesn't use up Ellune's allowance; switch
     if the phone keeps missing "Loomy" with his accent).
  3. **Phone recognizer = a checkbox he ticks, no auto switching.** If Ellune fails while online: an error, not a
     silent switch. Offline: the phone's recognizer is the only option, so use it.
  4. **Registration with a master password** (his idea, I said it's sound): the password is a Cloudflare *Secret*;
     "Add to guest list" asks for it once and Ellune stores the phone's key itself (KV or Durable Object storage)
     instead of him pasting into `DEVICES` (keep `DEVICES` as a backup). 5 wrong tries → registration locked an
     hour. Constant-time compare. Needs a removal path too.
  5. **Remote (his call: in v2.22):** Bluetooth only for pairing, after that commands go through Ellune when
     Bluetooth isn't there. Design in `ELLUNE-HANDOVER.md` §6 A + §7. Needs Durable Objects (free plan: SQLite-backed,
     100k requests/day, WebSocket hibernation); probably can't be set up by pasting in the dashboard: likely a one-time
     `npx wrangler deploy` from his PC (Node). Check for certain first and tell him.
- **v2.21 ENDED (Nov 2):** PR #20 ready. The security talk (`ELLUNE-HANDOVER.md` §7) + "the brain": Ellune's guest
  list, Ellune hears you (Whisper) + her AI for phrases the phone doesn't know. Worker 0.3, deployed with the `AI`
  binding. All tested by him on his phone: works. (He first asked for more in 2.21, then moved it all to 2.22.)
- **v2.20 ENDED (Nov 2):** PR #19 ready. Ellune's ping, nothing else (he chose to end it there). The server (`ellune/`) is live and `/ellune`
  (core) pings it (`ELLUNE-HANDOVER.md` §2). He tested it on his PC: up, first run 1735 ms (DNS + Windows' proxy
  check, cached after), then 63-300 ms with one 734 spike: normal, no fix needed. Then he asked for the app to ping
  too: **Ping Ellune** in Pocket Loomy (INTERNET permission, his OK; `POCKET-HANDOVER.md` §11). Tested on his phone
  (Nov 2): works, 150-700 ms, usually ~400 (phone radio wake-up + fresh TLS each time: normal). The Cowork session that started it couldn't push; a later chat applied the patch and pushed.
  Work laptop not tested and not needed (§1).
- **v2.19 ENDED (Nov 1):** PR #17 ready (he tested it: works). Pairing confirm on the PC + the link/pipe split
  + `/phone` "Recent" renamed "Logs" (`POCKET-HANDOVER.md` §9.8).
  Next big direction (agreed, not started): Ellune as a blind relay + brain (`POCKET-HANDOVER.md` §11b).
- **v2.18 released (Oct 31).** He hasn't set up the APK signing secrets yet (so no APK on releases; he uses dev builds). The release APK only gets
  built if he has set up the signing secrets (`POCKET_KEYSTORE_B64`, `POCKET_KEYSTORE_PASSWORD`, alias `pocket`;
  steps were given in chat: PowerShell + keytool from a JDK); without them the release just has `loomy.exe` and he
  keeps using the dev APK. The release app is a separate install from "Pocket Loomy dev" (needs its own pairing).
- **The Pocket Loomy Android app** on `claude/v2.18` (PR #16), in `pocket/` — status in
  `POCKET-HANDOVER.md` §11. He added `.github/workflows/pocket.yml` on the branch (pull before pushing). **First real test
  worked** (pairing + commands). His feedback is in as 2.18 (protocol v2: notes + forget; misses and refusals shown
  on the PC; notification + tile + 5-min ping; state-driven screen; "slash" words). His second round of feedback is built (`POCKET-HANDOVER.md` §12); waiting on his test of that build.
  Later (his call, separate build): slash commands from the phone behind an extra security step. App (Kotlin + the same `pocket.c` via the
  NDK). The APK goes on the same `vX.Y` release as `loomy.exe` (an APK-only release would look like the latest
  release to `/update`). The sandbox can't build it; that needs a workflow change only he can make.
- **v2.17 is released** (PR #15, tag `v2.17`, exe 206,848 B built by Actions first try). He tested on Windows: Loomy
  unchanged with the feature off; `/pkg add phone`, `/phone` code + "listening", 0 CPU idle, code expiry, `/reload`
  all fine. The real phone connection is untested until the app exists. **Pocket Loomy PC side**: — protocol, `/phone`, Bluetooth listener,
  lockout, allowlist, tests, docs. Full details, the wire protocol and what to check on Windows are in
  `POCKET-HANDOVER.md` (§9, §10). Opt-in feature `phone`: `features_on = phone` in `loomy.cfg` (opt-in features
  are stored as an ON list, `FEATURES_OPTIN` in `loomy.h`; they don't count in help's "N features are off").
  Also new in `loomy.cfg`: `phone_locked`. Nothing of it ran on Windows yet. Calls he hasn't confirmed: hang-ups
  mid-handshake aren't a lockout; `SO_BTH_AUTHENTICATE` (needs Windows-level Bluetooth pairing first); the
  phone-safe list.   Voice on the PC itself was considered and dropped (always-on mic, +20–50 MB RAM, Windows' speech engine is
  deprecated); the phone listens instead.
- **GPU temperature (sysinfo) reads a stuck 101 °C on his laptop; he parked it ("forget it for now").** Unconfirmed
  which GPU. Suspect: the Intel iGPU's driver reporting junk via D3DKMT; Task Manager never shows iGPU temps. Likely
  fix: ignore temperature for integrated GPUs (also stops Flow wrongly flagging "hot"). Ask before changing.
- **v2.16 is released** (PR #14, tag `v2.16`; he confirmed `/pkg` works on Windows): `/pkg` feature toggles. He wanted VS Code-style "extensions" to pick
  features per PC. **Decision: everything stays built into the exe; `/pkg` turns features on/off per PC. No downloads.**
  Why: it gives him the per-device feature set; real downloadable packages (DLLs) would only save ~50 KB now, add
  security/release work, and may be blocked on his work laptop (unsigned DLLs from user folders). RAM: off features'
  code is never paged in, so they cost ~nothing. New features still ship via normal releases + `/update` (fine at
  this size). **Revisit DLL packages only if** Loomy gets MBs big (voice/AI), features update much faster than the
  core, or other people write features.
  How it's built:
  - `features.c`: `FEAT_CORE` + `FEAT_WEB, LP, NET, RB, DISPLAY, SYSINFO, GITHUB` (ids, names, abouts), `feature_on()`,
    `/pkg` (bare = toggle list panel; `add|remove <feature>`; Tab), core words get "part of Loomy's core".
  - `Command` has a last field `feature`; rows that leave it out are core. `in_mode()` also checks `feature_on()`, so
    off commands are unknown, not in `help`, no Tab (same path as the mode lock). `help` adds "N features are off".
  - Saved as the OFF list: `features_off = net, sysinfo` (bitmask `g_cfg.featuresOff`; unknown names ignored), so
    new features arrive switched on. Turning `web` off keeps the saved-site files.
  - The pkg panel is wired into `ui.c` like the others (screen id 9). **Not done on purpose:** the planned panel-registry
    refactor of `ui.c`'s if-chains. Toggles don't need it, and it's a big untested change to `ui.c`; do it only
    when real DLL packages happen.
  - To make a new command optional: add a `FEAT_` id in `loomy.h` + its strings in `features.c`, and put it last in the
    command's row.
  - Tests: ~45 new checks (remove/add, unknown when off, help, Tab, core refusal, usage, mode lock + feature, the
    toggle list keys, config parse/save).
  - Ideas parked for later (he liked them, not decided): a quick **side panel** (small input-only window that slides
    in at the top-left, runs simple commands, hands bigger ones to full Loomy; catch: exclusive-fullscreen games);
    **data packages** (a file defining simple shortcuts like "jira opens this URL").
- **v2.15 is released** (PR #13, tag `v2.15`): fade-in fix, Fade speed slider, double-click clock to hide. These were
  meant for 2.14, but he merged PR #12 and tagged `v2.14` after its first commit, so they shipped as 2.15.
  **Lesson:** a draft PR can still be merged; if he says something "looks good" mid-version, check whether he merged.
  Never confirmed on Windows: the clock double-click.
- **v2.14 is released** (PR #12, tag `v2.14`): animations + `/config` sections. Not run on Windows. Ask him to check: the
  dot wave looks calm (not busy), the line glow on open (Win 10: sweep along top/bottom border and the input rule;
  Win 11: border brightness pulse), list glide in `/m`, `display`, `web`, `lp`, `/config`; text fade; fade-in when turned
  on; the Customization section fits; and CPU while Loomy sits open with the dot wave (Task Manager).
  - `anim.c`: one timer (`TIMER_ANIM`): 16 ms while a one-shot runs, 33 ms for the dot wave alone, killed when hidden.
    `anim_show()` (before `ShowWindow`) gates it: `animations = auto` skips everything if RAM >= 90 % or battery saver,
    else opens plain and samples CPU over the first 50 ms (`GetSystemTimes`), starting effects only if < 80 %.
  - Dot wave frames invalidate just the strip (`ui_invalidate_strip`); `paint()` has a fast path that redraws only
    that band when the update region is inside it.
  - **List glide:** panels draw their selected row with `ui_sel_row()` (never their own fill). `paint()` calls
    `anim_paint_begin(dc, screen)` before the panel, which paints the highlight at its gliding spot *under* the rows;
    `ui_sel_row` records the target. **New panels must use `ui_sel_row` for the selection.**
  - Text fade: `Line.born` (tick at push); lines from before Loomy opened don't fade.
  - Fade-in: `WS_EX_LAYERED` + alpha ramp, removed when done.
  - Fade-in length is `fade_ms` (slider row "Fade speed" in Customization, 100–800 ms, step 50, default 300,
    clamped on load). It's a smoothstep curve whose clock starts on the first timer tick (after the first paint).
    The first version (140 ms ease-out) read as "nothing happens".
  - **Double-click the clock = hide.** The header is `HTCAPTION` (drag handle), so the double-click arrives as
    `WM_NCLBUTTONDBLCLK`; `paint()` stores a padded `g_clockRect`. Dragging by the clock still works.
  - Config keys: `animations = auto|on|off`, `anim_dots`, `anim_lines`, `anim_select`, `anim_text`, `fade_in`, `fade_ms`.
    `settings.c`: sections via `SEC_FIRST[]`, `settings_open(section)`, `/config [general|custom]`.
- **v2.13b is released** (PR #11): input bar hides while a panel is open (`input_shown()`; edit box parked off-screen,
  keeps focus; `paint()` re-runs `layout()` on change, so new panels get it free), and mode switch / `/reload` keep
  focus (`AllowSetForegroundWindow(ASFW_ANY)` before `cleanup()` in `ui_restart_to`; `/update` relaunch not covered).
  Ask whether both work on his PC.
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
  through Ellune (see `ELLUNE-HANDOVER.md`).
- **Floated, not decided:** `audio` output switch (needs the undocumented IPolicyConfig COM call), `timer 10m`,
  `pc lock|sleep|restart`, per-mode command sets for work, voice/AI later, `/vh` change-notes.
- **Known small stuff from the 2.10 perf review (all fine for now):** `res/loomy.rc` version info still says 2.0
  (should come from `LOOMY_VERSION`); `net.c` header comment is stale (speeds/kill already exist); WINHTTP.dll loads
  at startup though only updates use it (could be lazy-loaded like `net.c` does); batch opening sleeps 150 ms per tab
  on the UI thread; the mode and display pickers duplicate code (share it if a third picker appears); icon BMP
  entries could be PNG (~5 KB). Idle cost is zero (no timers while hidden).
