// Runs the Worker's fetch handler locally: node test.mjs (Node 18+)
import worker from "./src/index.js";

let fails = 0;
async function check(name, method, path, wantStatus, wantOk) {
  const res = await worker.fetch(new Request("https://ellune.example.workers.dev" + path, { method }));
  const body = await res.json();
  const good = res.status === wantStatus && body.ok === wantOk && res.headers.get("cache-control") === "no-store";
  if (!good) fails++;
  console.log(`${good ? "ok  " : "FAIL"} ${name}: ${res.status} ${JSON.stringify(body)}`);
}

await check("ping", "GET", "/ping", 200, true);
await check("ping with query", "GET", "/ping?x=1", 200, true);
await check("ping POST", "POST", "/ping", 405, false);
await check("root", "GET", "/", 404, false);
await check("other path", "GET", "/files", 404, false);
await check("trailing slash", "GET", "/ping/", 404, false);

console.log(fails ? `${fails} failed` : "all passed");
process.exit(fails ? 1 : 0);
