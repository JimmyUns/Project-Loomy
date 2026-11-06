// Ellune: Loomy's online sister. Runs on Cloudflare Workers.
//
// GET /ping  ->  200 {"ok":true,"service":"ellune","version":"0.2"}   (open to anyone)
// GET /me    ->  200 {"ok":true,"guest":true,"name":"K7P2-9QXD"}      (guest list only, see below)
// POST /hear  (guest list) body: a WAV clip, 16 kHz mono 16-bit, at most 10 s
//             ->  200 {"ok":true,"heard":"loomy turn the screens off"}  (Whisper, via the AI binding)
// POST /understand (guest list) body: {"text":"make the screens go dark"}
//             ->  200 {"ok":true,"command":"display off"}  ("" when it isn't a Loomy command)
// GET /pc?room=R     (guest list, WebSocket) the PC connects OUT and waits for its phone      \  remote (0.4):
// GET /phone?room=R  (guest list, WebSocket) the phone joins its PC's room for one session      >  the relay,
// GET /status?room=R (guest list)            -> {"ok":true,"pc":true}  is the PC connected?   /  see Room below
// POST /register    {"code":"123456","name":"phone-J000-NN38"}  signed by the NEW key itself: with the current
//                   Authy code a device puts itself on the guest list (0.5, his idea: no copy-pasting)
// GET  /devices     (guest list) -> {"devices":[{"name","key","from":"app"|"dashboard"}]}
// POST /remove      {"key":"...","code":"123456"} (guest list): throw a device out (one added from the app)
// GET  /prefs       (guest list) -> {"ok":true,"prefs":{"pkg.scrcpy.file.x64":"scrcpy-win64-*.zip"}}       \ 0.7: small
// POST /prefs       {"key":"...","value":"..."} (guest list), "" removes it                               / shared settings
// POST /game        {"q":"league"} (guest list) -> {"ok":true,"name":"League of Legends","kind":"riot","time":ms|null,
//                   "version":"...","since":ms|null}: when the game last updated (0.8; only when asked, no alerts).
//                   Steam: looked up on the spot (store search -> api.steamcmd.net's public build time).
//                   Riot (League/TFT, Valorant): Riot only says the current version, so a cron every 15 min writes
//                   down when it changes (the "logbook", in the Guests object); "since" = when the logbook started.
// anything else on a known path -> 405, any other path -> 404.
// No storage, no logging of who asked or what was said. Audio and text only go to Cloudflare's Workers AI
// (it doesn't train on them) and are gone after the answer. Needs a Workers AI binding named AI
// (dashboard -> the Worker -> Settings -> Bindings -> Add -> Workers AI); without it /hear and /understand
// answer 503 and the phone falls back to its own recognizer.
//
// The guest list: Ellune only serves Jimmy's own devices. Each device has its own key pair (on the phone it
// lives in Android's Keystore and can't be copied out). The public halves are listed in the Worker's DEVICES
// variable (Cloudflare dashboard -> the Worker -> Settings -> Variables), one per line as name=key:
//     K7P2-9QXD=BOr0...   (key = base64url of the raw P-256 public key, 65 bytes starting 04)
// A device signs every request (ECDSA P-256 + SHA-256, signature as raw r||s, base64url) over:
//     "ellune1\n" + METHOD + "\n" + path + "\n" + time + "\n" + hex(sha256(body))
// and sends x-ellune-key, x-ellune-time (unix ms) and x-ellune-sig. Requests more than 60 s off are refused, so
// an old request can't be sent again later. The list is public data (public keys only), nothing secret here.

const VERSION = "0.8";
const SKEW_MS = 60_000;

function reply(status, body, extra = {}) {
  return new Response(JSON.stringify(body), {
    status,
    headers: {
      "content-type": "application/json; charset=utf-8",
      // never cached, so the time Loomy measures is a real round trip
      "cache-control": "no-store",
      ...extra,
    },
  });
}

// ---------------------------------------------------------------- Authy (TOTP, RFC 6238: 6 digits, 30 s)
// The secret is a Cloudflare Secret (TOTP_SECRET, base32), made on his PC by totp-setup.mjs and added to Authy
// there: it never passes through anything else. A code works once (the last used time-step is kept), one step
// either side is allowed for clock drift, and 5 wrong codes lock registration and removal for an hour.
function base32(s) {
  const A = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
  const clean = String(s || "").toUpperCase().replace(/[\s=-]/g, "");
  let bits = 0, v = 0;
  const out = [];
  for (const ch of clean) {
    const i = A.indexOf(ch);
    if (i < 0) return null;
    v = (v << 5) | i; bits += 5;
    if (bits >= 8) { out.push((v >>> (bits - 8)) & 255); bits -= 8; }
  }
  return out.length >= 10 ? new Uint8Array(out) : null;
}

export async function totp(secret, step) {
  const key = await crypto.subtle.importKey("raw", secret, { name: "HMAC", hash: "SHA-1" }, false, ["sign"]);
  const msg = new Uint8Array(8);
  let c = step;
  for (let i = 7; i >= 0; i--) { msg[i] = c % 256; c = Math.floor(c / 256); }
  const h = new Uint8Array(await crypto.subtle.sign("HMAC", key, msg));
  const o = h[19] & 15;
  const n = ((h[o] & 127) << 24 | h[o + 1] << 16 | h[o + 2] << 8 | h[o + 3]) % 1000000;
  return String(n).padStart(6, "0");
}

// the time-step a code belongs to (now, or one either side), if it's newer than the last one used; else 0
export async function codeStep(secretB32, code, now, lastStep) {
  const secret = base32(secretB32);
  if (!secret || !/^\d{6}$/.test(String(code || ""))) return 0;
  const t = Math.floor(now / 30000);
  for (const step of [t, t - 1, t + 1]) if (step > lastStep && (await totp(secret, step)) === code) return step;
  return 0;
}

// One object holds the devices added with a code, the lockout and the last used step (one instance, so it's all
// atomic). Its own storage; Ellune keeps nothing else.
export class Guests {
  constructor(ctx, env) { this.ctx = ctx; this.env = env; }
  async fetch(request) {
    const what = new URL(request.url).pathname;
    const st = this.ctx.storage;
    const devices = (await st.get("devices")) || {};
    if (what === "/list") return reply(200, { ok: true, devices });
    let b = {};
    try { b = await request.json(); } catch {}
    // 0.7: small settings all his devices share (which file of a package to download...). No code needed: only
    // guest-list devices get here, like every other guest call.
    // 0.8: the Riot logbook (the cron writes, /game reads)
    if (what === "/riot-log") {
      const log = (await st.get("riot")) || {};
      const cur = log[b.product], t = Number(b.now) || Date.now();
      if (!cur) log[b.product] = { url: b.url, version: b.version || "", changed: null, since: t, checked: t };
      else if (cur.url !== b.url) log[b.product] = { url: b.url, version: b.version || "", changed: t, prev: cur.changed, since: cur.since, checked: t };
      else cur.checked = t;
      await st.put("riot", log);
      return reply(200, { ok: true, entry: log[b.product] });
    }
    if (what === "/riot-get") return reply(200, { ok: true, log: (await st.get("riot")) || {} });
    if (what === "/prefs-get") return reply(200, { ok: true, prefs: (await st.get("prefs")) || {} });
    if (what === "/prefs-set") {
      const prefs = (await st.get("prefs")) || {};
      if (b.value === "") delete prefs[b.key];
      else {
        if (!(b.key in prefs) && Object.keys(prefs).length >= 50) return reply(413, { ok: false, error: "50 settings at most" });
        prefs[b.key] = b.value;
      }
      await st.put("prefs", prefs);
      return reply(200, { ok: true });
    }
    const now = Number(b.now) || Date.now();
    if (!this.env || !this.env.TOTP_SECRET) return reply(503, { ok: false, error: "registration isn't set up (no TOTP_SECRET)" });
    const lock = (await st.get("lock")) || { fails: 0, until: 0 };
    if (now < lock.until) return reply(429, { ok: false, error: "locked for an hour after 5 wrong codes" });
    const step = await codeStep(this.env.TOTP_SECRET, b.code, now, (await st.get("last")) || 0);
    if (!step) {
      lock.fails++;
      if (lock.fails >= 5) { lock.fails = 0; lock.until = now + 3600000; }
      await st.put("lock", lock);
      return reply(403, { ok: false, error: lock.until > now ? "wrong code: locked for an hour" : "wrong code" });
    }
    await st.put("last", step);
    await st.put("lock", { fails: 0, until: 0 });
    if (what === "/register") devices[b.key] = { name: b.name, added: now };
    else if (what === "/remove") delete devices[b.key];
    await st.put("devices", devices);
    return reply(200, { ok: true });
  }
}

let cache = { at: 0, devices: {} };                // the added devices, kept a minute per Worker instance
async function added(env, now) {
  if (!env || !env.GUESTS) return {};
  if (cache.at && now - cache.at < 60000) return cache.devices;
  try {
    const r = await guestsStub(env).fetch(new Request("https://guests/list"));
    const j = await r.json();
    cache = { at: now, devices: j.devices || {} };
  } catch {}
  return cache.devices;
}
const guestsStub = (env) => env.GUESTS.get(env.GUESTS.idFromName("guests"));
export function resetGuestCache() { cache = { at: 0, devices: {} }; }

// ---------------------------------------------------------------- the guest list
const B64URL = /^[A-Za-z0-9_-]+$/;
const NAME = /^[A-Za-z0-9._-]{1,32}$/;

function b64urlBytes(s) {
  if (typeof s !== "string" || !B64URL.test(s) || s.length > 200) return null;
  const pad = s.replace(/-/g, "+").replace(/_/g, "/") + "===".slice((s.length + 3) % 4);
  try {
    const bin = atob(pad);
    const out = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
    return out;
  } catch {
    return null;
  }
}

// DEVICES -> Map(key string -> name). Bad lines are skipped, never fatal.
export function guestList(text) {
  const list = new Map();
  for (const raw of String(text || "").split(/[\s,]+/)) {
    const eq = raw.indexOf("=");
    if (eq < 1) continue;
    const name = raw.slice(0, eq), key = raw.slice(eq + 1);
    const bytes = b64urlBytes(key);
    if (!NAME.test(name) || !bytes || bytes.length !== 65 || bytes[0] !== 4) continue;
    list.set(key, name);
  }
  return list;
}

async function sha256hex(buf) {
  const h = new Uint8Array(await crypto.subtle.digest("SHA-256", buf));
  return Array.from(h, (b) => b.toString(16).padStart(2, "0")).join("");
}

// The body, but never more than max bytes (null if it's bigger): keys are public, so anyone can name a listed
// key and send a huge body before the signature check fails. Read it once, here.
async function readCapped(request, max) {
  if (!request.body) return new Uint8Array();
  const reader = request.body.getReader();
  const parts = [];
  let n = 0;
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    n += value.length;
    if (n > max) { try { await reader.cancel(); } catch {} return null; }
    parts.push(value);
  }
  const out = new Uint8Array(n);
  let o = 0;
  for (const p of parts) { out.set(p, o); o += p.length; }
  return out;
}

// The guest's name, or a short reason it isn't one. body = the bytes from readCapped.
async function guest(request, env, path, now, body) {
  const key = request.headers.get("x-ellune-key");
  const time = request.headers.get("x-ellune-time");
  const sig = request.headers.get("x-ellune-sig");
  if (!key || !time || !sig) return { why: "not signed" };
  let name = guestList(env && env.DEVICES).get(key);
  if (!name) { const a = (await added(env, now))[key]; if (a) name = a.name; }
  if (!name) return { why: "not on the guest list" };
  return sigCheck(request, path, now, body, key, time, sig, name);
}

// the signature on a request, by `key` (listed or, for /register, the key asking to be listed)
async function sigCheck(request, path, now, body, key, time, sig, name) {
  if (!/^\d{1,15}$/.test(time) || Math.abs(now - Number(time)) > SKEW_MS) return { why: "clock off (over 60 s)" };
  const sigBytes = b64urlBytes(sig);
  if (!sigBytes || sigBytes.length !== 64) return { why: "bad signature" };

  const signed = `ellune1\n${request.method}\n${path}\n${time}\n${await sha256hex(body)}`;
  try {
    const pub = await crypto.subtle.importKey("raw", b64urlBytes(key), { name: "ECDSA", namedCurve: "P-256" }, false, ["verify"]);
    const ok = await crypto.subtle.verify({ name: "ECDSA", hash: "SHA-256" }, pub, sigBytes, new TextEncoder().encode(signed));
    return ok ? { name } : { why: "bad signature" };
  } catch {
    return { why: "bad signature" };
  }
}

// ---------------------------------------------------------------- listening (the brain)
const MAX_WAV = 16000 * 2 * 10 + 44;               // 10 s of 16 kHz mono 16-bit, plus the header
const WHISPER = "@cf/openai/whisper-large-v3-turbo";
const LLM = "@cf/meta/llama-3.1-8b-instruct-fp8-fast";
// Context for Whisper: her name and Loomy's words, so it writes "Loomy" and "display extend", not "Looney".
const HINT = "Loomy, display extend. Loomy, turn the screens off. Loomy, system info, cpu, gpu. Loomy, recycle bin. " +
             "Loomy, next tab, previous tab, back, confirm, input mode, solo mode. Okay thank you.";

// Is this a WAV we'll take: RIFF/WAVE, PCM, mono, 16 kHz, 16-bit? (anything else is refused, never decoded)
export function wavOk(u8) {
  if (!u8 || u8.length < 44 + 3200 || u8.length > MAX_WAV) return false;   // 0.1 s .. 10 s
  const t = (i, s) => String.fromCharCode(...u8.subarray(i, i + 4)) === s;
  const dv = new DataView(u8.buffer, u8.byteOffset, u8.byteLength);
  return t(0, "RIFF") && t(8, "WAVE") && t(12, "fmt ") && dv.getUint16(20, true) === 1 && dv.getUint16(22, true) === 1 &&
         dv.getUint32(24, true) === 16000 && dv.getUint16(34, true) === 16 && t(36, "data");
}

function b64(u8) {
  let s = "";
  for (let i = 0; i < u8.length; i += 0x8000) s += String.fromCharCode.apply(null, u8.subarray(i, i + 0x8000));
  return btoa(s);
}

// The words Loomy knows, for the AI. Kept in step with Pocket Loomy's Phrases.kt and Loomy's commands.c.
const COMMANDS = `You turn what someone said to their PC assistant "Loomy" into ONE Loomy command. Reply with only the command, or NONE.
Commands:
open | hide - show or hide Loomy's window
help - list what Loomy can do
lp [folder] - Loomy's path: the user's folders of shortcuts (lp games)
web [alias] - open a saved website by its alias (web youtube); web alone lists them
display extend - use both screens (extend across them)
display pc - ONLY the first / main / PC / laptop screen (the second one goes dark)
display second - ONLY the second / other / external screen (the first one goes dark)
display duplicate - the same picture on both screens (mirror)
display off | display on - turn the screens off or on
net - what's using the internet
rb - open the recycle bin
si | si general | si cpu | si gpu - system info: si alone opens the list, or name the view (si gpu 2 for a second GPU)
github - open Loomy's GitHub page
scrcpy - show the user's phone screen on the PC (mirror the phone, a package)
input <key> - press a key on Loomy's open panel. Keys: up, down, left, right, next, previous, next tab, previous tab, back, enter, select, ok, confirm, cancel, yes, close, home, end, page up, page down. A count can follow: input down 3
Several commands: join with " and " (si and input next tab).
Examples:
make the screens go dark -> display off
wake the monitors up -> display on
how hot is my graphics card -> si gpu
open youtube -> web youtube
put it on both screens -> display extend
show me only the first monitor -> display pc
just my main screen -> display pc
turn off the second monitor -> display pc
switch to the second -> display second
only the other screen -> display second
open the general system information -> si general
mirror my phone -> scrcpy
i plugged my phone in, put it on the screen -> scrcpy
show me the processor stats -> si cpu
scroll down a few -> input down 3
go one tab over -> input next tab
what's the weather -> NONE
call my mom -> NONE`;

const KEYS = "up|down|left|right|next|previous|next tab|previous tab|back|enter|select|ok|confirm|cancel|yes|close|home|end|page up|page down";
const ONE = new RegExp("^(?:open|hide|help|net|rb|github|scrcpy|lp(?: [a-z0-9 ._'-]{1,40})?|web(?: [a-z0-9._-]{1,40})?|" +
  "display(?: (?:extend|second|pc|duplicate|off|on))?|si(?: (?:general|cpu|gpu(?: [1-9])?))?|" +
  `input (?:${KEYS})(?: [1-9])?)$`);

// What the AI said -> a command we're willing to pass on, or "" (the PC still checks it again itself)
export function cleanCommand(said) {
  let c = String(said || "").trim().split("\n")[0].toLowerCase().replace(/^command:\s*/, "").replace(/[`"'.]+$/g, "").replace(/^[`"']+/, "").trim();
  if (!c || c === "none" || c.length > 120) return "";
  const parts = c.split(" and ").map((p) => p.trim());
  if (parts.length > 4 || !parts.every((p) => ONE.test(p))) return "";
  return parts.join(" and ");
}

async function hear(body, ai) {
  if (!wavOk(body)) return reply(400, { ok: false, error: "not a 16 kHz mono WAV under 10 s" });
  try {
    const r = await ai.run(WHISPER, { audio: b64(body), language: "en", initial_prompt: HINT, vad_filter: true,
                                      condition_on_previous_text: false });
    const heard = String((r && r.text) || "").replace(/\s+/g, " ").trim().slice(0, 300);
    return reply(200, { ok: true, heard });
  } catch {
    return reply(503, { ok: false, error: "ai busy or out of free use" });
  }
}

async function understand(body, ai) {
  let text = "";
  try { text = String(JSON.parse(new TextDecoder().decode(body)).text || ""); } catch { /* not json */ }
  text = text.replace(/[\u0000-\u001f]+/g, " ").trim();
  if (!text || text.length > 300) return reply(400, { ok: false, error: "text missing or over 300 characters" });
  try {
    const r = await ai.run(LLM, { messages: [{ role: "system", content: COMMANDS }, { role: "user", content: text }],
                                  max_tokens: 24, temperature: 0 });
    return reply(200, { ok: true, command: cleanCommand(r && r.response) });
  } catch {
    return reply(503, { ok: false, error: "ai busy or out of free use" });
  }
}

// ---------------------------------------------------------------- remote: the relay (a blind post office)
// R = a room name both ends already know from their Bluetooth pairing: hex of SHA-256("Loomy room" + the PC's
// pairing public key), first 32 characters. Ellune learns nothing from it, and a new pairing means a new room.
// Inside a room: one PC socket and at most one phone socket; bytes from one go to the other, untouched. They're
// Pocket Loomy's own end-to-end encrypted frames (pocket.c): Ellune can't read, change or replay them, only pass
// them on or not. Text messages are the room's own signals:
//   to the PC:    "phone" (a phone joined: start a session, say HELLO), "gone" (it left)
//   to the phone: "nopc" (the PC isn't connected; then closed), the PC leaving closes the phone too
//   from the PC:  "drop" (hang up on the phone), "ping" (answered "pong" without waking the room)
// Uses a Durable Object (ROOMS binding, SQLite class, free plan) with WebSocket hibernation, so an idle PC
// connection costs nothing.
const ROOM = /^[a-f0-9]{32}$/;
const MAX_FRAME = 1024;                            // pocket.c frames are at most 514 bytes

export class Room {
  constructor(ctx, env) {
    this.ctx = ctx;
    this.env = env;
    try { ctx.setWebSocketAutoResponse(new WebSocketRequestResponsePair("ping", "pong")); } catch {}
  }

  // overridable in tests (Node can't make a 101 response)
  upgraded(client) { return new Response(null, { status: 101, webSocket: client }); }
  pair() { const p = new WebSocketPair(); return [p[0], p[1]]; }

  quietClose(ws, code, why) {
    try { ws.serializeAttachment({ replaced: true }); } catch {}
    try { ws.close(code, why); } catch {}
  }

  async fetch(request) {
    const role = request.headers.get("x-ellune-role");
    const pcs = this.ctx.getWebSockets("pc");
    if (role === "status") return reply(200, { ok: true, pc: pcs.length > 0 });
    if (request.headers.get("Upgrade") !== "websocket") return reply(426, { ok: false, error: "websocket only" });
    const [client, server] = this.pair();
    if (role === "pc") {
      for (const old of pcs) this.quietClose(old, 4000, "another PC connection took over");
      for (const p of this.ctx.getWebSockets("phone")) this.quietClose(p, 4003, "pc reconnected");
      this.ctx.acceptWebSocket(server, ["pc"]);
      return this.upgraded(client);
    }
    // a phone: one at a time, and only if the PC is there
    for (const old of this.ctx.getWebSockets("phone")) {
      this.quietClose(old, 4001, "another phone session took over");
      for (const pc of pcs) try { pc.send("gone"); } catch {}
    }
    this.ctx.acceptWebSocket(server, ["phone"]);
    if (pcs.length === 0) { try { server.send("nopc"); } catch {} this.quietClose(server, 4004, "pc offline"); }
    else for (const pc of pcs) try { pc.send("phone"); } catch {}
    return this.upgraded(client);
  }

  webSocketMessage(ws, msg) {
    const isPc = this.ctx.getTags(ws).includes("pc");
    if (typeof msg === "string") {
      if (isPc && msg === "drop") for (const p of this.ctx.getWebSockets("phone")) this.quietClose(p, 4002, "the pc hung up");
      return;                                       // anything else in text is ignored
    }
    if (msg.byteLength > MAX_FRAME) { this.quietClose(ws, 1009, "too big"); if (!isPc) this.tellPcGone(); return; }
    for (const to of this.ctx.getWebSockets(isPc ? "phone" : "pc")) try { to.send(msg); } catch {}
  }

  tellPcGone() { for (const pc of this.ctx.getWebSockets("pc")) try { pc.send("gone"); } catch {} }

  webSocketClose(ws) { this.closed(ws); }
  webSocketError(ws) { this.closed(ws); }

  closed(ws) {
    let a = null;
    try { a = ws.deserializeAttachment(); } catch {}
    if (a && a.replaced) return;                    // we closed it on purpose and already said so
    if (this.ctx.getTags(ws).includes("pc")) {
      for (const p of this.ctx.getWebSockets("phone")) this.quietClose(p, 4003, "pc gone");
    } else {
      this.tellPcGone();
    }
  }
}

async function relay(request, env, url, path, now) {
  if (request.method !== "GET") return reply(405, { ok: false, error: "method not allowed" }, { allow: "GET" });
  const room = url.searchParams.get("room") || "";
  if (!ROOM.test(room) || url.search !== `?room=${room}`) return reply(400, { ok: false, error: "bad room" });
  const body = await readCapped(request, 0);
  if (!body) return reply(413, { ok: false, error: "too big" });
  const g = await guest(request, env, path + url.search, now, body);
  if (!g.name) return reply(401, { ok: false, guest: false, error: g.why });
  if (!env || !env.ROOMS) return reply(503, { ok: false, error: "no rooms binding" });
  if (path !== "/status" && request.headers.get("Upgrade") !== "websocket") return reply(426, { ok: false, error: "websocket only" });
  // a fresh request: only what the room needs, so nobody outside can set the role
  const headers = { "x-ellune-role": path.slice(1) };
  if (path !== "/status") headers.Upgrade = "websocket";
  const stub = env.ROOMS.get(env.ROOMS.idFromName(room));
  return stub.fetch(new Request("https://room/" + path.slice(1), { headers }));
}

// ---------------------------------------------------------------- registration
async function register(request, env, path, now) {
  if (request.method !== "POST") return reply(405, { ok: false, error: "method not allowed" }, { allow: "POST" });
  const body = await readCapped(request, 1024);
  if (!body) return reply(413, { ok: false, error: "too big" });
  if (!env || !env.GUESTS) return reply(503, { ok: false, error: "no guests binding" });
  let b = {};
  try { b = JSON.parse(new TextDecoder().decode(body)); } catch {}
  const key = request.headers.get("x-ellune-key") || "";
  const kb = b64urlBytes(key);
  if (!kb || kb.length !== 65 || kb[0] !== 4) return reply(400, { ok: false, error: "bad key" });
  if (!NAME.test(String(b.name || ""))) return reply(400, { ok: false, error: "bad name" });
  // the request must be signed by the key being added: nobody can add someone else's key
  const g = await sigCheck(request, path, now, body, key, request.headers.get("x-ellune-time"), request.headers.get("x-ellune-sig"), b.name);
  if (!g.name) return reply(401, { ok: false, error: g.why });
  const r = await guestsStub(env).fetch(new Request("https://guests/register", { method: "POST",
    body: JSON.stringify({ key, name: b.name, code: String(b.code || ""), now }) }));
  resetGuestCache();
  return r;
}

async function devices(request, env, path, now) {
  const isList = path === "/devices";
  if (request.method !== (isList ? "GET" : "POST")) return reply(405, { ok: false, error: "method not allowed" });
  const body = await readCapped(request, 1024);
  if (!body) return reply(413, { ok: false, error: "too big" });
  const g = await guest(request, env, path, now, body);
  if (!g.name) return reply(401, { ok: false, guest: false, error: g.why });
  if (!env || !env.GUESTS) return reply(503, { ok: false, error: "no guests binding" });
  if (isList) {
    const out = [];
    for (const [key, name] of guestList(env.DEVICES)) out.push({ name, key, from: "dashboard" });
    for (const [key, d] of Object.entries(await added(env, now))) out.push({ name: d.name, key, from: "app" });
    return reply(200, { ok: true, devices: out });
  }
  let b = {};
  try { b = JSON.parse(new TextDecoder().decode(body)); } catch {}
  const r = await guestsStub(env).fetch(new Request("https://guests/remove", { method: "POST",
    body: JSON.stringify({ key: String(b.key || ""), code: String(b.code || ""), now }) }));
  resetGuestCache();
  return r;
}

const PREF_KEY = /^[a-z0-9][a-z0-9._-]{0,59}$/;
const PREF_VAL = /^[A-Za-z0-9._*+() -]{0,95}$/;

async function prefs(request, env, path, now) {
  const get = request.method === "GET";
  if (!get && request.method !== "POST") return reply(405, { ok: false, error: "method not allowed" });
  const body = await readCapped(request, 1024);
  if (!body) return reply(413, { ok: false, error: "too big" });
  const g = await guest(request, env, path, now, body);
  if (!g.name) return reply(401, { ok: false, guest: false, error: g.why });
  if (!env || !env.GUESTS) return reply(503, { ok: false, error: "no guests binding" });
  if (get) return guestsStub(env).fetch(new Request("https://guests/prefs-get", { method: "POST", body: "{}" }));
  let b = {};
  try { b = JSON.parse(new TextDecoder().decode(body)); } catch {}
  const key = String(b.key || ""), value = String(b.value ?? "");
  if (!PREF_KEY.test(key) || !PREF_VAL.test(value)) return reply(400, { ok: false, error: "bad key or value" });
  return guestsStub(env).fetch(new Request("https://guests/prefs-set", { method: "POST", body: JSON.stringify({ key, value }) }));
}

// ---------------------------------------------------------------- 0.8: when did a game last update (his ask)
// Only when he asks; nothing is ever pushed to him. Riot has no public "when", so the cron below keeps a logbook.
const UA = "Ellune/0.8 (personal assistant for one user; checks game versions every 15 minutes)";
export const RIOT = {
  league_of_legends: { name: "League of Legends", says: /^(league( of legends)?|lol|tft|teamfight tactics|league of legend)$/ },
  valorant: { name: "VALORANT", says: /^(valorant|val|valorent|valerant)$/ },
};

// Riot's public patchline config -> the live Windows build: {url, version} or null
export function riotLive(json, product) {
  const live = json && json[`keystone.products.${product}.patchlines.live`];
  const cfgs = live && live.platforms && live.platforms.win && live.platforms.win.configurations;
  const cfg = Array.isArray(cfgs) && cfgs[0];
  const url = cfg && cfg.patch_url;
  if (typeof url !== "string" || !/^https:\/\//.test(url) || url.length > 400) return null;
  const v = cfg.version || (cfg.metadata && cfg.metadata.version) || "";
  return { url, version: typeof v === "string" ? v.slice(0, 40) : "" };
}

async function riotNow(product, fetchFn) {
  try {
    const r = await fetchFn(`https://clientconfig.rpg.riotgames.com/api/v1/config/public?namespace=keystone.products.${product}.patchlines`,
                            { headers: { "user-agent": UA, accept: "application/json" } });
    return r.ok ? riotLive(await r.json(), product) : null;
  } catch { return null; }
}

// the cron (wrangler.toml [triggers]): every 15 min, each Riot game's live build into the logbook
export async function riotTick(env, now = Date.now(), fetchFn = fetch) {
  if (!env || !env.GUESTS) return 0;
  let n = 0;
  for (const product of Object.keys(RIOT)) {
    const live = await riotNow(product, fetchFn);
    if (!live) continue;
    await guestsStub(env).fetch(new Request("https://guests/riot-log", { method: "POST",
      body: JSON.stringify({ product, url: live.url, version: live.version, now }) }));
    n++;
  }
  return n;
}

// "rust" -> the Steam game: {id, name} (an exact name wins over the first result)
const simple = (t) => String(t).toLowerCase().replace(/[^a-z0-9]+/g, " ").trim();
export function steamPick(search, q) {
  const items = (search && Array.isArray(search.items) ? search.items : []).filter((i) => i && Number.isInteger(i.id) && typeof i.name === "string");
  if (!items.length) return null;
  const want = simple(q);
  const it = items.find((i) => simple(i.name) === want) || items[0];
  return { id: it.id, name: it.name.slice(0, 80) };
}
// api.steamcmd.net's answer -> {time (ms), build} of the public branch, or null
export function steamBuild(info, id) {
  const app = info && info.data && info.data[String(id)];
  const pub = app && app.depots && app.depots.branches && app.depots.branches.public;
  const t = pub && Number(pub.timeupdated);
  return t > 0 ? { time: t * 1000, build: String(pub.buildid || "").slice(0, 20) } : null;
}

async function game(request, env, path, now, fetchFn = fetch) {
  if (request.method !== "POST") return reply(405, { ok: false, error: "method not allowed" });
  const body = await readCapped(request, 512);
  if (!body) return reply(413, { ok: false, error: "too big" });
  const g = await guest(request, env, path, now, body);
  if (!g.name) return reply(401, { ok: false, guest: false, error: g.why });
  let b = {};
  try { b = JSON.parse(new TextDecoder().decode(body)); } catch {}
  const q = simple(b.q || "").slice(0, 60);
  if (!q) return reply(400, { ok: false, error: "which game?" });
  for (const [product, r] of Object.entries(RIOT)) {
    if (!r.says.test(q)) continue;
    if (!env || !env.GUESTS) return reply(503, { ok: false, error: "no guests binding" });
    const res = await guestsStub(env).fetch(new Request("https://guests/riot-get", { method: "POST", body: "{}" }));
    const e = ((await res.json()).log || {})[product];
    return reply(200, { ok: true, name: r.name, kind: "riot", time: e ? e.changed : null, version: e ? e.version : "", since: e ? e.since : null });
  }
  try {
    const s = await fetchFn(`https://store.steampowered.com/api/storesearch/?term=${encodeURIComponent(q)}&l=english&cc=US`, { headers: { "user-agent": UA } });
    const hit = s.ok ? steamPick(await s.json(), q) : null;
    if (!hit) return reply(404, { ok: false, error: `no game called ${q}` });
    const i = await fetchFn(`https://api.steamcmd.net/v1/info/${hit.id}`, { headers: { "user-agent": UA } });
    const bl = i.ok ? steamBuild(await i.json(), hit.id) : null;
    if (!bl) return reply(502, { ok: false, error: `couldn't get ${hit.name}'s builds right now` });
    return reply(200, { ok: true, name: hit.name, kind: "steam", time: bl.time, version: bl.build, since: null });
  } catch {
    return reply(502, { ok: false, error: "couldn't reach Steam right now" });
  }
}

// ---------------------------------------------------------------- routes
export default {
  // 0.8: the Riot logbook, every 15 min (wrangler.toml [triggers])
  async scheduled(event, env, ctx) { ctx.waitUntil(riotTick(env)); },
  async fetch(request, env, ctx, now = Date.now()) {
    const path = new URL(request.url).pathname;
    const get = request.method === "GET" || request.method === "HEAD";

    if (path === "/ping") {
      if (!get) return reply(405, { ok: false, error: "method not allowed" }, { allow: "GET, HEAD" });
      return reply(200, { ok: true, service: "ellune", version: VERSION });
    }

    if (path === "/me") {
      if (request.method !== "GET") return reply(405, { ok: false, error: "method not allowed" }, { allow: "GET" });
      const body = await readCapped(request, 1024);
      if (!body) return reply(413, { ok: false, error: "too big" });
      const g = await guest(request, env, path, now, body);
      if (!g.name) return reply(401, { ok: false, guest: false, error: g.why });
      return reply(200, { ok: true, guest: true, name: g.name });
    }

    if (path === "/pc" || path === "/phone" || path === "/status") return relay(request, env, new URL(request.url), path, now);
    if (path === "/register") return register(request, env, path, now);
    if (path === "/devices" || path === "/remove") return devices(request, env, path, now);
    if (path === "/prefs") return prefs(request, env, path, now);
    if (path === "/game") return game(request, env, path, now, (ctx && ctx.fetch) || fetch);

    if (path === "/hear" || path === "/understand") {
      if (request.method !== "POST") return reply(405, { ok: false, error: "method not allowed" }, { allow: "POST" });
      const body = await readCapped(request, path === "/hear" ? MAX_WAV : 2048);
      if (!body) return reply(413, { ok: false, error: "too big" });
      const g = await guest(request, env, path, now, body);
      if (!g.name) return reply(401, { ok: false, guest: false, error: g.why });
      const ai = env && env.AI;
      if (!ai) return reply(503, { ok: false, error: "no AI binding" });
      return path === "/hear" ? hear(body, ai) : understand(body, ai);
    }

    return reply(404, { ok: false, error: "not found" });
  },
};
