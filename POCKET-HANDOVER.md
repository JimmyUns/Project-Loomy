# Pocket Loomy — Handover (PC side v2.17, Android app v2.18, pairing confirm + link split v2.19)

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
  neighbours on the Wi-Fi can't see it. Range ≈ one room, which fits "near my PC". (Later option: relay via Ellune for
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
- **Pairing confirm (2.19, his idea):** after the right code (PK_EV_PAIRED on the PC) Loomy does NOT send its key yet.
  `ask_confirm()` opens `/phone` with the phone's claimed name, address and **ID** (`pk_fingerprint` of the phone's
  new public key: SHA-512 of "LoomyPocket1 id" + key, 40 bits, Crockford base32 "XXXX-XXXX"); the phone shows the
  same ID (`pk_awaiting_pc` + JNI `phoneId`). Enter = `accept_pairing` (save trust, flush the held E_KEY_PC);
  Esc / 60 s / phone hang-up = `refuse_pairing` (drop, nothing saved, no lockout; the code is spent anyway). No
  protocol change: the PC just holds its last message. The phone waits up to 80 s for it.
- **Links (2.19):** `PhoneLink` in loomy.h (listen/unlisten/listening/send/drop); `PHONE_BT` in phonebt.c is the only
  one; phone.c keeps `LINKS[]` and remembers which link the connected phone came through (`g_via`). On the phone,
  `Pipe` / `BluetoothPipe` in Link.kt. An internet relay = one more link/pipe, pocket.c untouched.
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
  Kotlin 2.0.21, Gradle 8.14.3, JDK 17. Package `io.github.jimmyuns.pocketloomy`. **INTERNET permission since 2.20, only for `Ellune.kt`**
  (tap-to-ping, one HTTPS GET, `usesCleartextTraffic="false"`; he OK'd it Nov 2); commands stay Bluetooth-only;
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
  - `MainActivity` is drawn from state (`render()`). **Redesigned in 2.22** (his brief, see §11): title + status
    chip + a gear (`GearView`) on top; the latest history line in a pill under it (tap: the whole history,
    newest first, over the screen); `MicView` in the middle (round mic button, halftone dots rippling out, calm
    idle / swelling with the voice / pulsing while sending; paused behind the layers); the live text; the mode
    button (Say "Loomy" / Solo, a dialog); typing. Until paired, setup steps 1 (PC) / 2 (pair) sit where the mic is.
    The gear opens Settings over the screen: Your PC (name, paired until, On switch, Change PC, Forget), Listening
    (the two modes + the "Use the phone's recognizer" checkbox), Ellune (ID, Ping, Add to guest list), App
    (Notification, Talk card, Net speed). Back closes the layers. Battery exemption asked once when first turned on.
  - `Store.on` + `PocketService.running`; `Pocket.fixStaleOn` resets "on" if the process died without onDestroy.
- Keep-listening with the "yo/ayo Loomy" greeting is built (§12). Not yet: auto-off after idle time.
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

- **Ellune ping (2.20):** an "Ellune" box on the main screen (works unpaired): **Ping Ellune** → `Ellune.ping`
  (own thread, `HttpsURLConnection`, 8 s timeouts, no redirects, reads ≤512 bytes) → same three answers as `/ellune`
  on the PC. `Ellune.check` mirrors `ellune_check()` (needs `"ok":true` + `"service":"ellune"`, body must be a JSON
  object) and is tested in `tests/jni` (plain Java, no Android classes; 176 checks now). Not on voice yet. He tested it
  (Nov 2): works, 150-700 ms, usually ~400.

- **Ellune guest list (2.21):** `ElluneKey.kt` = this phone's own P-256 key in the Android Keystore (alias
  `ellune_device`, sign only, made on first use, separate from the 7-day pairing keys). `Ellune.kt` signs requests
  (`signedHeaders`: DER from Java turned into raw r||s by `derToRaw`), `deviceId` (40-bit Crockford like the pairing
  ID), `guestLine` (`ID=base64url`), `meNow` (GET /me). Ellune box: the ID, **Ping Ellune** (ping, then /me: on the
  list or not), **Add to guest list** (copies the line + a dialog saying where it goes, Share to send it to the PC).
  tests/jni: 184 checks + a signed request handed to `ellune/test.mjs` (the Worker's real code verifies it).

- **Ellune listening (2.21, reworked 2.22):** `ElluneEar.kt` records itself (AudioRecord, VOICE_RECOGNITION
  source, 16 kHz mono; 4 tries if the recognizer still holds the mic), `Vad` in `Sound.kt` finds start/end (noise
  floor, 60 ms to start, 1 s quiet ends, 6 s wait, 10 s max, 300 ms kept each side), `Sound.wav` →
  `Ellune.hearNow` → `heard(listOf(text))`: the same pipeline as Android's recognizer, so names, keys, controls
  work unchanged. **Which ear** (`Voice.wantEllune`): Ellune only for the sentence after her name (armed, also
  right after a mic tap) and in Solo; never while waiting for "Loomy" (stays on the phone) and never in input
  mode. Needs the checkbox unticked (`Store.phoneEar`, default false), a key and validated internet
  (`ElluneEar.online`; ACCESS_NETWORK_STATE). **2.22, his call: no auto switching.** If Ellune fails: a history
  note ("nothing was sent, say it again") and she's tried again next time (`elluneDownUntil` is gone). Offline (or
  no key yet): the phone's recognizer, and `partial` says so. **AI:** `Words.knows(cmd)` false → `understandNow`
  → sent as is ("said → sent"); NONE = a miss; if she can't answer, an error and nothing sent (no falling back to
  the rules' guess). tests/jni + the phone's /hear and /understand requests checked by the Worker's code.
- **Listening modes (2.22):** `Store.solo` false = **Say "Loomy"**, true = **Solo** (everything is a command, no
  wake/sleep any more: `soloAwake` and the SLEEP behaviour are gone). The mic always keeps listening until tapped
  again (`Store.keepListening` is gone; Talk from the notification / card keeps listening too). A tap arms the
  first sentence (no name needed for 8 s). In both, `Words.untilThanks` cuts each heard text at "thank you" /
  "thanks" (whole words): before it is the command, after it is dropped, a bare "thank you" does nothing. "please"
  is only a filler (LEAD/TAIL), never an end. Told him: the pause is what ends the recording ("thank you" can't
  cut it instantly, Whisper gets the clip after you stop). By voice "solo mode" / "solo mode off" still switch.
- **2.22 part 1, his feedback round 1 (Nov 2):** (1) settings had checkboxes for the mode while the main screen
  had a dropdown → Settings uses the same mode button. (2) Wording: "Loomy is listening", Ellune only named while
  she works ("Ellune is understanding"), and then the mic is **off with a loading ring** (`Voice.thinking`: set by
  `ElluneEar`'s `busy` callback during /hear, and by `understand()`, which now waits for her answer before
  listening again). (3) "The connection switch is buggy": turning On failed and the reason was printed on the main
  screen, hidden behind Settings, so the switch just flipped back → the reason now shows in red under the switch
  + a toast (`onProblem`). If it still won't stay on, get that message from him. (4) Background: faint grid lines
  fading out to the edges (`GridDrawable` in MicView.kt, accent at alpha 22, 26 dp cells). (5) **His main issue:
  Say "Loomy" didn't catch her name** (the recognizer writes "lumi", "lumie", "loomino"...): `Words.isName` now
  also takes anything shaped like l+oo/u/ou/o+m+a short ending (`NAMEISH`, not "loom"/"lome"), and "lou me" /
  "lu mi" as two words (`joinName`). And **the state must be visible, not read**: MicView modes IDLE (dark, slow),
  WAITING (dark, ring breathing = listening for her name), LISTENING (purple, white ring, dots swell with the voice
  = taking the command), THINKING (dimmed, spinning ring = Ellune / sending), plus `flash()` green/red ring + ripple
  when an answer comes back. `Voice.takingCommand()` decides WAITING vs LISTENING. Live text is 2-3 words; partial
  transcripts only show while taking the command (no more random words while waiting). 225 checks.
- **2.22 feedback round 2 (Nov 2), his calls:** **online first.** `Store.btMode` (default false = **Ellune mode**):
  Bluetooth isn't checked at all (`Pocket.ready` only checks the pairing; `readyBt` holds the Bluetooth part, used
  in Bluetooth mode and for pairing / forget, which stay Bluetooth). **Bluetooth mode** = the Bluetooth button
  (`BtIconView`, top right of the mic area): Bluetooth only, Ellune ignored (phone recognizer, no /hear,
  /understand), and the theme animates purple → blue (`#5A8BFF`; `themers` re-colour everything drawn in the theme
  colour, 650 ms). Only he switches modes; nothing switches by itself. **The bar on top = the connection only**
  (`Net.kt`, his five states): Offline / Ellune offline / PC offline / Online / Online BT; checked every 30 s while
  the app is open (60 s in Bluetooth mode, a real Bluetooth ping), on tap, and by the service every 5 min when On
  (Ellune mode: no Bluetooth pings). **Until remote exists, Ellune mode can't deliver commands**: `Voice.send` says
  "comes with the next step (remote), tap the Bluetooth button", and the bar tops out at "PC offline". **Grid**: a
  Visuals slider (`Store.gridOpacity`, default 50 % = line alpha 50 %; it was 9 %, "too dim"). **Solo picked up
  typing / noises**: `Vad` now needs 80 ms to start and, to count as speech, ≥ 8 loud *voiced* frames (zero-crossing
  rate < 0.25: vowels; keyboard taps and knocks are crackly) → else `NOISE`, never uploaded; `Words.isNoise` drops
  Whisper's noise outputs ("[typing]", "you", "Thanks for watching!"); in Solo, anything that isn't a command (rules
  don't know it / the AI says NONE) is dropped quietly, no red miss. 244 checks.
- **Remote (2.22), the app side:** `EllunePipe.kt` (a tiny WebSocket client: SSLSocket + hostname check, the
  upgrade with the guest-list headers on `/phone?room=R`, `Ws.kt` frames: masked out, ping answered, "nopc" →
  `NoPcException`) is a `Pipe` like `BluetoothPipe`. `Link.viaEllune` (set by `Pocket.syncPipe` before every send:
  Ellune mode → the relay, Bluetooth mode → null) picks the pipe; **pairing always opens Bluetooth**. Switching
  modes drops the kept connection. `Net` asks `/status?room=R` for "Online" vs "PC offline". The room =
  `Ellune.room(trust.pcPk)` (same vector as `el_room` on the PC). Notes (misses) go through either pipe now.
  tests/jni: 256 checks (frames, accept key, room). PC side: `phonenet.c` / `ellune.c`, see `ELLUNE-HANDOVER.md` §2.
- **/remote panel redrawn simpler (2.22, his ask, "a lot of confusing text"):** title + listening; one line `[x] Bluetooth
  [x] Remote (Ellune)` with `PC W6VV-D6NH` at the right; one short Ellune line (`net_status`); then ONE thing: the
  confirm (ID big), shut down, the paired phone (name + connected / last seen + paired until), or the QR on the left
  with the code / time left / waiting on the right (QR sized to fit), or "No phone paired. N to pair one." Keys:
  short. C still copies the guest line but isn't advertised (use `/remote add <Authy code>`).
- **QR pairing fixes (Nov 4):** the PC hung up remote mid-confirm (the code is used up the moment it's right, so
  "no code, no phone" → unlisten): `update_listener` now never unlistens the link a phone is on (`g_conn && g_via
  == l`). App setup screen follows the mode (his ask): Ellune mode = "Pair with your PC" + **Scan the QR** (opens
  the camera app, `INTENT_ACTION_STILL_IMAGE_CAMERA`; the QR link brings it back, the step shows the progress / ID);
  Bluetooth mode = Your PC + Pair with the code **said or typed** (`typedCode`). The Bluetooth button sits on top
  of the setup too.
- **Last two asks for 2.22 (Nov 4):** (1) **power button**: `PowerView` small at the top left while On (tap = off);
  while Off the mic is replaced by a big beating `PowerView` (tap = on), the small one hidden. (2) **the mic in Ellune
  mode**: tap while listening = send now (`Voice.tap`: Ellune's ear `finishNow()` sends from where talking started, or
  the last 10 s; the phone's ear waiting for her name: the tap arms, Ellune takes the next sentence), listening
  carries on; **hold 800 ms = mute** (a ring fills round the button, `MicView.onHold`). Bluetooth mode: tap starts /
  stops like before, hold also mutes. No anti-spam (his call).
- **v2.22 (in progress):** part 1 built (the redesign, the two modes, the checkbox: above). Next: password
  registration, then remote. Full list in `HANDOVER.md` §6. The notification stays as it is (only the solo wording
  in it changed: "Solo mode: listening").

## 11b. Where it's heading (agreed Nov 1, not started)
- **Remote from anywhere** (e.g. at work: "open Steam" on the home laptop) through **Ellune as a blind relay**: the same
  end-to-end encrypted pocket.c bytes, Ellune can't read, forge or replay them; the PC connects OUT to Ellune (no open
  ports). Pairing stays Bluetooth-only, in person, with the PC-side confirm. Extra for remote later: a smaller remote
  allowlist, every remote command shown on the PC, a remote on/off switch, fingerprint lock in the app (he said not
  yet), a saved-apps launcher for "open steam" (he said not yet). Laptop must be awake.
- **Ellune as the brain** for free speech / his accent: Whisper speech-to-text + a small LLM mapping what he said to a
  command; the phone keeps detecting "Loomy" locally and sends only what follows. The phone stays the one holding
  the keys, so the AI can suggest a command but never send one. Needs Ellune first (its ping step).

## 12. His feedback after testing the first 2.18 build — DONE (Oct 31), all items below are built
(Plus two he added: "screen on/off" in any wording -> `display off|on`, and talking like a human with a
"yo/ayo Loomy" greeting in keep-listening mode.) How it's built:
- PC: `cmd_phone_ok` = any plain command (the `.phone` flag is gone); `run_from_phone` splits on " and " only where a
  command/input/open/hide follows ("lp salt and pepper" stays whole); `run_one` closes the open panel first
  (`ui_close_panels`), runs it, appends `ui_panel_choices` (sysinfo/display/web list theirs; others a generic hint);
  `phone_input` maps words to keys (`KEYS[]` in phone.c, trailing count "down 3"/"down three") and sends them through
  `ui_panel_key` (a real WM_KEYDOWN into wnd_proc; Shift faked via `ui_shift()`, which sysinfo uses), only while a
  panel is open. `help` from the phone replies with `cmd_phone_help` (a short list), the table still prints on the PC.
  `display on` = `ui_screens_on` (1-pixel mouse nudge there and back). PK_TEXTMAX is 480 now.
- App: `Words` (greeting detection `addressed()`, fillers, screen phrases, `press`/natural key phrases -> `input`,
  "open X", slash, spoken names, "gpu one", per-"and"-part cleanup); `Voice` (the listening loop, in the service);
  `PocketService` owns the one notification and runs pings / listening / the speed meter with matching FGS types;
  `Notes` (speed icon bitmap, "pocket" + "pocket_min" channels); `TalkActivity` follows `Voice` (or vanishes when
  the card is off); `Boot` brings the speed meter back after reboot/update; settings in `MainActivity`.
- Known: Google's recognizer beeps each time it starts listening; in keep-listening that's every few seconds. Not
  fixed (would mean muting system sounds). Ask him if it bothers him.

Round 3 (Oct 31): net speed under 1 KB/s shows 0; "height" (his accent) = "hide" (`Words.one`); **input mode**
("ayo Loomy input mode", greeting optional for on/off): bare key words via `Words.key()` -> "input ..." until "input
mode off" / "disable input mode" (`Voice.inputMode`; non-keys still ignored); new icon (`ic_launcher_bg/fg/mono.xml`,
adaptive + themed). He found the first one (prompt + sound wave) too techy/console, and a plain L + dot grid "dumb". Final
(his design): the L in the middle, halftone dots along its left and bottom, biggest next to the letter and
fading out (`pocket/tools/icon_dots.py` places them). Remember Android only shows the middle 72 of the 108 canvas. JNI/word test: 92 checks.

Round 4 (Oct 31): **input from the phone never worked on Windows**: `ui_panel_key` sent WM_KEYDOWN to the main
window, but panels take keys in `edit_proc` (the edit box). Fixed (send to `g_edit`). The Linux shim dispatches
straight to the panels, so tests can't catch routing like that; keep it in mind. Voice: her **name alone** is the
wake word now (`Words.addressed`: the name must open the utterance, after optional fillers/greetings, or follow a
greeting); the name said alone (or last) arms an 8 s follow-up, also for a single Talk tap (`Voice.followUp`) -
fixes the recognizer cutting "Loomy | show" in two. Recognizer: longer end-of-speech silence extras + Android 13
biasing strings (`Words.HINTS`). Key words work with the name any time ("Loomy down"), bare in input/solo mode.
**Solo mode** (`Store.solo`, setting + "Loomy solo mode"/"solo mode off"): no name needed. He asked about better
recognition: the real step up would be an offline engine with a fixed command grammar (Vosk: ~40-50 MB model + its
library, continuous, no beeps, much more reliable for a small vocabulary). Not done; offered to him. 112 word checks.

Round 5 (Oct 31): `Words.key` reads several keys in one breath ("up up right confirm" -> "input up 2 and input
right and input confirm"; every word must be a key/count/filler or it's not keys); the PC takes up to 16 chained
parts and only the last one describes the screen (`g_lastPart`). "off"/"on" alone = hide/open. Phone-side controls
in `Voice.control`: `Words.isDisconnect` ("stop (the) connection", "disconnect"...) = `Pocket.turnOff`;
`Words.isStop` ("mute", "ok that's it", "stop listening"...) = stop listening. Both need her name (or solo / a Talk
tap / following her name), except in input mode. 130 word checks.

Round 6 (Oct 31, last for 2.18): the On switch's label was always "On" (and the dark theme's switch colours made
off look on): label follows the state, all switches tinted (purple on / grey off). **Phrases.kt**: the library of
ways to say things (COMMANDS, STARTS, KNOWN, KEYS, MUTE, SLEEP, DISCONNECT, mode on/off sets); `Words` does the
cleanup and looks phrases up there; `Words.plain` is the cleaned phrase used for switches/controls. "open <word>"
that isn't a command = `web <word>`. Solo mode is now a session: her name wakes it (`Voice.soloAwake`), SLEEP
phrases ("ok thank you", "thanks", "stop listening") put it back to waiting for her name, mic stays on. MUTE
(mic off) is multi-word only ("turn off the mic", "stop listening" outside solo). tests/jni/run.sh now fails
loudly if kotlinc fails (it silently reused an old jar once). 161 checks.
He asked whether he needs a small LLM for free talk: answered no for now (phrase library + rules; the bigger win
would be a grammar-limited offline recognizer like Vosk).

The original list:
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
   He named the model: the "Internet Speed Meter" style app by Nisarg Jhaveri, so ONE notification instead of
   several stuck ones. Must work on its own with Pocket Loomy off: then no Bluetooth, no pings, no mic, just a
   1 s TrafficStats read + icon redraw, **paused while the screen is off** (nobody sees it), so it's ~free.
