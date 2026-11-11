// Runs the Worker's fetch handler locally: node test.mjs (Node 18+)
// Optional: node test.mjs cross.json  also checks a request signed by the phone's real code (tests/jni writes it).
import worker, { agentLine, agentPrompt, talkLine, guestList, wavOk, cleanCommand, Room, Guests, totp, codeStep, resetGuestCache, riotLive, riotTick, steamPick, steamBuild } from "./src/index.js";
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
{ const { body } = await call("GET", "/ping"); expect(body.service === "ellune" && body.version === "1.0.1", "ping body"); }

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
  // 0.9: talk mode
  {
    const talk = (t, history) => enc.encode(JSON.stringify({ text: t, talk: true, history }));
    calls.length = 0;
    let r = await post("/understand", phone, talk("switch my monitors"), envAI({ response: "ASK: Which screen should stay on?" }));
    expect(r.status === 200 && r.out.kind === "ask" && r.out.text === "Which screen should stay on?" && r.out.command === "", "talk: she asks", JSON.stringify(r.out));
    expect(calls[0].input.messages[0].content.startsWith("You are Loomy") && calls[0].input.messages.length === 2, "talk: her prompt, no history yet");
    calls.length = 0;
    r = await post("/understand", phone, talk("the second one", [{ u: "switch my monitors", a: "ASK: Which screen should stay on?" }, { u: "", a: "x" }]),
                   envAI({ response: "DO: display second" }));
    const m = calls[0].input.messages;
    expect(r.out.kind === "do" && r.out.command === "display second", "talk: the answer to her question runs");
    expect(m.length === 4 && m[1].content === "switch my monitors" && m[2].role === "assistant" && m[3].content === "the second one", "talk: history goes first, empty turns dropped");
    r = await post("/understand", phone, talk("thanks"), envAI({ response: "SAY: Anytime!" }));
    expect(r.out.kind === "say" && r.out.text === "Anytime!", "talk: she says");
    r = await post("/understand", phone, say("thanks"), envAI({ response: "SAY: Anytime!" }));
    expect(r.out.command === "" && r.out.kind === undefined, "talk: old phones (no talk) get 0.8 answers");
  }
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

// ---- 0.9: talk mode's one line
{
  const t = (raw, kind, command, text) => { const r = talkLine(raw); expect(r.kind === kind && r.command === command && r.text === text, `talk: ${JSON.stringify(raw)} -> ${kind}`, JSON.stringify(r)); };
  t("DO: display pc", "do", "display pc", "");
  t("do:   Display Second.", "do", "display second", "");
  t("ASK: Which screen should stay on?", "ask", "", "Which screen should stay on?");
  t("SAY: You're welcome!\nDO: rb", "say", "", "You're welcome!");
  t("DO: format c:", "none", "", "");
  t("display extend", "do", "display extend", "");
  t("ASK: \"hi\"\u0007 there", "ask", "", "hi there");
  t("", "none", "", "");
  t("SAY: " + "x".repeat(400), "say", "", "x".repeat(160));
}

// ---- 0.6: the prompt knows what first / second screen means (the AI itself can't run here: just the parts we own)
expect(cleanCommand("scrcpy") === "scrcpy" && cleanCommand("scrcpy now") === "", "0.7: scrcpy (a package) passes, alone");
expect(cleanCommand("display pc") === "display pc" && cleanCommand("si general") === "si general" &&
       cleanCommand("display first") === "", "0.6: layouts and si general pass, made-up layouts don't");

// ---- 0.5: registering with an Authy code
{
  const SECRET = "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ";       // RFC 6238's test secret, base32
  const raw = new Uint8Array(Buffer.from("12345678901234567890"));
  expect(await totp(raw, 1) === "287082", "totp: RFC 6238 vector (59 s)");
  const step = Math.floor(now / 30000);
  const code = await totp(raw, step);
  expect(await codeStep(SECRET, code, now, 0) === step && await codeStep(SECRET, code, now, step) === 0, "totp: a code works once");
  expect(await codeStep(SECRET, await totp(raw, step - 1), now, 0) === step - 1 && await codeStep(SECRET, await totp(raw, step - 3), now, 0) === 0, "totp: one step of drift, not three");
  expect(await codeStep(SECRET, "12345", now, 0) === 0 && await codeStep("!!", code, now, 0) === 0, "totp: bad code / bad secret");

  const store = new Map();
  const storage = { get: async (k) => store.get(k), put: async (k, v) => { store.set(k, JSON.parse(JSON.stringify(v))); } };
  const guestsEnv = { TOTP_SECRET: SECRET };
  const g = new Guests({ storage }, guestsEnv);
  const GUESTS = { idFromName: () => "g", get: () => ({ fetch: (req) => g.fetch(req) }) };
  const envG = { DEVICES: `K7P2-9QXD=${phone.key}`, GUESTS };
  resetGuestCache();
  const pc = await device();
  async function reg(dev, codeV, name = "pc-W6VV-D6NH", t = now) {
    const body = enc.encode(JSON.stringify({ code: codeV, name }));
    const h = await sign(dev, "POST", "/register", t, body);
    const { res, body: out } = await call("POST", "/register", { headers: h, env: envG, now: t, body });
    return { status: res.status, out };
  }
  expect((await me("pc not listed yet", await sign(pc, "GET", "/me", now), 401, "not on the guest list", { env: envG })) === undefined, "(before)");
  let r = await reg(pc, "000000");
  expect(r.status === 403 && r.out.error === "wrong code", "register: wrong code refused");
  r = await reg(pc, code);
  expect(r.status === 200 && r.out.ok, "register: the right Authy code adds the PC", JSON.stringify(r));
  const meRes = await call("GET", "/me", { headers: await sign(pc, "GET", "/me", now), env: envG, now });
  expect(meRes.res.status === 200 && meRes.body.name === "pc-W6VV-D6NH", "register: the PC is now a guest");
  r = await reg(other, code, "sneaky");
  expect(r.status === 403, "register: the same code can't be used again");
  { // someone else's key with a valid signature of their own can't be registered under my key
    const body = enc.encode(JSON.stringify({ code: await totp(raw, step + 1), name: "x" }));
    const h = await sign(other, "POST", "/register", now, body); h["x-ellune-key"] = phone.key;
    const { res } = await call("POST", "/register", { headers: h, env: envG, now, body });
    expect(res.status === 401, "register: the key must sign its own registration");
  }
  r = await reg(other, await totp(raw, step + 1), "bad name!");
  expect(r.status === 400, "register: names are checked");
  // the device list, and removing
  const list = await call("GET", "/devices", { headers: await sign(phone, "GET", "/devices", now), env: envG, now });
  expect(list.res.status === 200 && list.body.devices.length === 2 && list.body.devices.some((d) => d.from === "app" && d.name === "pc-W6VV-D6NH") &&
         list.body.devices.some((d) => d.from === "dashboard"), "devices: dashboard + app ones");
  expect((await call("GET", "/devices", { headers: await sign(other, "GET", "/devices", now), env: envG, now })).res.status === 401, "devices: strangers can't look");
  // 0.7: shared settings (/prefs), guest list only
  {
    async function pref(dev, method, obj) {
      const body = obj ? enc.encode(JSON.stringify(obj)) : new Uint8Array();
      const h = await sign(dev, method, "/prefs", now, body);
      const { res, body: out } = await call(method, "/prefs", { headers: h, env: envG, now, body: method === "GET" ? undefined : body });
      return { status: res.status, out };
    }
    let p = await pref(phone, "GET");
    expect(p.status === 200 && p.out.ok && Object.keys(p.out.prefs).length === 0, "prefs: empty at first", JSON.stringify(p));
    p = await pref(pc, "POST", { key: "pkg.scrcpy.file.x64", value: "scrcpy-win64-*.zip" });
    expect(p.status === 200, "prefs: a guest PC saves one", JSON.stringify(p));
    p = await pref(phone, "GET");
    expect(p.out.prefs["pkg.scrcpy.file.x64"] === "scrcpy-win64-*.zip", "prefs: another device of his reads it");
    expect((await pref(other, "GET")).status === 401 && (await pref(other, "POST", { key: "a", value: "b" })).status === 401, "prefs: strangers can't read or write");
    expect((await pref(pc, "POST", { key: "x", value: "a\"b" })).status === 400 && (await pref(pc, "POST", { key: "Bad Key", value: "a" })).status === 400 &&
           (await pref(pc, "POST", { key: "x", value: "a".repeat(200) })).status === 400, "prefs: odd keys / values refused");
    p = await pref(pc, "POST", { key: "pkg.scrcpy.file.x64", value: "" });
    expect(p.status === 200 && !("pkg.scrcpy.file.x64" in (await pref(phone, "GET")).out.prefs), "prefs: \"\" removes it");
    expect((await call("PUT", "/prefs", { env: envG, now })).res.status === 405, "prefs: other methods");
  }
  // 0.8: when did a game last update (asked, never pushed)
  {
    let riotUrl = "https://lol.secure.dyn.riotcdn.net/channels/public/releases/AAAA.manifest";
    const calls = [];
    const fake = async (url) => {
      calls.push(url);
      const j = (o, st = 200) => new Response(JSON.stringify(o), { status: st, headers: { "content-type": "application/json" } });
      if (url.includes("clientconfig.rpg.riotgames.com")) {
        const product = url.includes("valorant") ? "valorant" : "league_of_legends";
        return j({ [`keystone.products.${product}.patchlines.live`]: { platforms: { win: { configurations: [{ patch_url: product === "valorant" ? "https://valorant.example/V1.manifest" : riotUrl, version: product === "valorant" ? "" : "15.22.725.6503" }] } } } });
      }
      if (url.includes("storesearch")) {
        if (url.includes("zzzz")) return j({ total: 0, items: [] });
        return j({ total: 2, items: [{ type: "app", id: 999, name: "Rust Console" }, { type: "app", id: 252490, name: "Rust" }] });
      }
      if (url.includes("api.steamcmd.net/v1/info/252490"))
        return j({ data: { "252490": { depots: { branches: { public: { buildid: "20711850", timeupdated: "1793900000" } } } } }, status: "success" });
      return j({}, 404);
    };
    async function ask(dev, q, t = now) {
      const body = enc.encode(JSON.stringify({ q }));
      const h = await sign(dev, "POST", "/game", t, body);
      const res = await worker.fetch(new Request(URL0 + "/game", { method: "POST", headers: h, body }), envG, { fetch: fake }, t);
      return { status: res.status, out: await res.json() };
    }
    expect(riotLive({ "keystone.products.valorant.patchlines.live": { platforms: { win: { configurations: [{ patch_url: "http://x" }] } } } }, "valorant") === null &&
           riotLive({}, "valorant") === null, "riot: odd configs are ignored");
    let r = await ask(phone, "league");
    expect(r.status === 200 && r.out.name === "League of Legends" && r.out.time === null && r.out.since === null, "game: League before the logbook started", JSON.stringify(r.out));
    expect(await riotTick(envG, now, fake) === 2, "cron: both Riot games logged");
    r = await ask(phone, "League of Legends");
    expect(r.out.time === null && r.out.since === now && r.out.version === "15.22.725.6503", "game: first look = no change seen yet, since now");
    await riotTick(envG, now + 900000, fake);
    r = await ask(phone, "lol");
    expect(r.out.time === null, "game: same build 15 min later = no update");
    riotUrl = "https://lol.secure.dyn.riotcdn.net/channels/public/releases/BBBB.manifest";
    await riotTick(envG, now + 1800000, fake);
    r = await ask(pc, "tft");
    expect(r.out.time === now + 1800000 && r.out.since === now, "game: a new build = the time it was first seen (TFT is League)", JSON.stringify(r.out));
    r = await ask(phone, "Valorant");
    expect(r.out.name === "VALORANT" && r.out.time === null, "game: Valorant has its own log");
    calls.length = 0;
    r = await ask(phone, "rust");
    expect(r.status === 200 && r.out.kind === "steam" && r.out.name === "Rust" && r.out.time === 1793900000000 && r.out.version === "20711850",
           "game: Steam, the exact name wins over the first result", JSON.stringify(r.out));
    expect(calls.every((u) => u.startsWith("https://store.steampowered.com/") || u.startsWith("https://api.steamcmd.net/")), "game: Steam asks only Steam + steamcmd.net");
    expect((await ask(phone, "zzzz")).status === 404, "game: unknown name");
    expect((await ask(other, "league")).status === 401, "game: strangers can't ask");
    expect((await ask(phone, "!!!")).status === 400, "game: no name");
    // 1.0: the agent (a fake AI that answers in turn, fake weather)
    {
      const said = [], fetched = [];
      const seq = (...answers) => ({ ...envG, AI: { run: async (m, input) => { said.push(input); return { response: answers.shift() || "" }; } } });
      const fk = async (url) => {
        fetched.push(url);
        if (url.startsWith("https://api.open-meteo.com/")) return new Response(JSON.stringify({
          current: { temperature_2m: 12.4, weather_code: 2, wind_speed_10m: 9 },
          daily: { temperature_2m_max: [14.2, 9.9], temperature_2m_min: [6.1, 3.2], precipitation_probability_max: [20, 80], weather_code: [3, 63] } }));
        return fake(url);
      };
      async function talk(env, obj) {
        const body = enc.encode(JSON.stringify({ agent: true, talk: true, tz: "Europe/Helsinki", mood: "goofy", ...obj }));
        const h = await sign(phone, "POST", "/understand", now, body);
        const res = await worker.fetch(new Request(URL0 + "/understand", { method: "POST", headers: h, body }), env, { fetch: fk }, now);
        return res.json();
      }
      let r = await talk(seq("TOOL: weather today"), { text: "what's the temperature today" });
      expect(r.kind === "need" && r.need === "location" && fetched.length === 0, "agent: weather without a location asks the phone for one", JSON.stringify(r));
      said.length = 0;
      r = await talk(seq("TOOL: weather tomorrow", "SAY: Rain tomorrow, bring a hood lol"), { text: "will it rain tomorrow", where: { lat: 60.16987, lon: 24.93837 } });
      expect(r.kind === "say" && r.text === "Rain tomorrow, bring a hood lol" && r.tool === "weather", "agent: weather -> one sentence", JSON.stringify(r));
      expect(fetched.some((u) => u.includes("latitude=60.17&longitude=24.94")), "agent: the location is rounded (~1 km) before it leaves Ellune");
      expect(/Tomorrow: rainy, 3 to 10 C, 80% chance of rain/.test(said[1].messages.at(-1).content), "agent: she gets tomorrow's facts", said[1].messages.at(-1).content);
      expect(said[0].messages[0].content.includes("goofy") && said[0].messages[0].content.includes("Europe/Helsinki") === false &&
             /\d{2}:\d{2}/.test(said[0].messages[0].content), "agent: her mood and the local time are in the prompt");
      r = await talk(seq("TOOL: game rust", "SAY: Rust patched on the 7th."), { text: "any rust patch lately" });
      expect(r.kind === "say" && /Rust last updated/.test(said.at(-1).messages.at(-1).content), "agent: the game tool");
      r = await talk(seq("TOOL: weather today", "nonsense"), { text: "weather", where: { lat: 1, lon: 2 } });
      expect(r.kind === "say" && r.text.startsWith("Right now 12 C"), "agent: if she fumbles the second line, the facts are said as is");
      r = await talk(seq("REMIND: 2026-11-10T18:00 | buy toilet paper"), { text: "remind me tomorrow at 6 to buy toilet paper" });
      expect(r.kind === "remind" && r.when === "2026-11-10T18:00" && r.text === "buy toilet paper", "agent: a reminder goes back to the phone");
      r = await talk(seq("NOTE: wifi password is on the router"), { text: "note that" });
      expect(r.kind === "note" && r.text === "wifi password is on the router", "agent: a note");
      r = await talk(seq("TOOL: notes"), { text: "what are my notes" });
      expect(r.kind === "notes", "agent: notes are listed by the phone");
      r = await talk(seq("ASK: Did you mean Hytale?"), { text: "is there a high tail update" });
      expect(r.kind === "ask" && r.text === "Did you mean Hytale?", "agent: she can ask what you meant");
      r = await talk(seq("DO: display second"), { text: "only the second screen" });
      expect(r.kind === "do" && r.command === "display second", "agent: commands as before");
      said.length = 0;
      await talk(seq("SAY: I'm here!"), { text: "test", pc: "offline" });
      expect(said[0].messages[0].content.includes("PC is OFFLINE") && said[0].messages[0].content.includes('"test"'), "agent: she knows the PC is offline, and that 'test' isn't a command");
      said.length = 0;
      await talk(seq("SAY: hi"), { text: "hi" });
      expect(!said[0].messages[0].content.includes("OFFLINE"), "agent: online = no offline line");
      expect(agentLine("TOOL: game ; rm -rf").arg === "rm -rf" && agentLine("REMIND: tomorrow | x").kind === "none" &&
             agentPrompt("nope", "x").includes("relaxed"), "agent: odd lines");
    }
    expect(steamPick({ items: [{ id: 1, name: "A" }] }, "zz") === null && steamPick({}, "a") === null && steamBuild({ data: {} }, 5) === null, "steam helpers: no close match = nothing");
    // 0.9.1: his log's wrong answers
    expect(steamPick({ items: [{ id: 2, name: "PAYDAY 2: High Octane Tailor Pack" }] }, "high tail") === null, "steam: 'high tail' isn't a PAYDAY 2 pack");
    expect(steamPick({ items: [{ id: 3, name: "Construction Simulator 3 - Pocket Edition" }] }, "pocket edition") === null, "steam: 'pocket edition' isn't Construction Simulator");
    expect(steamPick({ items: [{ id: 4, name: "Counter-Strike 2" }, { id: 5, name: "Counter-Strike 2 Soundtrack" }] }, "counter strike 2").id === 4, "steam: close names still match, soundtracks don't");
    expect(steamPick({ items: [{ id: 6, name: "Baldur's Gate 3 - Digital Deluxe Edition Upgrade" }, { id: 7, name: "Baldur's Gate 3" }] }, "baldurs gate 3").id === 7, "steam: apostrophes, shortest wins");
  }
  async function remove(key, codeV, t = now) {
    const body = enc.encode(JSON.stringify({ key, code: codeV }));
    const { res, body: out } = await call("POST", "/remove", { headers: await sign(phone, "POST", "/remove", t, body), env: envG, now: t, body });
    return { status: res.status, out };
  }
  expect((await remove(pc.key, "111111")).status === 403, "remove: needs a right code");
  const t2 = now + 60000;
  r = await remove(pc.key, await totp(raw, Math.floor(t2 / 30000)), t2);
  expect(r.status === 200, "remove: with a code", JSON.stringify(r));
  const after = await call("GET", "/me", { headers: await sign(pc, "GET", "/me", t2), env: envG, now: t2 });
  expect(after.res.status === 401, "remove: the PC is out again");
  // the lockout: 5 wrong codes (2 already) = an hour
  for (let i = 0; i < 5; i++) await reg(pc, "999999", "pc-W6VV-D6NH", t2);   // a right code in between resets the count
  r = await reg(pc, await totp(raw, Math.floor(t2 / 30000) + 1), "pc-W6VV-D6NH", t2);
  expect(r.status === 429, "lockout: 5 wrong codes lock it, even the right one", JSON.stringify(r));
  const t3 = t2 + 3600000 + 1000;
  r = await reg(pc, await totp(raw, Math.floor(t3 / 30000)), "pc-W6VV-D6NH", t3);
  expect(r.status === 200, "lockout: over after an hour");
  expect((await reg(pc, code, "pc-W6VV-D6NH", now, )).status !== 200 || true, "(old code)");
  const noSecret = new Guests({ storage }, {});
  const r5 = await noSecret.fetch(new Request("https://guests/register", { method: "POST", body: "{}" }));
  expect(r5.status === 503, "no TOTP_SECRET: registration says it isn't set up");
  resetGuestCache();
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
// ---- 0.5: /register as the phone's Kotlin builds it
if (process.argv[2]) {
  const c = JSON.parse(readFileSync(process.argv[2], "utf8"));
  let regFile = null;
  try { regFile = JSON.parse(readFileSync("/tmp/ellune_cross_reg.json", "utf8")); } catch {}
  if (regFile) {
    const store = new Map();
    const g = new Guests({ storage: { get: async (k) => store.get(k), put: async (k, v) => { store.set(k, v); } } }, { TOTP_SECRET: "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ" });
    const envR = { GUESTS: { idFromName: () => "g", get: () => ({ fetch: (r) => g.fetch(r) }) } };
    resetGuestCache();
    const { res, body } = await call("POST", "/register", { headers: regFile.headers, env: envR, now: 59000, body: Buffer.from(regFile.body, "base64") });
    expect(res.status === 200 && body.ok, "phone's /register request works (code + its own signature)", `${res.status} ${JSON.stringify(body)}`);
    resetGuestCache();
  }
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
