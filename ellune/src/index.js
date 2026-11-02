// Ellune: Loomy's online sister. Runs on Cloudflare Workers.
//
// GET /ping  ->  200 {"ok":true,"service":"ellune","version":"0.2"}   (open to anyone)
// GET /me    ->  200 {"ok":true,"guest":true,"name":"K7P2-9QXD"}      (guest list only, see below)
// POST /hear  (guest list) body: a WAV clip, 16 kHz mono 16-bit, at most 10 s
//             ->  200 {"ok":true,"heard":"loomy turn the screens off"}  (Whisper, via the AI binding)
// POST /understand (guest list) body: {"text":"make the screens go dark"}
//             ->  200 {"ok":true,"command":"display off"}  ("" when it isn't a Loomy command)
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

const VERSION = "0.3";
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
  const name = guestList(env && env.DEVICES).get(key);
  if (!name) return { why: "not on the guest list" };
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
display extend | display second | display pc | display duplicate - screen layouts
display off | display on - turn the screens off or on
net - what's using the internet
rb - open the recycle bin
si | si cpu | si gpu - system info (si gpu 2 for a second GPU)
github - open Loomy's GitHub page
input <key> - press a key on Loomy's open panel. Keys: up, down, left, right, next, previous, next tab, previous tab, back, enter, select, ok, confirm, cancel, yes, close, home, end, page up, page down. A count can follow: input down 3
Several commands: join with " and " (si and input next tab).
Examples:
make the screens go dark -> display off
wake the monitors up -> display on
how hot is my graphics card -> si gpu
open youtube -> web youtube
put it on both screens -> display extend
scroll down a few -> input down 3
go one tab over -> input next tab
what's the weather -> NONE
call my mom -> NONE`;

const KEYS = "up|down|left|right|next|previous|next tab|previous tab|back|enter|select|ok|confirm|cancel|yes|close|home|end|page up|page down";
const ONE = new RegExp("^(?:open|hide|help|net|rb|github|lp(?: [a-z0-9 ._'-]{1,40})?|web(?: [a-z0-9._-]{1,40})?|" +
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

// ---------------------------------------------------------------- routes
export default {
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
