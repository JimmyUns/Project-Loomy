// Makes the Authy secret for Ellune's registration, on this PC (it never goes anywhere else).
//   node totp-setup.mjs
// Then: 1) Authy -> Add account -> Enter key manually -> the key below (6 digits, 30 seconds, "Ellune")
//       2) npx wrangler secret put TOTP_SECRET   -> paste the same key when it asks
// Run it again only if you want a new secret (the old Authy entry stops working then).
import { randomBytes } from "node:crypto";

const A = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
const b = randomBytes(20);
let bits = 0, v = 0, out = "";
for (const x of b) { v = (v << 8) | x; bits += 8; while (bits >= 5) { out += A[(v >>> (bits - 5)) & 31]; bits -= 5; } }
if (bits > 0) out += A[(v << (5 - bits)) & 31];

console.log("\nYour Ellune key (keep it private, like a password):\n");
console.log("   " + out.match(/.{1,4}/g).join(" ") + "\n");
console.log("1. Authy: Add account -> Enter key manually -> type it in (spaces don't matter). Name it Ellune.");
console.log("2. Here: npx wrangler secret put TOTP_SECRET   and paste it when it asks:\n");
console.log("   " + out + "\n");
console.log("3. npx wrangler deploy\n");
