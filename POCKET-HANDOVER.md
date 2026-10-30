# Pocket Loomy — Handover (design agreed, nothing built)

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
  `/phone` panel), `cpace.c`. Expect **+25–35 KB** on the exe. Link `ws2_32` (already) — check `WSASetService`.
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

## 7. Suggested order
1. PC: feature `phone` (opt-in), Monocypher vendored, CPace + handshake + framing with tests, `/phone` panel, lockout.
2. PC: Bluetooth listener (`WSAAsyncSelect`), allowlist, replies. Release.
3. Android: Bluetooth connect + pairing by voice, send a command, show the reply. Then the notification/tile/
   always-listening polish, auto-off, battery exemption.

## 8. Open questions for him
- Exact phone-safe command list (start from §4).
- Wake phrase details ("Loomy run …" vs just "Loomy …"), and whether tap-to-talk comes first.
- APK signing/distribution (debug sideload vs release key in GitHub secrets).
