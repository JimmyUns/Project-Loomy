// Runs the Worker's fetch handler locally: node test.mjs (Node 18+)
// Optional: node test.mjs cross.json  also checks a request signed by the phone's real code (tests/jni writes it).
import worker, { guestList, wavOk, cleanCommand, Room } from "./src/index.js";
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
{ const { body } = await call("GET", "/ping"); expect(body.service === "ellune" && body.version === "0.4", "ping body"); }

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

// ---- listening: /hear and /understand, with a stand-in for Workers AI
function wav(seconds, { rate = 16000, channels = 1, bits = 16 } = {}) {
  const n = Math.round(seconds * rate) * channels * (bits / 8);
  const u8 = new Uint8Array(44 + n), dv = new DataView(u8.buffer);
  const put = (i, t) => { for (let k = 0; k < 4; k++) u8[i + k] = t.charCodeAt(k); };
  put(0, "RIFF"); dv.setUint32(4, 36 + n, true); put(8, "WAVE"); put(12, "fmt "); dv.setUint32(16, 16, true);
  dv.setUint16(20, 1, true); dv.setUint16(22, channels, true); dv.setUint32(24, rate, true);
  dv.setUint32(28, rate * channels * bits / 8, true); dv.setUint16(32, channels * bits / 8, true); dv.setUint16(34, bits, true);
  put(36, "data"); dv.setUint32(40, n, true);
  for (let i = 44; i < u8.length; i++) u8[i] = (i * 7) & 0xff;
  return u8;
}
expect(wavOk(wav(2)) && wavOk(wav(10)), "wav: 2 s and 10 s ok");
expect(!wavOk(wav(10.1)) && !wavOk(wav(0.05)), "wav: too long / too short refused");
expect(!wavOk(wav(2, { rate: 44100 })) && !wavOk(wav(2, { channels: 2 })) && !wavOk(wav(2, { bits: 8 })), "wav: wrong format refused");
expect(!wavOk(new TextEncoder().encode("<html>" + "x".repeat(5000))) && !wavOk(null), "wav: junk refused");

expect(cleanCommand("display off") === "display off" && cleanCommand("  Display Off.") === "display off", "clean: plain");
expect(cleanCommand("Command: si gpu") === "si gpu" && cleanCommand("si gpu 2") === "si gpu 2", "clean: prefix / gpu n");
expect(cleanCommand("si and input next tab") === "si and input next tab" && cleanCommand("input down 3") === "input down 3", "clean: and / count");
expect(cleanCommand("web youtube") === "web youtube" && cleanCommand("lp games") === "lp games", "clean: alias / folder");
expect(cleanCommand("NONE") === "" && cleanCommand("") === "" && cleanCommand(undefined) === "", "clean: none / empty");
expect(cleanCommand("/pkg remove phone") === "" && cleanCommand("/shutdown") === "", "clean: slash commands never");
expect(cleanCommand("display off; rm -rf /") === "" && cleanCommand("input sudo") === "" && cleanCommand("exe") === "", "clean: anything else never");
expect(cleanCommand("si and si and si and si and si") === "", "clean: at most 4");
expect(cleanCommand("display off\nI turned off the screens!") === "display off", "clean: first line only");

const calls = [];
const AI = (answer) => ({ run: async (model, input) => { calls.push({ model, input }); if (answer instanceof Error) throw answer; return answer; } });
async function post(path, dev, body, envX, opts = {}) {
  const headers = await sign(dev, "POST", path, now, body);
  if (opts.after) opts.after(headers, body);
  const { res, body: out } = await call("POST", path, { headers, env: envX, now, body });
  return { status: res.status, out };
}
const envAI = (answer) => ({ ...env, AI: AI(answer) });
{
  const clip = wav(2);
  calls.length = 0;
  const r = await post("/hear", phone, clip, envAI({ text: "  Loomy,   turn the screens off. " }));
  expect(r.status === 200 && r.out.heard === "Loomy, turn the screens off.", "hear: whisper's text back", JSON.stringify(r));
  const c = calls[0];
  expect(c && c.model.includes("whisper") && c.input.language === "en" && c.input.initial_prompt.includes("Loomy") &&
         Buffer.from(c.input.audio, "base64").equals(Buffer.from(clip)), "hear: the clip goes to whisper as is");
  expect((await post("/hear", other, clip, envAI({ text: "x" }))).status === 401, "hear: stranger refused");
  calls.length = 0;
  const r2 = await post("/hear", phone, clip, envAI({ text: "x" }), { after: (h, b) => { b[100] ^= 1; } });
  expect(r2.status === 401 && calls.length === 0, "hear: audio changed after signing refused, AI never called");
  expect((await post("/hear", phone, wav(2, { rate: 8000 }), envAI({ text: "x" }))).status === 400, "hear: not our wav = 400");
  expect((await post("/hear", phone, wav(10.5), envAI({ text: "x" }))).status === 413, "hear: over 10 s = 413");
  expect((await post("/hear", phone, clip, env)).status === 503, "hear: no AI binding = 503");
  expect((await post("/hear", phone, clip, envAI(new Error("quota")))).status === 503, "hear: AI fails = 503");
  const huge = await call("POST", "/hear", { headers: await sign(phone, "POST", "/hear", now), env: envAI({}), now, body: new Uint8Array(400000) });
  expect(huge.res.status === 413, "hear: huge body cut off before the signature check");
  expect((await call("GET", "/hear", { env: envAI({}), now })).res.status === 405, "hear: GET = 405");

  const say = (t) => enc.encode(JSON.stringify({ text: t }));
  calls.length = 0;
  const u = await post("/understand", phone, say("make the screens go dark"), envAI({ response: "display off" }));
  expect(u.status === 200 && u.out.command === "display off", "understand: command back", JSON.stringify(u));
  expect(calls[0].input.messages[1].content === "make the screens go dark" && calls[0].input.temperature === 0, "understand: text goes to the AI");
  expect((await post("/understand", phone, say("what's the weather"), envAI({ response: "NONE" }))).out.command === "", "understand: none = empty");
  expect((await post("/understand", phone, say("x"), envAI({ response: "/shutdown" }))).out.command === "", "understand: AI says something odd = empty");
  expect((await post("/understand", phone, enc.encode("not json"), envAI({ response: "x" }))).status === 400, "understand: junk = 400");
  expect((await post("/understand", phone, say(""), envAI({ response: "x" }))).status === 400, "understand: empty = 400");
  expect((await post("/understand", phone, say("y".repeat(301)), envAI({ response: "x" }))).status === 400, "understand: too long = 400");
  expect((await post("/understand", other, say("hi"), envAI({ response: "help" }))).status === 401, "understand: stranger refused");
  expect((await post("/understand", phone, say("hi"), envAI(new Error("busy")))).status === 503, "understand: AI fails = 503");
}

// ---- remote: the relay rooms, with a stand-in for Cloudflare's Durable Object + WebSocket runtime
class FakeWs {
  constructor(name) { this.name = name; this.sent = []; this.closed = null; this.att = null; this.peer = null; }
  send(m) { if (this.closed) throw new Error("closed"); this.sent.push(m); }
  close(code, why) { if (!this.closed) { this.closed = { code, why }; } }
  serializeAttachment(a) { this.att = a; }
  deserializeAttachment() { return this.att; }
}
class FakeCtx {
  constructor() { this.socks = []; }
  setWebSocketAutoResponse() {}
  acceptWebSocket(ws, tags) { ws.tags = tags; this.socks.push(ws); }
  getWebSockets(tag) { return this.socks.filter((w) => !w.closed && w.tags.includes(tag)); }
  getTags(ws) { return ws.tags; }
}
let made = 0;
function room() {
  const ctx = new FakeCtx();
  const r = new Room(ctx, {});
  r.upgraded = (client) => ({ status: 101, client });
  r.pair = () => { const a = new FakeWs("client" + made), b = new FakeWs("server" + made++); a.peer = b; return [a, b]; };
  return { r, ctx };
}
const up = (role) => new Request("https://room/x", { headers: { "x-ellune-role": role, Upgrade: "websocket" } });
async function join(r, ctx, role) { await r.fetch(up(role)); return ctx.socks[ctx.socks.length - 1]; }
const texts = (ws) => ws.sent.filter((m) => typeof m === "string");
{
  const { r, ctx } = room();
  const st0 = await (await r.fetch(new Request("https://room/x", { headers: { "x-ellune-role": "status" } }))).json();
  expect(st0.pc === false, "room: no pc yet");
  const lonely = await join(r, ctx, "phone");
  expect(texts(lonely).includes("nopc") && lonely.closed && lonely.closed.code === 4004, "room: phone without a pc is told and closed");
  const pc = await join(r, ctx, "pc");
  expect((await (await r.fetch(new Request("https://room/x", { headers: { "x-ellune-role": "status" } }))).json()).pc === true, "room: status sees the pc");
  const ph = await join(r, ctx, "phone");
  expect(texts(pc).join(",") === "phone", "room: pc told a phone joined");
  const hello = new Uint8Array([0, 3, 1, 2, 0]).buffer, conn = new Uint8Array([0, 2, 5, 9]).buffer;
  r.webSocketMessage(pc, hello);
  r.webSocketMessage(ph, conn);
  expect(ph.sent.includes(hello) && pc.sent.includes(conn), "room: bytes pass both ways, untouched");
  r.webSocketMessage(ph, "drop"); r.webSocketMessage(ph, "phone");
  expect(!ph.closed && texts(pc).join(",") === "phone", "room: a phone's text is ignored (can't fake signals)");
  r.webSocketMessage(ph, new Uint8Array(2000).buffer);
  expect(ph.closed && ph.closed.code === 1009 && texts(pc).join(",") === "phone,gone", "room: an oversized frame closes the phone, pc told");
  const ph2 = await join(r, ctx, "phone");
  const ph3 = await join(r, ctx, "phone");
  expect(ph2.closed && ph2.closed.code === 4001 && texts(pc).join(",") === "phone,gone,phone,gone,phone", "room: one phone at a time, pc told each change");
  r.webSocketClose(ph2);
  expect(texts(pc).length === 5, "room: closing a replaced phone says nothing more");
  r.webSocketMessage(pc, "drop");
  expect(ph3.closed && ph3.closed.code === 4002, "room: the pc can hang up");
  r.webSocketClose(ph3);
  expect(texts(pc).length === 5, "room: (dropped on purpose: no extra gone)");
  const ph4 = await join(r, ctx, "phone");
  r.webSocketClose(ph4); ph4.close(1000, "bye");
  expect(texts(pc)[texts(pc).length - 1] === "gone", "room: a phone leaving tells the pc");
  const ph5 = await join(r, ctx, "phone");
  const pc2 = await join(r, ctx, "pc");
  expect(pc.closed && pc.closed.code === 4000 && ph5.closed && ph5.closed.code === 4003, "room: a new pc connection replaces the old (and its phone)");
  const ph6 = await join(r, ctx, "phone");
  pc2.close(1006, "lost"); r.webSocketClose(pc2);
  expect(ph6.closed && ph6.closed.code === 4003, "room: the pc leaving closes the phone");
  expect((await r.fetch(new Request("https://room/x", { headers: { "x-ellune-role": "pc" } }))).status === 426, "room: websocket only");
}
// the Worker's door to the rooms: guest list, a proper room name, signed with the query, the role set by Ellune
{
  const roomName = "0123456789abcdef0123456789abcdef";
  let seen = null;
  const ROOMS = { idFromName: (n) => n, get: (id) => ({ fetch: async (req) => { seen = { id, role: req.headers.get("x-ellune-role"), up: req.headers.get("Upgrade") }; return reply200(); } }) };
  function reply200() { return new Response("{}", { status: 200 }); }
  const envR = { ...env, ROOMS };
  async function door(path, signPath, extra = {}, who = phone, envX = envR) {
    const h = await sign(who, "GET", signPath, now);
    const { res } = await call("GET", path, { headers: { ...h, ...extra }, env: envX, now });
    return res.status;
  }
  seen = null;
  expect(await door(`/phone?room=${roomName}`, `/phone?room=${roomName}`, { Upgrade: "websocket", "x-ellune-role": "pc" }) === 200 &&
         seen.id === roomName && seen.role === "phone" && seen.up === "websocket", "door: phone let in, role set by Ellune not the caller");
  expect(await door(`/pc?room=${roomName}`, `/pc?room=${roomName}`, { Upgrade: "websocket" }) === 200 && seen.role === "pc", "door: pc let in");
  expect(await door(`/status?room=${roomName}`, `/status?room=${roomName}`) === 200 && seen.role === "status" && !seen.up, "door: status");
  expect(await door(`/phone?room=${roomName}`, `/phone`, { Upgrade: "websocket" }) === 401, "door: the room is part of what's signed");
  expect(await door(`/phone?room=${roomName}`, `/phone?room=${roomName}`, { Upgrade: "websocket" }, other) === 401, "door: strangers refused");
  expect(await door(`/phone?room=NOTAROOM`, `/phone?room=NOTAROOM`, { Upgrade: "websocket" }) === 400, "door: bad room name");
  expect(await door(`/phone?room=${roomName}&x=1`, `/phone?room=${roomName}&x=1`, { Upgrade: "websocket" }) === 400, "door: nothing else in the query");
  expect(await door(`/phone?room=${roomName}`, `/phone?room=${roomName}`) === 426, "door: phone needs a websocket");
  expect(await door(`/pc?room=${roomName}`, `/pc?room=${roomName}`, { Upgrade: "websocket" }, phone, env) === 503, "door: no rooms binding = 503");
}

// ---- a request signed by the phone's real Kotlin code (tests/jni/run.sh writes it)
if (process.argv[2]) {
  const c = JSON.parse(readFileSync(process.argv[2], "utf8"));
  const e = { DEVICES: `${c.name}=${c.key}` };
  const { res, body } = await call("GET", "/me", { headers: c.headers, env: e, now: c.time });
  expect(res.status === 200 && body.name === c.name, "phone's Kotlin signature verifies", `${res.status} ${JSON.stringify(body)}`);
  const { res: r2 } = await call("GET", "/me", { headers: c.tampered, env: e, now: c.time });
  expect(r2.status === 401, "phone's tampered request refused");
}
// ---- /hear and /understand as the phone's Kotlin builds them (a WAV from Sound.wav, JSON from jsonQuote)
if (process.argv[3]) {
  const c = JSON.parse(readFileSync(process.argv[2], "utf8"));
  const h = JSON.parse(readFileSync(process.argv[3], "utf8"));
  const e = { DEVICES: `${c.name}=${c.key}`, AI: AI({ text: "loomy display off", response: "display off" }) };
  const clip = Buffer.from(h.hear.body, "base64");
  expect(wavOk(new Uint8Array(clip)), "phone's WAV is one Ellune takes");
  calls.length = 0;
  const { res, body } = await call("POST", "/hear", { headers: h.hear.headers, env: e, now: c.time, body: clip });
  expect(res.status === 200 && body.heard === "loomy display off" && Buffer.from(calls[0].input.audio, "base64").equals(clip),
         "phone's /hear request works", `${res.status} ${JSON.stringify(body)}`);
  calls.length = 0;
  const u = await call("POST", "/understand", { headers: h.understand.headers, env: e, now: c.time, body: Buffer.from(h.understand.body, "base64") });
  expect(u.res.status === 200 && u.body.command === "display off" && calls[0].input.messages[1].content === 'make the "screens" go dark',
         "phone's /understand request works", `${u.res.status} ${JSON.stringify(u.body)}`);
}

console.log(fails ? `${fails} of ${n} failed` : `all ${n} passed`);
process.exit(fails ? 1 : 0);
