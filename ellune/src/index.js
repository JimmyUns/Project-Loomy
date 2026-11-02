// Ellune: Loomy's online sister. Runs on Cloudflare Workers.
//
// GET /ping  ->  200 {"ok":true,"service":"ellune","version":"0.2"}   (open to anyone)
// GET /me    ->  200 {"ok":true,"guest":true,"name":"K7P2-9QXD"}      (guest list only, see below)
// anything else on a known path -> 405, any other path -> 404.
// No storage, no logging of who asked.
//
// The guest list: Ellune only serves Jimmy's own devices. Each device has its own key pair (on the phone it
// lives in Android's Keystore and can't be copied out). The public halves are listed in the Worker's DEVICES
// variable (Cloudflare dashboard -> the Worker -> Settings -> Variables), one per line as name=key:
//     K7P2-9QXD=BOr0...   (key = base64url of the raw P-256 public key, 65 bytes starting 04)
// A device signs every request (ECDSA P-256 + SHA-256, signature as raw r||s, base64url) over:
//     "ellune1\n" + METHOD + "\n" + path + "\n" + time + "\n" + hex(sha256(body))
// and sends x-ellune-key, x-ellune-time (unix ms) and x-ellune-sig. Requests more than 60 s off are refused, so
// an old request can't be sent again later. The list is public data (public keys only), nothing secret here.

const VERSION = "0.2";
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

// The guest's name, or a short reason it isn't one. Reads the body (for the hash), so call it once.
async function guest(request, env, path, now) {
  const key = request.headers.get("x-ellune-key");
  const time = request.headers.get("x-ellune-time");
  const sig = request.headers.get("x-ellune-sig");
  if (!key || !time || !sig) return { why: "not signed" };
  const name = guestList(env && env.DEVICES).get(key);
  if (!name) return { why: "not on the guest list" };
  if (!/^\d{1,15}$/.test(time) || Math.abs(now - Number(time)) > SKEW_MS) return { why: "clock off (over 60 s)" };
  const sigBytes = b64urlBytes(sig);
  if (!sigBytes || sigBytes.length !== 64) return { why: "bad signature" };

  const body = await request.arrayBuffer();
  const signed = `ellune1\n${request.method}\n${path}\n${time}\n${await sha256hex(body)}`;
  try {
    const pub = await crypto.subtle.importKey("raw", b64urlBytes(key), { name: "ECDSA", namedCurve: "P-256" }, false, ["verify"]);
    const ok = await crypto.subtle.verify({ name: "ECDSA", hash: "SHA-256" }, pub, sigBytes, new TextEncoder().encode(signed));
    return ok ? { name } : { why: "bad signature" };
  } catch {
    return { why: "bad signature" };
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
      const g = await guest(request, env, path, now);
      if (!g.name) return reply(401, { ok: false, guest: false, error: g.why });
      return reply(200, { ok: true, guest: true, name: g.name });
    }

    return reply(404, { ok: false, error: "not found" });
  },
};
