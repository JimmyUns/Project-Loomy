# Ellune

Loomy's online sister: the hosted server side that Loomy clients talk to. It runs on Cloudflare Workers (free plan).
Right now it answers a ping and checks its guest list. The plan and the decisions so far (including the security
talk) are in `../ELLUNE-HANDOVER.md`.

## What it does
- `GET /ping` answers `{"ok":true,"service":"ellune","version":"0.2"}`, to anyone.
- `GET /me` is for devices on the **guest list**: a signed request from a listed device gets
  `{"ok":true,"guest":true,"name":"..."}`, anything else a 401 with the reason.
- Anything else is a 404 (or 405 for the wrong method). No storage, no secrets, nothing logged.

## The guest list (`DEVICES`)
Each device has its own key; the list holds only the public halves, so nothing in it is secret. In Pocket Loomy,
**Add to guest list** copies a line like `K7P2-9QXD=BOr0...`. In the dashboard: the Worker → **Settings** →
**Variables and Secrets** → add a **Text** variable named `DEVICES` with the line as its value (more devices: one
line each). Changing it redeploys by itself. Removing a line throws that device out.
How requests are signed is described at the top of `src/index.js`.

## Deploy (dashboard, no installs)
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
