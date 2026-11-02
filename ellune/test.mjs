// Runs the Worker's fetch handler locally: node test.mjs (Node 18+)
// Optional: node test.mjs cross.json  also checks a request signed by the phone's real code (tests/jni writes it).
import worker, { guestList } from "./src/index.js";
import { readFileSync } from "node:fs";

let fails = 0, n = 0;
function expect(good, name, extra = "") {
  n++; if (!good) fails++;
  console.log(`${good ? "ok  " : "FAIL"} ${name}${extra ? ": " + extra : ""}`);
}
const URL0 = "https://ellune.example.workers.dev";
async function call(method, path, { headers = {}, env = {}, body, now } = {}) {
  const res = await worker.fetch(new Request(URL0 + path, { method, headers, body }), env, {}, now);
  return { res, body: await res.json() };
}
async function check(name, method, path, wantStatus, wantOk) {
  const { res, body } = await call(method, path);
  expect(res.status === wantStatus && body.ok === wantOk && res.headers.get("cache-control") === "no-store",
         name, `${res.status} ${JSON.stringify(body)}`);
}

await check("ping", "GET", "/ping", 200, true);
await check("ping with query", "GET", "/ping?x=1", 200, true);
await check("ping POST", "POST", "/ping", 405, false);
await check("root", "GET", "/", 404, false);
await check("other path", "GET", "/files", 404, false);
await check("trailing slash", "GET", "/ping/", 404, false);
{ const { body } = await call("GET", "/ping"); expect(body.service === "ellune" && body.version === "0.2", "ping body"); }

// ---- the guest list
const b64url = (u8) => Buffer.from(u8).toString("base64url");
const enc = new TextEncoder();
async function device() {
  const kp = await crypto.subtle.generateKey({ name: "ECDSA", namedCurve: "P-256" }, true, ["sign", "verify"]);
  return { kp, key: b64url(new Uint8Array(await crypto.subtle.exportKey("raw", kp.publicKey))) };
}
async function sign(dev, method, path, time, body = new Uint8Array()) {
  const h = Buffer.from(await crypto.subtle.digest("SHA-256", body)).toString("hex");
  const s = await crypto.subtle.sign({ name: "ECDSA", hash: "SHA-256" }, dev.kp.privateKey, enc.encode(`ellune1\n${method}\n${path}\n${time}\n${h}`));
  return { "x-ellune-key": dev.key, "x-ellune-time": String(time), "x-ellune-sig": b64url(new Uint8Array(s)) };
}
const phone = await device(), other = await device();
const env = { DEVICES: `K7P2-9QXD=${phone.key}\n  broken-line\nbad!name=${other.key}\nshort=BAAA\n` };
const now = 1_793_600_000_000;

expect(guestList(env.DEVICES).size === 1 && guestList(env.DEVICES).get(phone.key) === "K7P2-9QXD", "list skips bad lines");
expect(guestList("").size === 0 && guestList(undefined).size === 0, "empty / missing list");
expect(guestList(`a=${phone.key},b=${other.key}`).size === 2, "commas work too");

async function me(name, headers, want, why, opts = {}) {
  const { res, body } = await call(opts.method || "GET", opts.path || "/me", { headers, env: opts.env || env, now: opts.now || now });
  const good = res.status === want && (want === 200 ? body.guest === true && body.name === "K7P2-9QXD" : body.guest === false && body.error === why);
  expect(good, name, `${res.status} ${JSON.stringify(body)}`);
}
await me("guest signed in", await sign(phone, "GET", "/me", now), 200);
await me("59 s off still fine", await sign(phone, "GET", "/me", now - 59_000), 200);
await me("no headers", {}, 401, "not signed");
await me("stranger's key", await sign(other, "GET", "/me", now), 401, "not on the guest list");
await me("empty list", await sign(phone, "GET", "/me", now), 401, "not on the guest list", { env: {} });
await me("old request (61 s)", await sign(phone, "GET", "/me", now - 61_000), 401, "clock off (over 60 s)");
await me("future request (61 s)", await sign(phone, "GET", "/me", now + 61_000), 401, "clock off (over 60 s)");
await me("time not a number", { ...(await sign(phone, "GET", "/me", now)), "x-ellune-time": "1e12" }, 401, "clock off (over 60 s)");
{ const h = await sign(phone, "GET", "/me", now); h["x-ellune-time"] = String(now + 1);
  await me("time changed after signing", h, 401, "bad signature"); }
await me("signed for another path", await sign(phone, "GET", "/files", now), 401, "bad signature");
await me("signed for another method", await sign(phone, "POST", "/me", now), 401, "bad signature");
{ const h = await sign(other, "GET", "/me", now); h["x-ellune-key"] = phone.key;
  await me("stranger signs with my key's name", h, 401, "bad signature"); }
{ const h = await sign(phone, "GET", "/me", now); const s = Buffer.from(h["x-ellune-sig"], "base64url"); s[10] ^= 1; h["x-ellune-sig"] = s.toString("base64url");
  await me("one bit flipped", h, 401, "bad signature"); }
await me("signature junk", { ...(await sign(phone, "GET", "/me", now)), "x-ellune-sig": "!!!" }, 401, "bad signature");
await me("signature too short", { ...(await sign(phone, "GET", "/me", now)), "x-ellune-sig": "AAAA" }, 401, "bad signature");
{ const { res } = await call("POST", "/me", { headers: await sign(phone, "POST", "/me", now), env, now, body: "x" });
  expect(res.status === 405, "POST /me is 405"); }

// ---- a request signed by the phone's real Kotlin code (tests/jni/run.sh writes it)
if (process.argv[2]) {
  const c = JSON.parse(readFileSync(process.argv[2], "utf8"));
  const e = { DEVICES: `${c.name}=${c.key}` };
  const { res, body } = await call("GET", "/me", { headers: c.headers, env: e, now: c.time });
  expect(res.status === 200 && body.name === c.name, "phone's Kotlin signature verifies", `${res.status} ${JSON.stringify(body)}`);
  const { res: r2 } = await call("GET", "/me", { headers: c.tampered, env: e, now: c.time });
  expect(r2.status === 401, "phone's tampered request refused");
}

console.log(fails ? `${fails} of ${n} failed` : `all ${n} passed`);
process.exit(fails ? 1 : 0);
