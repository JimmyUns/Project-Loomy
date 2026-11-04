# Ellune

Loomy's online sister: the hosted server side that Loomy clients talk to. It runs on Cloudflare Workers (free plan).
Right now it answers a ping and checks its guest list. The plan and the decisions so far (including the security
talk) are in `../ELLUNE-HANDOVER.md`.

## What it does
- `GET /ping` answers `{"ok":true,"service":"ellune","version":"0.2"}`, to anyone.
- `GET /me` is for devices on the **guest list**: a signed request from a listed device gets
  `{"ok":true,"guest":true,"name":"..."}`, anything else a 401 with the reason.
- `POST /hear` (guest list): a WAV clip (16 kHz mono 16-bit, at most 10 s) → `{"ok":true,"heard":"..."}` (Whisper).
- `POST /understand` (guest list): `{"text":"make the screens go dark"}` → `{"ok":true,"command":"display off"}`
  (`""` when it isn't a Loomy command; only commands from a fixed list ever come out).
- `GET /pc?room=R`, `GET /phone?room=R` (guest list, WebSockets): remote. The PC waits in its room, the phone joins
  it for a session, Ellune passes their (end-to-end encrypted) bytes on untouched. `GET /status?room=R` says whether
  the PC is there. R comes from the pairing (both ends work it out themselves).
- Anything else is a 404 (or 405 for the wrong method). No storage, no secrets, nothing logged; audio and text only
  go to Workers AI for the answer.

## The AI binding (for /hear and /understand)
The Worker → **Settings** → **Bindings** → **Add binding** → **Workers AI**, variable name `AI`. Without it those
two answer 503 and Pocket Loomy uses the phone's own recognizer. Free plan: 10,000 neurons a day; a 3 s command
is about 2 for Whisper and about 3 when the AI has to pick the command, so thousands of commands a day.

## Adding devices with Authy (0.5)
A new phone ("Add to guest list") or PC (`/remote add 123456`) adds itself with the current Authy code; its request
is signed by its own key, so nobody can add someone else's. Ellune keeps those in her own storage (the `Guests`
object); a code works once; 5 wrong codes lock it for an hour. The app's **Devices on Ellune** lists everyone and
removes the ones added this way (with a code). Setup, once, in this folder:
1. `node totp-setup.mjs`: makes the secret on your PC and shows it.
2. Authy: **Add account** → **Enter key manually** → that key, name it Ellune.
3. `npx wrangler secret put TOTP_SECRET` → paste the same key.
4. `npx wrangler deploy`.

## The guest list (`DEVICES`, the backup)
Each device has its own key; the list holds only the public halves, so nothing in it is secret. In Pocket Loomy,
**Add to guest list** copies a line like `K7P2-9QXD=BOr0...`. In the dashboard: the Worker → **Settings** →
**Variables and Secrets** → add a **Text** variable named `DEVICES` with the line as its value (more devices: one
line each). Changing it redeploys by itself. Removing a line throws that device out.
How requests are signed is described at the top of `src/index.js`.

## Deploy (since 0.4: wrangler)
0.4 adds the relay rooms for remote, a Durable Object, and those can't be set up by pasting code into the dashboard.
Once, on the PC: install Node (nodejs.org, LTS). Then in this folder: `npx wrangler login` (a browser asks you to
allow it), then `npx wrangler deploy`. It keeps the `DEVICES` list (`keep_vars`) and sets up the `AI` binding and
the `ROOMS` rooms itself (`wrangler.toml`). Each later update: `npx wrangler deploy` again.

## Deploy (dashboard, no installs: only up to 0.3)
1. Cloudflare dashboard → **Workers & Pages** → **Create** → start from the **Hello World** Worker.
2. Name it `ellune` and deploy it. The first time, Cloudflare asks you to pick your `workers.dev` subdomain.
3. Open the Worker → **Edit code**, replace everything with `src/index.js`, and **Deploy**.
4. Check it: open `https://ellune.<your-subdomain>.workers.dev/ping` in a browser. You should see `"ok":true`.

## Deploy (wrangler, needs Node)
From this folder: `npx wrangler login` once, then `npx wrangler deploy`. `keep_vars = true` in `wrangler.toml` keeps
the `DEVICES` list you set in the dashboard.

## Test
`node test.mjs` (Node 18+) runs the Worker's handler locally, guest list included. `tests/jni/run.sh` also checks
that a request signed by the phone's real Kotlin code passes.
