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
- **The helper plan (his, Nov 8-9):** Pocket Loomy as an *action* helper, not a chatbot ("not Jarvis"). Steps agreed:
  1 talk back + ask when unsure + remember a minute (**v2.24d, built**), 2 reminders/notes with notifications (his
  choice per note), 3 game alerts opt-in per game with "update it?" (phone polls Ellune ~30 min; no Firebase), 4 the
  PC reports its state (display mode, downloads, apps) and starts updates / closes *approved* apps, always asking
  first, 5 a custom-voice package (Piper via sherpa-onnx; Kokoro too slow on phones). Voice: he listened, **Samsung's
  TTS is fine** for now. He reversed "no notifications" for reminders and game alerts: opt-in only.
- **v2.24d (Nov 9): step 1.** `Speaker.kt` (Android TTS, system engine, quiet on silent/vibrate, `Store.talkBack`
  default on, mic paused while speaking: `thinking` stays true until TTS onDone, 15 s cap). Ellune **0.9**:
  `/understand` with `"talk":true` + `history` (≤6 turns) → `{kind: do|ask|say|none, command, text}` (`TALK` prompt
  = DO/ASK/SAY rules + examples + the COMMANDS list; `talkLine` parses; old phones unchanged). Phone: `Ellune.talkNow`,
  `Voice.convo` (forgotten after 60 s quiet), after her ASK the next sentence goes back to her (`herAskAt`, 20 s, no
  name needed, key words skipped so "yes" isn't Enter; a known command still runs). Not tried on his phone yet;
  needs the 0.9 deploy. "Which view?" questions from the PC aren't spoken yet.
- **v2.24c (Nov 8), his bug: game questions answered "Websites is off on this PC... /pkg add web"**, i.e. something
  sent `web <game>` (the phone's "open X" → `web X` rule, or the AI guessing when `gameQuestion` missed the first
  recognizer guess). Fix: `gameQuestion` checked over every guess, "up date" joined, AI prompt (Ellune 0.8.1, needs
  deploy): web never for games, update questions → NONE. **Real cause (his exact phrases, no "Loomy" in them):
  Solo mode.** 2.24a skipped game questions when Solo had overheard them, so they went to the AI. Now they're taken
  in Solo too (an overheard question only shows an answer). His phrases tested: "did league drop any updates
  today?" → TODAY league; "did league of legends have any updates recently?" → LAST league of legends.
- **v2.24b (Nov 8), his bug: after a game question listening said "Stopped." and questions went unanswered**
  (commands still fine). Cause: `gameAsk`/`understand` called `ear.cancel()` after the recognizer had already
  finished → ERROR_CLIENT → `missed` restarted listening mid-question → collided with the restart after her answer →
  repeat until 5 errors → stop. Fix: no cancel there, `missed` ignores errors and `startEar` won't start while
  `thinking`. Not verified on his phone yet.
- **Nov 8: "Ellune found no game called League of legends"** = the live Ellune was still **0.5** (`/ping`):
  0.6, 0.7 and 0.8 were never deployed, so `/game` 404'd. The phone now says "Ellune is out of date" for a 404
  that isn't her "no game called" (next build). He has to run `npx wrangler deploy` in `ellune/`.
- **v2.25 in progress (Nov 8): "when did my game update" (his ask).** Built, in v2.24a, not tried by him yet.
  His rules: **only when he asks, never notifications / live alerts.** Answer big above the mic (reuses the
  "Which view?" box: `Voice.Shown`, 15 s, tap away), times in the phone's zone (`ZoneId.systemDefault()`, he's EET/EEST).
  - Phone: `Words.gameQuestion` (an update word + a game name; "today/this morning" → TODAY "Yes, at 6:12 PM" / "No, last
    update 3 days ago"; "when/last/ago" → WHEN "Yesterday at 11:50 PM"; else LAST "Last update: Wed 4/11/2026, 6:12 PM",
    d/M/yyyy). Not from Solo's overhearing. `Games.kt` words the answer; `Ellune.gameNow`. Bluetooth mode: says it
    needs Ellune (his old rule that BT mode never uses her).
  - Ellune **0.8**: `POST /game {q}` (guest list). Steam: store search (exact name wins) → api.steamcmd.net public
    branch `timeupdated` + buildid, on the spot. Riot: no public "when" → **cron every 15 min** (`[triggers]` in
    wrangler.toml, `riotTick`) reads `clientconfig.rpg.riotgames.com/api/v1/config/public?namespace=keystone.products.
    <league_of_legends|valorant>.patchlines` (live win `patch_url` changes every patch) into a logbook in the Guests
    object; `changed` = first time a new URL was seen (so "around", ±15 min), `since` = logbook start. TFT = League.
    He asked if Riot could flag it: no account/token is sent, public address, polite User-Agent; worst case Riot
    changes/blocks it and only the Riot part says "couldn't check". Unofficial endpoints: recheck if it breaks.
  - Not built (his call): no notifications, no update sizes, no auto-updating (the earlier Steam-files / launch idea
    is parked).
- **v2.24 released (Nov 7)**, he tested it: "its good".
- **Versions (his rule, Nov 7):** every release before a version is done has a letter; the finished one has none.
  Letters sort AFTER their number (2.23a > 2.23, `ver_gt` + pocket.yml's versionCode), so test builds hang off the
  last finished version: work toward v2.24 on branch `claude/v2.24`, release **v2.23a, v2.23b...**, finish as **v2.24**.
- **v2.23b (Nov 7, his feedback on v2.23a's /pkg):** "External" ran into the about text → the list is names only,
  External in that column (ABOUT[] gone; DESC[] in the info view). **Enter = info, Left/Right (and Space) = on/off**,
  also inside the info view. Bare "mirror" stays display duplicate (his answer); "share my phone to my screen" →
  scrcpy (already did).
- **v2.24 ENDED (Nov 7):** he said end it. Final = v2.23a + v2.23b + the renames below. Still unconfirmed by him:
  Ellune 0.7 deployed (E choice across PCs), the work laptop (AppLocker vs AppData exes), "mirror my phone" by
  voice + "No phone found", the update button doing a real self-update. Ask about these at the start of v2.25.
- **Nov 7, after v2.23b ("ok works"):** renamed in /pkg: "Phone" → **Remote** (it's /remote), "Phone Screen" →
  **Android Screen Mirroring** (help: "Android Mirroring"); External column moved to +300 so the long name fits.
  Feature ids stay `phone` / `scrcpy` (his saved settings). Not released yet: goes into the next build.
- **v2.24 in progress (Nov 7): external packages (his ask).** v2.23a released (he ran it: the list shows). Reverses the
  v2.16 "no downloads" decision *for external tools only* (his call): Loomy's own features stay built in.
  - `/pkg` = `/packages`; **External** column; **I** = info view (DESC[] + the feature's commands via
    `cmd_of_feature`); **E** (external only) = type/paste (Ctrl+V) which release file to download, the files on the
    release listed under it; `/pkg update <id>`. Off-feature commands now say "X is off on this PC. /pkg add id"
    (`cmd_off_feature`, PC + phone).
  - `extpkg.c` (portable, tested against scrcpy's real release `tests/fixtures/release_scrcpy.json`): catalog
    (`FEAT_SCRCPY` = Genymobile/scrcpy, opt-in, `FEATURES_EXTERNAL`), JSON walker, auto-pick (only .zip, not other
    OSes/checksums/sources, this CPU's build wins via `IsWow64Process2`; a tie asks for E), his pattern with the
    version as `*`, `ext_asset_ok` (repo's own release URL, uploaded, size, `sha256:` digest).
  - `extwin.c`: background thread (`WM_PKG` back): GitHub API → pick → download (`net_get` from update.c, progress)
    → sha256 vs GitHub's digest → Windows' `tar.exe` into `new\` → find the exe → swap into `current\` →
    `installed.txt`. Where: `%LOCALAPPDATA%\Loomy\Packages\<id>\`. Off deletes the folder (his choice file stays,
    `%APPDATA%\Loomy\pkg-<id>.txt`). Run: `scrcpy` / `mirror` (`CREATE_NO_WINDOW`, stdout read with PeekNamedPipe
    so adb inheriting it can't hang us; exits non-zero within 8 s → the ERROR line, "No phone found..." for ADB).
    Loomy hides after starting it.
  - Ellune sync of the E choice: key `pkg.<id>.file.<arch>` via `net_pref_get/set` (phonenet.c, signed), only on
    PCs that already have an Ellune key (`net_pref_ready`; never makes one). Ellune **0.7** `/prefs` (needs deploy).
  - Phone: `Words.phoneScreen` (a phone word + show/mirror/cast/plugged...), "copy screen" → `scrcpy`. Bare
    "mirror" stays display duplicate (I asked him; no answer yet, so it's my default).
  - Ask him: does download/unpack work on his PC and the work laptop (AppLocker may block exes from AppData), does
    scrcpy start, is the "No phone found" message right, does E + Ellune carry the choice to another PC.
- **v2.23 in progress (Nov 6): understanding him.** v2.22b is released (he installed it; listening "much better").
  His asks, BUILT, not tried by him yet:
  - **Say what you mean:** "show me only the first monitor" did nothing (Windows calls it "PC screen only").
    `Words.layout` (first/main/pc/laptop vs second/other/external, "turn off the second" = first only, "turn on the
    second" = both, both/all = extend, same/mirror = duplicate; needs a screen word, or "switch to the first/second")
    and `Words.sysinfoView` ("open the general system information" → `si general`). Both only fire when every word
    is one they know; anything else still goes to the AI. Ellune **0.6** prompt says the same (needs his deploy).
  - **"Which view?"** (his design): a command that opens a list on the PC (si, display, web) → the PC's reply line
    is `Which one (si)? general, cpu, gpu 1.` (`*_choices` in sysinfo.c/display.c/web.c; an expanded sysinfo view
    still says "Showing ..."). The phone (`Words.question`, `Voice.Ask`, 20 s) shows "Which view?" big over the mic
    with the choices, and takes the next sentence without the name: `Words.pick` (the option itself, other words
    for it, "the second one"/"last"/"number 2"). "Never mind"/"cancel"/"go back" (`Words.isCancel`) drops it on the
    phone only: the PC is left alone (he said he may have picked there; **ask him** if cancel should also close the
    list on the PC). Not asked after keys (`input ...`). A single Talk listens once more for the answer. Tap the
    question to dismiss it. Talk card + notification show it too. lp has no choices text, so it doesn't ask.
- **v2.22b (Nov 5): a test release, his call.** He can't open Actions artifacts on his phone, so the update
  button + faster listening below ship as **v2.22b** (branch `claude/v2.22b`, its own PR) for him to install from the
  release page. **Letters now also mean "a release mid-version so he can test on the phone"** (not only fix builds).
  Why 2.22b and not 2.23b: 2.22 < 2.22b < 2.23, so the real v2.23 later is newer and the update button in 2.22b can
  update to it (the first real self-update test). After he merges + tags: rebase `claude/v2.23` on main (PR #22) and
  carry on there; v2.23 gets its own changelog entry for whatever comes next.
- **The update button + faster listening (Nov 5), shipped in v2.22b, not tried by him yet.**
  He said "build the update button and the faster Ellune listening": taken as the proposal below, as is.
  - **Update button** (`Update.kt` = plain checks, tested in tests/jni against `tests/fixtures/release_v2.22.json`
    + tampered copies; `Updater.kt` = Android part + `UpdateDone` receiver). Settings → **Updates** → Check for
    updates: GitHub's latest release must be by JimmyUns, `pocket-loomy.apk` uploaded by `github-actions[bot]`,
    state uploaded, URL exactly `.../releases/download/<tag>/pocket-loomy.apk`, sane size, a `sha256:` digest.
    Compares `Update.code(tag)` (same formula as pocket.yml) with the installed versionCode. Downloads to
    `cacheDir/update`, checks size + sha256, then the archive's package name, versionCode (must equal the tag's) and
    signer (vs the installed app, when Android reports both). PackageInstaller session (REQUEST_INSTALL_PACKAGES);
    `UpdateDone` opens Android's confirm screen. If "Install unknown apps" isn't allowed: a dialog → Android's setting
    → `onResume` carries on. **The dev app** (`.dev`) can't update itself from releases; the button installs /
    reinstalls the *release* app instead (`<queries>` lists it): that's how he can try the whole flow before a
    newer release exists. After a real self-update Pocket Loomy is off (`Boot` sets on=false, his earlier design).
  - **Listening (his feedback Nov 5: "a motorcycle outside stops it from running a command", spoken commands
    didn't work in Ellune mode, while the phone's recognizer "hears everything ... very fast").** Now: **the phone
    hears by default** (Android's recognizer + endpointing); when no guess makes a command the rules know, the text
    goes to Ellune's AI (`/understand`, as before). `Voice.heard` first tries the recognizer's other guesses
    ("display extent" / "display extend"). `Words.knows` now checks arguments too (display/dp args, si
    general|cpu|gpu [1-9], no-arg commands take none), so accent slips go to the AI instead of a PC refusal.
    `Store.phoneEar` is gone; **`Store.elluneHears`** ("Ellune hears you", default off) = Whisper as in 2.21/2.22.
    Bluetooth mode: phone + rules only, no AI (unchanged). Flipping the box restarts listening.
    **Vad** (only used with "Ellune hears you"): ends after 0.7 s quiet (was 1 s); while talking the room level
    updates (a steady 0.6 s within 6 dB under the voice = the room; the quietest frame of the last second when
    clearly under the voice); quiet also = under 16 % of the speech level. tests/jni: the old Vad held a mid-sentence
    engine open to 10 s (his bug reproduced), the new one ends ~1.3 s after you stop.
  - Ask him: does the phone-first listening feel fast and catch his accent with the AI behind it; does Check for
    updates in the dev app install the release app; Android's "install unknown apps" prompt. Real self-update can
    only be tried once a release newer than his app exists (v2.23 installed by hand → v2.24).
  Other things he might add (offered, not asked for yet): `ellune/totp-check.mjs` (prints the current code for a key,
  to compare with Authy: it found his typo); parked ideas: fingerprint lock on the app, "open steam"-style launching,
  Pocket Loomy auto-off after idle, `timer 10m`, the `audio` output switch, "Loomy ping Ellune" by voice. Still to be
  confirmed by him from v2.22: /shutdown no longer freezing, the net speed fix, Solo ignoring typing, the grid slider.
- **v2.22 RELEASED (Nov 4/5), with the signed APK.** He made `pocket-release.keystore` (keytool, CN=Pocket Loomy,
  backed up as base64 in a Bitwarden secure note) and set `POCKET_KEYSTORE_B64` + `POCKET_KEYSTORE_PASSWORD`; the
  tag build ran before the secrets, so he re-ran it and the APK got attached. He uninstalled the dev app, installed
  the release one, re-paired by QR (new Ellune key → Authy code): **Online, works.**
- **v2.22 ENDED (Nov 4), PR #21 merged.** He called it done after the power button + tap-to-send / hold-to-mute.
  Built but not all tried by him yet: QR pairing (after the mid-confirm fix), the setup screens by mode, the simpler
  `/remote`, /shutdown no longer freezing, the net speed fix, power + tap/hold. **Next release's first job: the
  APK in releases** (he has the steps: make `pocket-release.keystore` with `keytool` (Java), back it up, base64 it,
  GitHub secrets `POCKET_KEYSTORE_B64` + `POCKET_KEYSTORE_PASSWORD`; the release APK has another signature than the
  dev one, so the dev app must be uninstalled once and the phone paired again).
- **(the v2.22 history:) branch `claude/v2.22`.** Part 1 (items 1-3 below: redesign, modes, checkbox) built;
  he tried it, feedback rounds 1 + 2 done (`POCKET-HANDOVER.md` §11), round 2 not tried yet.
  **Round 2 made Ellune mode the default and Bluetooth an opt-in mode (blue theme)**: until remote exists, Ellune
  mode can't deliver commands, so **remote is now the urgent next step** (he called it "the mailing system").
  **Remote built Nov 2 (3 stages, `ELLUNE-HANDOVER.md` §2): Ellune 0.4 relay, the PC (`/phone` renamed `/remote`, his
  call, with Allow Bluetooth / Allow remote; `phonenet.c` + `ellune.c`), the app (`EllunePipe`). **Tested end to end
  Nov 4: works** (a command with the phone's Bluetooth off reached the PC). Ellune is on 0.4 (deployed with wrangler
  from his PC; Node installed). Still on his list for **this release (v2.22)**:
  - **Registration with Authy: BUILT Nov 4 (Ellune 0.5, `ELLUNE-HANDOVER.md` §2), he hasn't set it up yet.** Plan was: a TOTP secret (6 digits, 30 s)
    in Ellune as a Cloudflare Secret and in his Authy; a new phone ("Add to guest list") or PC (C in `/remote`)
    asks for the current code, Ellune checks it (a used code can't be reused), stores the device's key herself.
    5 wrong tries → locked an hour. `DEVICES` stays as a backup. A device list with Remove in the app (needs a
    code). The secret is made on his PC by a small script in `ellune/`, never in chat.
  - **QR pairing: BUILT Nov 4, not tried by him yet.** (`phone.c`: `g_pairRoom` per code, `phone_net_room`, `qr_text`, the
    QR drawn in the panel; `qr.c` wraps `src/qr/qrcodegen.c`; PHONE_NET moves rooms only between sessions. App:
    `MainActivity.handleLink` / `askAuthyThenPair` / `qrPair`, `Link.pair(via=)`, Link's device is nullable,
    `Pocket.ready` doesn't need a Bluetooth PC in Ellune mode. A QR pairing switches the app to Ellune mode.)
    The plan was: `/remote` → N shows the code AND a QR
    (`pocketloomy://pair?c=<8 digits>&r=<pairing room, 32 hex, random per code>&n=<PC name>`). He scans it with the
    phone's normal camera app → Android opens Pocket Loomy (intent filter, no QR library in the app). The app makes
    sure it's on Ellune's guest list (**his call: still asks for the Authy code, the QR alone isn't enough**), then
    pairs through Ellune in that pairing room: the same CPace pairing + the Enter confirm on the PC, only the wire
    differs. No Ellune change needed (rooms are generic; both ends are guests). PC: while a code is live and remote
    is allowed, PHONE_NET sits in the pairing room instead of the trust room (`phone.c` decides the room). QR drawn
    in C (vendor nayuki's qrcodegen, MIT). App: a QR-paired PC has no Bluetooth address, so Ellune mode must work
    without `pcAddress` (Link takes a nullable device). Bluetooth pairing stays, behind Allow Bluetooth.
    **Pairing stays 7 days** (his call; I offered auto-renew or 30/90 days).
  - **The APK in releases** (needs the signing key set up as GitHub secrets).
  - **Net speed meter spikes** (8 MB/s shown on his 800 KB/s line): fixed in code (4 s window, `PocketService`),
    not tried yet. Popups on switching modes removed (his ask). (`POCKET-HANDOVER.md` §5 + §11). His list, in the order agreed (test each part first):
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
