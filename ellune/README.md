# Ellune

Loomy's online sister: the hosted server side that Loomy clients talk to. It runs on Cloudflare Workers (free plan).
Right now it only answers a ping. The plan and the decisions so far are in `../ELLUNE-HANDOVER.md`.

## What it does
`GET /ping` answers `{"ok":true,"service":"ellune","version":"0.1"}`. Anything else is a 404 (or 405 for other
methods on `/ping`). No storage, no secrets, nothing logged.

## Deploy (dashboard, no installs)
1. Cloudflare dashboard → **Workers & Pages** → **Create** → start from the **Hello World** Worker.
2. Name it `ellune` and deploy it. The first time, Cloudflare asks you to pick your `workers.dev` subdomain.
3. Open the Worker → **Edit code**, replace everything with `src/index.js`, and **Deploy**.
4. Check it: open `https://ellune.<your-subdomain>.workers.dev/ping` in a browser. You should see `"ok":true`.

## Deploy (wrangler, needs Node)
From this folder: `npx wrangler login` once, then `npx wrangler deploy`.

## Test
`node test.mjs` (Node 18+) runs the Worker's handler locally.
