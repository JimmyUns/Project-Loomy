// Ellune: Loomy's online sister. Runs on Cloudflare Workers.
// For now it only answers a ping, so Loomy can check the whole path works
// (hosting, deploy, Loomy reaching it) before any real features exist.
//
// GET /ping  ->  200 {"ok":true,"service":"ellune","version":"0.1"}
// anything else on /ping -> 405, any other path -> 404.
// No secrets, no storage, no logging of who asked.

const VERSION = "0.1";

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

export default {
  async fetch(request) {
    const path = new URL(request.url).pathname;

    if (path === "/ping") {
      if (request.method !== "GET" && request.method !== "HEAD") {
        return reply(405, { ok: false, error: "method not allowed" }, { allow: "GET, HEAD" });
      }
      return reply(200, { ok: true, service: "ellune", version: VERSION });
    }

    return reply(404, { ok: false, error: "not found" });
  },
};
