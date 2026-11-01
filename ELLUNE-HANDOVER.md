# Ellune — Handover (ping server written in `ellune/`, not deployed yet; Nov 1 2026)

Brief for a fresh chat. Ellune is the online sister of **Loomy**. Loomy's `HANDOVER.md` (same repo, same folder)
covers who Jimmy is, how he likes to work, the workflow, and the Loomy codebase: read that first. Keep Ellune notes
here, not in Loomy's handover. This repo is public: nothing secret in here.

## 1. What Ellune is
**Ellune** (renamed from ELLA, "Encrypted Locker for Loomy Accounts", on Nov 1 2026: ELLA + *lune*, French for moon;
the idea is that data goes up to the moon and comes back). Loomy is the local assistant; Ellune is the hosted server side that
Loomy clients talk to. The long-term idea (not decided in detail yet): save, move and share files between Loomy
clients, plus online saves of user data and presets, with an account and password per Loomy.

Jimmy has no hardware to run a server at home, so Ellune runs on rented hosting. He knows there are security
questions and wants to talk security **later**, as its own conversation. Don't bake in security decisions without him.

## 2. Status
- **Decided Nov 1:** the code lives in **`ellune/`** in this repo (same bot + PR flow); the ping command is
  **`/ellune`** in **core** mode; **no domain** for now (the free `workers.dev` address).
- **He made a Cloudflare account** (free plan). Nothing deployed yet.
- **`ellune/` (v2.20 branch):** the Worker (`src/index.js`, plain JS, no dependencies). `GET /ping` →
  `200 {"ok":true,"service":"ellune","version":"0.1"}`, `cache-control: no-store` so Loomy's timing is a real
  round trip; other methods on `/ping` → 405, other paths → 404. `node test.mjs` checks it locally. Deploy steps
  (dashboard or `wrangler`) are in `ellune/README.md`. He deploys; we never hold a Cloudflare token.
- **Next:** he deploys and sends the `workers.dev` URL → build `/ellune` in Loomy (v2.20) with that URL baked in
  (it's not a secret).
- Loomy is at **v2.19** (Pocket Loomy, the Android app, talks to Loomy over Bluetooth today; see
  `POCKET-HANDOVER.md`). Its connection is already split from its security so an internet path can plug in (§6).
- **First step he picked: a simple ping.** Loomy sends a request to Ellune, Ellune answers, Loomy shows that Ellune is up
  (and how long it took). That proves the whole path (hosting, deploy, Loomy talking to it) before any real features.
- **Still open:** the full feature list (he hasn't decided).
- **Plan he agreed (Nov 1):** account → Worker → he deploys → `/ellune` in Loomy (v2.20) → he tests on his PC and
  the work laptop (its network may block it) → optionally the app pings too (needs the INTERNET permission: his
  call) → the security talk → then A or B from §6.

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
security conversation he wanted (§1) is still due: A's design above is the core of it, nothing else is decided.
