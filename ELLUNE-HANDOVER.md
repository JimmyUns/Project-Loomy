# Ellune — Handover (live 0.3, 0.4 relay written not deployed; security talk §7; remote being built; Nov 2 2026)

Brief for a fresh chat. Ellune is the online sister of **Loomy**. Loomy's `HANDOVER.md` (same repo, same folder)
covers who Jimmy is, how he likes to work, the workflow, and the Loomy codebase: read that first. Keep Ellune notes
here, not in Loomy's handover. This repo is public: nothing secret in here.

## 1. What Ellune is
**Ellune** (renamed from ELLA, "Encrypted Locker for Loomy Accounts", on Nov 1 2026: ELLA + *lune*, French for moon;
the idea is that data goes up to the moon and comes back). Loomy is the local assistant; Ellune is the hosted server side that
Loomy clients talk to. The long-term idea (not decided in detail yet): save, move and share files between Loomy
clients, plus online saves of user data and presets, with an account and password per Loomy.

Jimmy has no hardware to run a server at home, so Ellune runs on rented hosting. He knows there are security
questions and wanted a security talk as its own conversation: **done Nov 2 2026, decisions in §7.** Anything §7
doesn't cover is still his call: don't bake it in without him.

## 2. Status
- **Decided Nov 1:** the code lives in **`ellune/`** in this repo (same bot + PR flow); the ping command is
  **`/ellune`** in **core** mode; **no domain** for now (the free `workers.dev` address).
- **Live (Nov 1):** `https://ellune.loomybot.workers.dev/ping` (Worker `ellune`, account subdomain `loomybot`, free
  plan). He deployed it through the dashboard (Create app → Start with Hello World → Edit code → paste
  `src/index.js` → Deploy) and checked it in his browser. **The dashboard holds the deployed copy:** when
  `ellune/src/index.js` changes, he has to paste and deploy it again (no GitHub hookup; he wasn't asked yet, and it
  would give Cloudflare access to his GitHub: security-talk topic).
- **`ellune/` (v2.20 branch):** the Worker (`src/index.js`, plain JS, no dependencies). `GET /ping` →
  `200 {"ok":true,"service":"ellune","version":"0.1"}`, `cache-control: no-store` so Loomy's timing is a real
  round trip; other methods on `/ping` → 405, other paths → 404. `node test.mjs` checks it locally. Deploy steps
  (dashboard or `wrangler`) are in `ellune/README.md`. He deploys; we never hold a Cloudflare token.
- **Loomy side (v2.20):** `/ellune` (slash, core mode) in `update.c` next to `https_get`; host/path are
  `ELLUNE_HOST` / `ELLUNE_PING` there. Shows "Pinging Ellune...", then "Ellune is up (v0.1, N ms)" (time includes
  DNS + TLS, fresh connection each time), "Couldn't reach Ellune." (no internet / blocked), or "Something answered,
  but it wasn't Ellune." The reply check `ellune_check()` is in `version.c` (needs `"ok":true` and
  `"service":"ellune"`, so a captive portal's 200 doesn't count) and is unit-tested.
- **Tested Nov 2 on his PC:** up, 63-300 ms after the first run (1735 ms: DNS + proxy check). v2.20 is PR #19.
- **App ping (v2.20, Nov 2):** he said yes to the INTERNET permission. Pocket Loomy has an Ellune box with **Ping
  Ellune** (`pocket/.../Ellune.kt`, `POCKET-HANDOVER.md` §11). Tested Nov 2: works, 150-700 ms, usually ~400.
- **v2.21, step 1 of the brain: the guest list** (Nov 2). Worker 0.2: `GET /me` checks a signed request against the
  `DEVICES` variable (`name=base64url raw P-256 key`, one per line, set in the dashboard; `keep_vars` in
  wrangler.toml). Signed text and headers documented at the top of `src/index.js`; 60 s clock window, no storage
  (a replay inside 60 s would need TLS broken first). `node test.mjs`: 26 checks + the cross-check from tests/jni.
  App side: `POCKET-HANDOVER.md` §11. **Deployed + tested Nov 2:** he pasted 0.2 and added `DEVICES` (a plain Text variable, not
  Secret, so he can see/edit it); the app says the phone is on her guest list. (First try he saved it empty, and he
  asked whether to copy only the ID from the dialog title: the button already copies the whole line.)
- **Steps 2+3 built (Nov 2), Worker 0.3:** `POST /hear` (signed; WAV 16 kHz mono 16-bit, 0.1-10 s, `wavOk`; Whisper
  large-v3-turbo with `language: en`, `vad_filter`, an `initial_prompt` full of Loomy words) and `POST /understand`
  (signed; `{"text"}` ≤ 300 chars; llama-3.1-8b-instruct-fp8-fast, temperature 0, a prompt listing Loomy's commands
  + examples; `cleanCommand` only lets out commands matching a fixed pattern (no slash commands, max 4 joined with
  "and"), else ""). Body read once with a size cap before the signature check (keys are public). Needs a **Workers
  AI binding named `AI`** (503 without it). `node test.mjs`: 58 checks with a stand-in AI. App side: `POCKET-HANDOVER.md`
  §11. **Deployed + tested by him Nov 2 (0.3 + the `AI` binding + the new APK): works.**
- **Remote, being built (v2.22, started Nov 2; he said go).** Staged; each stage committed:
  1. **Done: Ellune 0.4, the relay** (`src/index.js`, class `Room`, a Durable Object; `wrangler.toml` now has the
     `ROOMS` binding + `new_sqlite_classes` migration + the `AI` binding). `GET /pc?room=R` (PC, WebSocket),
     `GET /phone?room=R` (phone, WebSocket, one session), `GET /status?room=R` (`{"pc":bool}`), all guest-list
     signed **with the query in the signed path**. R = hex(SHA-256("Loomy room" + the PC's pairing static public
     key))[0..32]: both ends know it from the Bluetooth pairing, nothing new to exchange, a re-pair = a new room.
     The room passes bytes (pocket.c frames, end-to-end encrypted) both ways untouched; text = signals ("phone" /
     "gone" to the PC, "nopc" to the phone, "drop" from the PC; "ping" auto-answered "pong" without waking it).
     One PC + one phone per room; replaced sockets are marked so their close doesn't signal twice. Frames > 1024
     bytes close the sender. `node test.mjs`: 82 checks (fake DO runtime). **Deploy needs wrangler** (Durable
     Objects can't be pasted in): he needs Node, the repo, `npx wrangler login`, `npx wrangler deploy` in `ellune/`.
  2. **Done (not run on Windows yet): the PC.** Rename `/phone` → **`/remote`** (his call) with checkboxes inside: **Allow Bluetooth**,
     **Allow remote** (+ anything else important). The PC's own Ellune key (CNG ECDSA P-256 via bcrypt, private
     blob DPAPI-protected in `%APPDATA%\Loomy\ellune.dat`), its ID + guest line shown in `/remote` with copy to
     clipboard (paste into `DEVICES` once; password registration later). `PHONE_NET` in a new `phonenet.c`: a
     WinHTTP WebSocket to `/pc?room=R` on a worker thread, reconnecting with backoff, pinging "ping" every 60 s,
     posting to the UI thread; "phone" → `phone_on_accept(&PHONE_NET, ...)`, bytes → `phone_on_data`, "gone" →
     `phone_on_closed`; `drop` sends "drop". Listens only when a phone is trusted and Allow remote is on. Junk
     through the relay = the normal lockout of everything (his §7 choice).
  2b. As built: `ellune.c` (portable: SHA-256, base64url, `el_device_id`, `el_guest_line`, `el_room`,
     `el_signed_text`; Linux-tested against the same vectors as the phone) + `phonenet.c` (Windows: CNG key in
     `ellune.dat`, WinHTTP WebSocket on a worker thread, WM_PHONENET to the UI thread, keepalive 60 s, backoff 5 s /
     15 s / 1 min / 2 min, 5 min after a 401). Config `remote_bluetooth` / `remote_ellune` (both default on); keys B /
     R / C in `/remote`. Cross-compiles clean with mingw (`build.sh`).
  3. **Then the app:** a tiny WebSocket client (no libraries: SSLSocket, masked client frames) as an `EllunePipe`
     next to `BluetoothPipe` in Link.kt, used in Ellune mode; `Net` asks `/status` so the bar can say Online /
     PC offline; `Voice.send` stops saying "comes with the next step".
- **Next, v2.22 (agreed Nov 2):** master-password registration, then remote. Full list in `HANDOVER.md` §6.
- **Plan he OK'd for v2.21 (Nov 2), "the brain" (done):** 1 guest list (done, above) → 2 `/hear` on Ellune: audio →
  Whisper → small LLM picks a Loomy command (Workers AI, free 10k neurons/day; Whisper ~45 neurons per audio minute,
  so a 3 s command ≈ 2) → 3 the app sends audio when online, falls back to the local recognizer offline or if Ellune
  is slow. Catch told to him: Android's recognizer won't hand over audio, so the app records itself; in
  keep-listening, "Loomy" + pause → audio goes up, "Loomy open steam" in one breath → the recognizer's text goes up
  instead. Then v2.22: remote (reuses the guest list). I call them "remote" and "the brain" with him now, not A/B.
- Loomy is at **v2.20** (Pocket Loomy, the Android app, talks to Loomy over Bluetooth today; see
  `POCKET-HANDOVER.md`). Its connection is already split from its security so an internet path can plug in (§6).
- **First step he picked: a simple ping.** Loomy sends a request to Ellune, Ellune answers, Loomy shows that Ellune is up
  (and how long it took). That proves the whole path (hosting, deploy, Loomy talking to it) before any real features.
- **Still open:** the full feature list (he hasn't decided).
- **Plan he agreed (Nov 1):** account → Worker → he deploys → `/ellune` in Loomy (v2.20) → he tests on his PC (done; the
  work laptop is out of scope now) → the app pings too (built in v2.20, he OK'd the INTERNET
  permission) → the security talk → then A or B from §6.

## 3. Hosting (researched Oct 2026, recheck prices before deciding)
- **Recommended to start: Cloudflare (serverless).** Write only the logic; Cloudflare runs, patches and keeps it up.
  Free plan, roughly: Workers 100,000 requests/day; R2 file storage 10 GB with free downloads; D1 database
  (SQLite) for accounts/lists. Uploads are capped at 100 MB per request on the free plan (bigger files = chunking).
  Server code would be JavaScript/TypeScript (a second codebase, not C).
- **Alternative: a small VPS** (e.g. Hetzner, ~€4/month after their April 2026 price rise). Full control, but he'd be
  responsible for updates, backups and security. Can move here later if Cloudflare gets limiting.
- The sandbox can't reach `api.cloudflare.com` (not in its allowed domains), so deploying from chat won't work as-is.
  Deploys would be done by him (e.g. Cloudflare dashboard or `wrangler` on his PC) unless he adds the domain.
  Any deploy token would follow Loomy's spirit: nothing ships without him.

## 4. What to expect (told to him already)
- **Possible:** accounts with passwords; cloud saves (web lists, batches, presets, settings) loadable on any PC;
  a personal file locker; sending a file to another Loomy user's inbox; share links that expire or are user-limited.
- **Hard / off the table:** instant "you got a file" pings while Loomy is hidden (Loomy idles at zero cost; it would
  check on open, or on a slow timer, which costs lightness); huge files (chunking, storage past free tier costs a bit);
  live collaboration. Holding other people's files makes him responsible for them.
- **Work laptop:** company rules apply to moving files/data off it. Never help get around workplace security.
- **Direct laptop-to-laptop without a server:** fine on the same wifi (but Windows' firewall prompt likely needs
  admin on the work laptop). Over the internet, routers block incoming connections, so a middle server is needed
  anyway: that's why Ellune exists.

## 5. Loomy-side notes for when the ping gets built
- Loomy already does HTTPS with WinHTTP in `update.c` (`https_get`, with 5 s DNS/connect/send and 10 s read timeouts,
  on the UI thread). Reuse it; call `ui_flush()` before the request so "Pinging Ellune..." shows first.
- Keep Loomy tiny: the server does the heavy lifting, Loomy just makes small requests.
- Nothing secret goes in the exe (anyone can read it). Real auth comes with the security conversation.

## 6. What Pocket Loomy needs from Ellune (agreed with him, Nov 1 2026; not started)
Background: Pocket Loomy (`pocket/`, Android) sends commands to Loomy on his PC over Bluetooth, end-to-end
encrypted with Loomy's own protocol (`src/pocket.c`, spec in `POCKET-HANDOVER.md` §9). Two things need a server.
Full context: `POCKET-HANDOVER.md` §11b.

**A. Remote from anywhere: Ellune as a blind relay.** His goal: at work, open the app and tell the laptop at home to
do something (e.g. open Steam so games update). Bluetooth can't reach that far.
- Ellune only passes along the same encrypted pocket.c bytes. It can't read them, can't forge them (no keys) and
  can't replay them (counter nonces). A fully hacked Ellune can block messages, never run anything on his PC.
- The PC connects OUT to Ellune and waits (no open ports, no router changes). Only while remote is switched on.
- Pairing stays Bluetooth-only and in person, with the PC-side confirm (phone name + ID, Enter). A phone can only
  use the relay after that. Nobody can pair over the internet.
- Hooks already in the code (v2.19): on the PC `PhoneLink` (loomy.h; `PHONE_BT` in phonebt.c, `LINKS[]` in phone.c);
  in the app `Pipe` / `BluetoothPipe` (Link.kt). The relay = one more PhoneLink + one more Pipe. pocket.c stays as is.
- Ideas for remote later (not decided): a smaller remote allowlist, every remote command shown on the PC with
  "remotely", a remote on/off switch, a fingerprint lock in the app (he said not yet), a saved-apps launcher for
  "open steam" (he said not yet). The laptop has to be awake.
- Cloudflare note: a relay needs connections that stay open (WebSockets). Workers can do that with Durable Objects;
  check what the free plan allows before choosing.

**B. Ellune as the brain for his speech.** Android's recognizer struggles with his accent (Lebanese, often taken for
Scandinavian), and he doesn't want to memorise exact commands.
- Whisper (speech to text, good with accents) + a small LLM that maps what he said to one of Loomy's commands (and
  fixes mishearings from context). Cloudflare Workers AI runs both with a free daily allowance (check limits).
- The phone keeps listening for "Loomy" locally and sends only what follows, never his whole conversation.
- The phone stays the only one with the keys: Ellune suggests the command, the phone sends it (over Bluetooth or the
  relay). So the AI can never send anything to his PC on its own.
- The app would need the INTERNET permission for this (today it has none). Tell him when that changes.

**Order he agreed:** Ellune's ping first (§2: hosting, deploy, Loomy and the app reaching it), then A or B. The
security conversation happened Nov 2 (§7). Still open: which of A or B comes first.

## 7. Security decisions (the security talk, Nov 2 2026, all his calls)
How it went: he wanted plain explanations (post office analogy: a hacked Ellune holds locked boxes it can't open
or fake). He agreed these are the normal risks of any connected app. Told him honestly: pocket.c is our own
protocol from standard parts (Monocypher, CPace), heavily tested but never reviewed by an outside expert.
- **Accounts:** 2FA (Authy) is ON for both GitHub and Cloudflare (Nov 2). Told him: recovery codes off the phone,
  Authy backups on, Authy "multi-device" off after setup. GitHub matters most (it controls what `/update` installs).
- **Deploys stay manual:** he pastes `ellune/src/index.js` into the dashboard. No GitHub to Cloudflare auto-deploy,
  no Cloudflare token held by us. Nothing changes on Ellune without him.
- **Guest list:** Ellune only serves *his devices*, each with its own key from the in-person Bluetooth pairing. No
  app-wide secret (the repo is public, APKs unpack). Strangers get turned away before anything reaches the PC.
  `/ping` stays open to anyone. Even rejected requests count toward the free 100k/day (spam = Ellune down for the
  day: annoying, not dangerous).
- **A (relay):** as in §6. Remote commands = **the same as over Bluetooth** (today's set is mild: nothing deletes or
  installs). Every remote command is marked "remotely" on the PC. **The on/off switch is on the PC**: the PC only
  connects out to Ellune while remote is on.
- **Junk (lockout):** junk = broken/fake *encrypted* messages, not words. Junk arriving over the relay **cuts off
  everything** (Bluetooth too), same as today's lockout; he sorts it out when he's home. (I suggested remote-only;
  he chose everything.) Speech that isn't a command is never junk: it stays a note.
- **Solo mode over the internet: unchanged** (I suggested name-first over remote; he said solo is for when he's
  alone, keep it).
- **B (brain):** the phone **trusts Ellune's answer** and sends it; no second guessing on the phone (his call). The
  PC's own command rules are the safety net (it refuses anything that isn't a real command). The app shows
  "heard X -> sent Y".
- **B, what goes up:** **if the phone has internet, send the audio clip** to Ellune (Whisper, for his accent), even
  when the PC link is Bluetooth. **No internet: fall back to Android's local recognizer** and say so in the app
  (less accurate, set phrases only, no free speech). My addition (tell him when building): also fall back if Ellune
  doesn't answer within a few seconds. He's OK with the audio going to Cloudflare's AI.
- **Risks named to him, not acted on yet:** a stolen unlocked phone can send commands for the 7-day pairing (remote
  switch on the PC helps; fingerprint lock he said not yet); bugs in our code (hostile-input rules cover it).
