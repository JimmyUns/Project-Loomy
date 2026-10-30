# Pocket Loomy — Handover (PC side released in v2.17; Android app next, v2.18)

Brief for a fresh chat. **Pocket Loomy** = an Android app that hears Jimmy's voice commands and sends them to Loomy on
his PC over **Bluetooth**; Loomy runs them. Read Loomy's `HANDOVER.md` first (who Jimmy is, how he works, workflow,
codebase). Keep Pocket Loomy notes here. This repo is **public**: no secrets in here.

## 1. Why it exists
He wanted Alexa-style voice ("Loomy open", "Loomy hide", "Loomy run display extend"). Doing speech on the PC means an
always-on mic and +20–50 MB RAM in Loomy, plus Windows' speech engine is deprecated. So the **phone** does the listening
(Android's built-in recognizer, very accurate) and the PC only waits for messages (~zero CPU, a few KB of RAM).
**Commands only, not free speech.** He has an **Android** phone. Both his PCs have Bluetooth.

## 2. Decisions (all agreed with him, Oct 2026)
- **Transport: Bluetooth (RFCOMM), not Wi-Fi.** His apartment's Wi-Fi is shared, captive-portal, marked **Public**
  in Windows, and very likely has client isolation, so phone→PC over Wi-Fi wouldn't work there; marking it Private
  would be unsafe. Bluetooth works the same at home and in the apartment, isn't touched by Windows Firewall, and
  neighbours on the Wi-Fi can't see it. Range ≈ one room, which fits "near my PC". (Later option: relay via ELLA for
  anywhere-access; not now.)
- **PC side is a `/pkg` feature `phone`, OFF by default** (opt-in, unlike other features, which default on). Off =
  no Bluetooth listener at all. Never on the work laptop unless he decides so (company policy).
- **Phone side: off unless he turns it on.** A notification (expand → On/Off button) plus a **quick-settings tile**
  (the toggles next to Wi-Fi/Bluetooth). While off: nothing runs; if he swipes the notification away it re-posts
  itself (delete intent), no service needed. While on: a foreground service (mic), so Android pins the notification
  and shows the green mic dot (can't be hidden). Auto-off after a set idle time / when the PC can't be reached.
  Prompt once for "don't optimize battery" (Samsung/Xiaomi kill background listeners otherwise).
- **Battery:** tap-to-talk ≈ 0; always-listening = a few % per hour, only while he has it on.
- **Replies:** the PC's answer ("Opening gmail", "I don't know that one") shows on the phone as a notification/toast.
- **Performance:** plain native Kotlin, no heavy frameworks (no Flutter/React Native), tiny APK.

## 3. Pairing (his design, hardened)
- On the PC, `/phone` opens a **connection manager** screen. Untrusted: shows a **random 8-digit code** at the top
  (`BCryptGenRandom`) and "waiting for phone". Trusted: shows the trusted device and "awaiting trusted device ping".
- He enters the code **by voice only** on the phone (speech → digits). The phone becomes **trusted for 7 days**
  (fixed from pairing, not sliding); after that, a new code is needed.
- **Every failed attempt is printed** on the PC with the device name the phone claimed (unauthenticated, can be
  faked) and its Bluetooth address.
- **PAKE: CPace** (CFRG draft `draft-irtf-cfrg-cpace`, suite CPACE-X25519-SHA512, with key confirmation). Why: a
  plain "prove you know the code" exchange lets anyone who records it brute-force 8 digits offline in seconds; with
  CPace an active attacker gets exactly one online guess per run and nothing to brute-force offline. Check the
  implementation against the draft's test vectors (incl. low-order point rejection).
- Code rules: expires after 2 min; **one wrong attempt burns it**; a new code needs a deliberate action on the PC.
- After CPace, both sides exchange long-term X25519 keys inside the encrypted channel and store them:
  PC in `%APPDATA%\Loomy` protected with DPAPI (`CryptProtectData`, per user); phone in Android Keystore.

## 4. After pairing
- Every connection: fresh ephemeral X25519 + both static keys (Noise-KK-style triple DH), BLAKE2b/SHA-512 KDF, key
  confirmation both ways → forward secrecy, no replay of old sessions.
- Every frame: AEAD (XChaCha20-Poly1305 from Monocypher), counter nonces, reject anything out of order or reused.
- Commands travel as text inside the AEAD and go through Loomy's normal `cmd_run` (strict args etc.).
- **Allowlist:** only commands marked phone-safe (e.g. open/hide, `display`/`dp`, `web`, `rb`, `si`...). Never
  `/shutdown`, `/update`, `/pkg`, `/config`, `/phone`, `/reload`, `/webconfig`. Add a `phone` flag to `Command`.

## 5. Hostile input ("max security", his words)
- **Size before content:** 2-byte length prefix, hard max frame ~512 bytes; anything bigger, short, or malformed is
  dropped and the connection closed. Never trust a length the peer sends beyond that cap.
- **Authenticate before parsing:** nothing is interpreted until the AEAD tag checks out.
- **One connection at a time**; a few seconds to finish the handshake or it's cut (slow-drip/slowloris).
- **His lockout rule:** any failed auth, bad frame or unknown peer → the listener **shuts down completely**, Loomy
  alerts him (output line + tray balloon), and only `/phone` reopens it. Trade-off accepted: someone nearby could
  switch it off by sending junk, but can't get in.
- Fixed buffers, no allocation driven by peer input, all string handling bounded (`s_cpy` caps).
- Fuzz the frame parser and handshake state machine with random/hostile input in the Linux test harness.

## 6. Building it
- **Crypto library: Monocypher** (single C file, no dependencies, not even libc, audited by Cure53 in 2020; has
  X25519, Elligator 2, XChaCha20-Poly1305, BLAKE2b, optional SHA-512). Vendor `monocypher.c/h` (+ the optional
  SHA-512 file) into `src/` for the PC; compile the **same files** into the Android app via the NDK (JNI) so both
  sides run identical crypto.
- **PC (Loomy):** Winsock Bluetooth: `socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM)`, bind `BT_PORT_ANY`, `listen`,
  publish an SDP record with a fixed service UUID via `WSASetService`, and `WSAAsyncSelect` onto Loomy's window so it
  costs nothing while waiting. Windows may require the phone to be paired in Windows' Bluetooth settings first
  (OS-level); our CPace pairing sits on top and doesn't rely on it. New files e.g. `phone.c` (listener, protocol,
  `/phone` panel), `cpace.c`. Expected +25–35 KB; **actual +43 KB** (exe 162,816 → 205,824 B): Monocypher's used parts ~15 KB + SHA-512 3 KB, `pocket.c` 8 KB, `phone.c` 12 KB (mostly the screen), `phonebt.c` 3 KB. Link `ws2_32` (already) — check `WSASetService`.
- **Opt-in feature:** `features_off` can't express "off by default" (old configs have no `phone` entry). Store
  opt-in features separately, e.g. `features_on = phone`.
- **Android app:** Kotlin, `pocket/` folder in Project-Loomy (decided: same repo). `BluetoothSocket` via
  `createRfcommSocketToServiceRecord(UUID)` (Android 12+: `BLUETOOTH_CONNECT` permission), `SpeechRecognizer`
  (prefer offline), `TileService` for the tile, foreground service type `microphone|connectedDevice` while on.
- **The sandbox can't build the APK** (Android SDK/Gradle downloads aren't on its allowed domains). The APK has to
  be built by GitHub Actions — a **workflow change only Jimmy can make** — or on his PC. Signing: a release keystore
  would be a new secret (GitHub secret, his call); a debug-signed sideload APK is fine to start.
- **Testable here:** CPace and the handshake against test vectors, the frame parser + lockout logic (fuzzed), the
  allowlist, the `/phone` panel logic — on Linux with the existing shim. **Not testable here:** Windows Bluetooth,
  the Android app. He's the tester.

## 7. Status and order
**PC side is released (v2.17), steps 1 and 2 below.** He checked it on Windows without a phone (§10 list, all fine);
the real Bluetooth connection is still untested. **Step 3 (Android app) is in progress on `claude/v2.18`, §11.**
1. PC: feature `phone` (opt-in), Monocypher vendored, CPace + handshake + framing with tests, `/phone` panel, lockout.
2. PC: Bluetooth listener (`WSAAsyncSelect`), allowlist, replies. Release.
3. Android: Bluetooth connect + pairing by voice, send a command, show the reply. Then the notification/tile/
   always-listening polish, auto-off, battery exemption.

## 8. Open questions for him
- Phone-safe list is a first cut: `open`/`show`, `hide`, `web`, `rb`, `display`/`dp`, `sysinfo`/`si` (§9.7). Add more?
- Wake phrase details ("Loomy run …" vs just "Loomy …"), and whether tap-to-talk comes first.
- APK signing/distribution (debug sideload vs release key in GitHub secrets).

## 9. Wire protocol (v2 since Loomy 2.18; v1 was 2.17)
The Android app must match this byte for byte. The easiest way is to compile the same `src/pocket.c` +
`src/monocypher.c` + `src/monocypher-ed25519.c` through the NDK and drive it from Kotlin via JNI (the engine has
no OS calls and no allocation; the app only supplies `pk_random()`, e.g. from `getrandom()`). `pocket.h` explains
how to drive it. Constants below are in `pocket.c`.

### 9.1 Transport
- RFCOMM, SDP service UUID **`89609207-27e0-4126-90fe-40d16244b0cd`**, service name "Loomy". Android:
  `createRfcommSocketToServiceRecord(UUID)` (the secure variant). The PC sets `SO_BTH_AUTHENTICATE`, so the phone
  must be paired in Windows' Bluetooth settings first (OS-level bonding; our pairing sits on top).
- One connection at a time. The PC speaks first.

### 9.2 Frames
- `len (2 bytes, big-endian) || body (len bytes)`, 1 <= len <= 512. Anything else ends the connection
  (after just the 2 length bytes for an oversized one).
- Handshake frames are plain; `body[0]` is the type. After the handshake every frame is sealed:
  body = `XChaCha20-Poly1305(key, nonce, ad = the 2 length bytes, plaintext = type || data)` = ciphertext || 16-byte tag.
  One key per direction; nonce = 24 bytes, a 64-bit little-endian counter in bytes 0..7, rest 0, starting at 0 per
  direction per connection and never sent (so replays, drops and reordering fail the tag).

### 9.3 HELLO (PC -> phone, plain), always first
`01 | version (02) | flags | nb[16]`. A phone refuses any other version (F_VERSION, before sending anything). Flags: bit0 = a pairing code is open, bit1 = a phone is trusted; other bits
must be 0 (phone refuses otherwise). Version + flags are bound into the keys below.

### 9.4 Pairing (CPace, draft-irtf-cfrg-cpace, CPACE-X25519-SHA512, phone = initiator A, PC = responder B)
- PRS = the 8 ASCII digits. sid = nb || na (32 bytes). ADa = phone name (UTF-8, <= 48 bytes), ADb = PC name.
- CI = lv_cat("LoomyPocket1", "phone", "pc", [version, flags]) (lv_cat = LEB128 length before each field).
- Generator per the draft: SHA-512 of the zero-padded generator string, clear bit 255, Elligator 2.
  **Gotcha:** Monocypher's `crypto_elligator_map` only reads 254 bits; `pk_elligator()` first replaces r by p - r
  (or r - p if r >= p) when bit 254 is set (the map depends only on r^2). Tested against the draft's generator
  vector and a Python RFC 9380 reference.
- Vectors checked against the draft source at github.com/cfrg/draft-irtf-cfrg-cpace, commit `8fb4056e` (Apr 2026).
- `PAIR_A` (phone): `02 | na[16] | Ya[32] | ADa`
- `PAIR_B` (PC): `03 | Yb[32] | Tb[64] | ADb`
- `PAIR_C` (phone): `04 | Ta[64]`
- K = X25519(y, Y_peer); abort if all-zero. ISK = SHA512(lv_cat("CPace255_ISK", sid, K) || lv_cat(Ya, ADa) ||
  lv_cat(Yb, ADb)). Key confirmation as the draft suggests: mac_key = SHA512("CPaceMac" || sid || ISK),
  Ta = HMAC-SHA512(mac_key, lv_cat(Ya, ADa)), Tb = HMAC-SHA512(mac_key, lv_cat(Yb, ADb)).
- Traffic keys: first 32 bytes of HMAC-SHA512(ISK, "LoomyPocket1 phone->pc") / ("LoomyPocket1 pc->phone").
- Then sealed: phone sends `10 | phone_static_pub[32]` (right after PAIR_C), PC answers `11 | pc_static_pub[32]`.
  Both static key pairs are fresh X25519 keys made for this pairing. The phone then hangs up and reconnects.
- The PC burns the code the moment PAIR_A arrives (one try per code, whatever happens next).

### 9.5 Connecting (trusted phone; Noise-KK-style, all four DHs)
- `CONN_A` (phone): `05 | na[16] | Ea[32] | phone name`
- `CONN_B` (PC): `06 | Eb[32] | Tb[32]`
- `CONN_C` (phone): `07 | Ta[32]`
- ee = X(ea, Eb), es = X(ea, S_pc), se = X(s_phone, Eb), ss = X(s_phone, S_pc); abort if any is all-zero.
- th = SHA512(lv_cat("LoomyPocket1 connect", [version, flags], nb, na, Ea, Eb, S_phone, S_pc, phone name));
  master = HMAC-SHA512(key = th, ee || es || se || ss); Tb = HMAC(master, "LoomyPocket1 pc confirm")[0..32],
  Ta = HMAC(master, "LoomyPocket1 phone confirm")[0..32]; traffic keys as in pairing but from master.

### 9.6 After the handshake (sealed)
- `20 | text` command (phone -> PC, UTF-8, 1..400 bytes, no NULs); `21 | text` reply (PC -> phone, 0..400 bytes).
- `22` ping (phone -> PC), `23` pong. The PC hangs up after 90 s of silence, so ping (or just reconnect).
- v2: `24 | text` note (phone -> PC, 0..400 bytes): something the phone heard but couldn't turn into a command
  (empty = heard nothing). The PC prints it ("didn't catch a command (heard ...)") and logs it. No reply.
- v2: `25` forget (phone -> PC): the phone dropped the pairing. The PC forgets the phone at once (deletes
  `phone.dat`, stops listening). Nothing may follow it (both engines go to DONE).

### 9.7 What the PC does with a command (phone.c)
`open`/`show` and `hide` (alone) show/hide Loomy. Otherwise the first word must be a plain command marked
`.phone = 1` in `commands.c` that exists right now (mode + `/pkg`): `web`, `rb`, `display`/`dp`, `sysinfo`/`si`.
It runs through `cmd_run` (strict args apply), prints "From your phone:" in Loomy, and everything it prints goes
back as the reply ("Done." if nothing). Refused and unknown commands are printed in Loomy too (2.18), with the
reason, and logged in `/phone` Recent. Slash commands get "Slash commands only work on the PC.", others
"'x' only works on the PC." / "I don't know 'x'." Control characters and bad UTF-8 are refused politely.

### 9.8 PC policy (phone.c)
- Listens only when: feature on, not shut down, and (a phone is trusted or a code is open). No timer while idle;
  a 1 s timer only while a code is open or a phone is connected.
- Code: 8 digits (`BCryptGenRandom`, rejection-sampled, no bias), 2 minutes, burned by the first PAIR_A. Opening
  `/phone` with no trusted phone makes one; after that only N does.
- Trust: one phone. `%APPDATA%\Loomy\phone.dat`, DPAPI per user (+ fixed entropy). Holds both static keys, the
  phone's claimed name, its address, paired/expiry times. Expires 7 days after pairing (or if the clock jumps back
  more than an hour); then it's deleted and listening stops, quietly (no shutdown).
- **Shutdown ("lockout"):** any `PK_EV_FAIL` from the engine (bad size, out-of-order, low-order key, wrong code
  tag, unknown phone, failed decrypt, malformed message, data after pairing) or Windows refusing random bytes.
  Hangs up, burns the code, stops listening, saves `phone_locked = on` (survives restarts), prints the reason with
  the claimed name + address, tray balloon. Enter in `/phone` turns it back on.
- **Not** a shutdown (my call, he hasn't confirmed): a phone hanging up or stalling (10 s) mid-handshake. It gets
  cut and logged; if it had used the code, the code is still burned and Loomy says so.
- If Windows ever fails to give random bytes, nothing made from them is sent (checked after every engine call).

## 10. Testing
- `tests/run.sh` builds `tests/pocket_test.c` with ASan + UBSan: the draft's X25519 vectors (generator, Ya, Yb, K,
  ISK, low-order points q0..qb), 11 Elligator edge cases, engine against engine (pairing, connect, commands,
  replay, reorder, AD tamper, wrong trust), low-order shares in real runs (u0..u5, u7 abort), every bit of a
  recorded pairing (~2300 runs) and connect (~2200) flipped (none succeed), 20,000 random frames (none accepted).
- `tests/test.c` drives `phone.c` with a real phone engine over a fake radio (`shim.c`): opt-in config, codes,
  wrong code, pairing, restart, every allowlist case, ping, idle cut, all the shutdown paths, impostor phone,
  300 junk connections, handshake timeout, RNG failure, 7-day expiry, forget, `/pkg remove phone`, no radio.
- `tests/render/render_phone.c` draws the `/phone` states under Wine (see its header).
- **Not testable here:** Windows Bluetooth itself. Ask him to check on his PC (feature is off on the laptop):
  `/pkg add phone`, `/phone` shows a code and "listening" (if it says no Bluetooth: radio off or SDP refused);
  that Loomy shows up as a service from the phone (any Bluetooth serial app listing SDP services); that
  `SO_BTH_AUTHENTICATE` doesn't block the Android connection (if it does, try without it); that `/reload` and quitting
  remove the SDP record; idle CPU stays 0 while listening.

## 11. Android app (`pocket/`, v2.18, first cut)
- Plain Kotlin, framework APIs only (no AndroidX, no libraries), minSdk 26, targetSdk/compileSdk 35, AGP 8.7.3,
  Kotlin 2.0.21, Gradle 8.14.3, JDK 17. Package `io.github.jimmyuns.pocketloomy`. **No INTERNET permission**;
  backups and device transfer exclude everything (`res/xml/no_backup.xml`).
- `cpp/pocket_jni.c` + `CMakeLists.txt` compile `src/pocket.c` + Monocypher (arm64 + armv7 only).
  `pk_random` = bionic `arc4random_buf`. `Native.kt` mirrors the JNI; event/failure numbers must match `pocket.h`.
- `Store.kt`: trust (phone sk/pk, PC pk, paired time, PC name) sealed with AES-256-GCM under an Android Keystore key
  (`pocket_trust`); PC address + label in plain prefs. Phone also shows the 7-day expiry.
- `Link.kt`: one worker thread, blocking RFCOMM via `createRfcommSocketToServiceRecord(SERVICE)`, watchdog closes
  the socket on timeouts (15 s connect/reply, 20 s pairing). Pairing: one connection, then hang up. Commands:
  connect + handshake, keep it open 60 s for follow-ups, one retry on a stale kept connection.
- `MainActivity.kt`: Choose PC (bonded devices), Pair (voice only: say 8 digits; `Words.code`), Talk (tap, speech,
  `Words.command` strips "Loomy"/"run"), typed commands, a log. `SpeechRecognizer` with prefer-offline.
- 2.18 additions (his feedback after the first real test, which worked):
  - `Words.command`: "slash X" (also "slesh/slush/flash") -> "/X"; spoken names -> commands (packages -> pkg,
    recycle bin -> rb, system info -> si, web config -> webconfig, d p -> dp, reset window -> resetwindow).
    Slash commands are still refused by the PC (§9.7); **he was asked which ones to allow** (pending).
  - Misses go to the PC as notes (`Pocket.reportMiss`): recognizer no-match/timeout, or text that made no command.
  - Forget: `Link.forget` sends `25` first; if the PC can't be reached the app says to `/phone forget` there.
  - `Pocket` (app-wide state + On/Off), `Notes` (channel "pocket", low importance; OFF notification id 2 with
    "Turn on", re-posted when swiped via its delete intent; ON notification id 1 = the service's, with "Talk" and
    "Turn off"), `Actions` (the buttons' receiver), `PocketService` (foreground, type connectedDevice, no mic:
    pings every 5 min, first one at once; 3 misses in a row = turns itself off), `PocketTile` (on/off tile, opens
    the app when it can't turn on), `TalkActivity` (translucent card: listens at once, sends, shows the answer,
    closes; tap card = again), `Ear` (SpeechRecognizer with partial results + mic level).
  - `MainActivity` is drawn from state (`render()`): setup steps 1 (PC) / 2 (pair) until paired; then PC card with
    the On switch, a Talk button that glows with the mic level while listening, the live transcript, the last
    answer, typing, history; a status chip (not paired / listening / pairing / talking to Loomy / connecting /
    connected / can't reach PC / on / off). Battery-optimization exemption asked once when first turned on.
  - `Store.on` + `PocketService.running`; `Pocket.fixStaleOn` resets "on" if the process died without onDestroy.
- Not yet: always-listening (wake word), auto-off after idle time.
- **Signing:** dev builds (branches) use `pocket/dev.keystore` (committed on purpose, password `pocketdev`,
  applicationId suffix `.dev`, so dev and release install side by side). Release builds (tags only) use his key
  from GitHub secrets `POCKET_KEYSTORE_B64` + `POCKET_KEYSTORE_PASSWORD` (alias `pocket`); without them the tag
  build skips the APK with a warning instead of failing.
- **Workflow** `.github/workflows/pocket.yml` (he adds it; the bot can't touch workflows): builds the dev APK on
  pushes to `claude/**` touching `pocket/`, `src/pocket.*` or `src/monocypher*` (artifact "pocket-loomy-dev"), and
  on `v*` tags builds the signed APK and uploads `pocket-loomy.apk` to that release. Actions are pinned by SHA.
  The bot can see each run's step results through the API (`/actions/runs`, `/jobs`), but **not logs or
  artifacts**: those redirect to `*.blob.core.windows.net`, which the sandbox can't reach. For error text, ask him
  to paste it (or to allow that domain in the chat's network settings). First run (21:57, Oct 30): built fine in 2 min.
- **Local checks** (sandbox can't build an APK): `tests/jni/run.sh` (JDK + kotlinc from the JetBrains GitHub
  release) runs the app's real `Native.kt`/`Words.kt` + `pocket_jni.c` on a desktop JVM against a PC-role engine
  (44 checks). Type-check the app with kotlinc against an `android.jar` (e.g. github.com/Reginer/aosp-android-jar,
  android-35): `kotlinc -cp android.jar *.kt`.

## 12. QUEUED: his feedback after testing 2.18 (Oct 30, not started; he's out of usage until ~4am)
Build these next on `claude/v2.18` (still a draft; not ended):
1. App settings section: toggle the notification on/off.
2. App setting "keep listening": after Talk, stay listening (run "show", keep listening) until he stops the mic
   from the notification or the app. Needs a microphone foreground service (green dot stays on).
3. App setting: the bottom Talk card from the notification is optional, so with 2 he can use Loomy hands-free
   while using the phone normally, no UI popping up.
4. When a command opens a panel on the PC (e.g. `si`), the reply lists the choices (views/tabs/rows) so the app
   can show them; he can't memorise them.
5. "input <key>" from the phone: `input back`, `input next`, `input next tab` (= Tab), etc. Sends that key to
   Loomy's open panel only (not system-wide keystrokes).
6. Running a new command closes the open panel first (si open + "help" -> back to the main screen, then help).
7. Policy change (he decided): **any plain (non-/) command is allowed from the phone by default** (help was
   blocked and annoyed him). **Slash commands need a separate security step before they're allowed** — that's a
   LATER build, not 2.18. Until then slash stays refused.
8. Chaining: "system info AND input next tab" runs both in order.
9. Net speed: a toggle that turns the notification into a net-speed indicator (raw up/down, B/KB/MB per second,
   no "Loomy" title), like the status-bar speed apps. Assumed: the phone's own traffic (TrafficStats, no INTERNET
   permission needed), the number drawn into the status-bar icon. Confirm phone vs PC speed with him.
