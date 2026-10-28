# ELLA — Handover (idea stage)

Brief for a fresh chat. ELLA is the online sister of **Loomy**. Loomy's `HANDOVER.md` (same repo, same folder)
covers who Jimmy is, how he likes to work, the workflow, and the Loomy codebase: read that first. Keep ELLA notes
here, not in Loomy's handover. This repo is public: nothing secret in here.

## 1. What ELLA is
**ELLA = Encrypted Locker for Loomy Accounts.** Loomy is the local assistant; ELLA is the hosted server side that
Loomy clients talk to. The long-term idea (not decided in detail yet): save, move and share files between Loomy
clients, plus online saves of user data and presets, with an account and password per Loomy.

Jimmy has no hardware to run a server at home, so ELLA runs on rented hosting. He knows there are security
questions and wants to talk security **later**, as its own conversation. Don't bake in security decisions without him.

## 2. Status
- **Nothing exists yet.** No hosting account, no code, no repo or folder chosen.
- **First step he picked: a simple ping.** Loomy sends a request to ELLA, ELLA answers, Loomy shows that ELLA is up
  (and how long it took). That proves the whole path (hosting, deploy, Loomy talking to it) before any real features.
- **Still open:** the full feature list (he hasn't decided), where ELLA's code lives (own repo vs a folder in
  Project-Loomy), the Loomy command name for the ping and which mode it lives in (core fits: it's Loomy upkeep),
  and whether ELLA gets its own domain.

## 3. Hosting (researched Oct 2026, recheck prices before deciding)
- **Recommended to start: Cloudflare (serverless).** Write only the logic; Cloudflare runs, patches and keeps it up.
  Free plan, roughly: Workers 100,000 requests/day; R2 file storage 10 GB with free downloads; D1 database
  (SQLite) for accounts/lists. Uploads are capped at 100 MB per request on the free plan (bigger files = chunking).
  Server code would be JavaScript/TypeScript (a second codebase, not C).
- **Alternative: a small VPS** (e.g. Hetzner, ~€4/month after their April 2026 price rise). Full control, but he'd be
  responsible for updates, backups and security. Can move here later if Cloudflare gets limiting.
- The sandbox can't reach `api.cloudflare.com` (not in its allowed domains), so deploying from chat won't work as-is.
  Deploys would be done by him (e.g. Cloudflare dashboard or `wrangler` on his PC) unless he adds the domain.
  Any deploy token would follow Loomy's spirit: nothing ships without him.

## 4. What to expect (told to him already)
- **Possible:** accounts with passwords; cloud saves (web lists, batches, presets, settings) loadable on any PC;
  a personal file locker; sending a file to another Loomy user's inbox; share links that expire or are user-limited.
- **Hard / off the table:** instant "you got a file" pings while Loomy is hidden (Loomy idles at zero cost; it would
  check on open, or on a slow timer, which costs lightness); huge files (chunking, storage past free tier costs a bit);
  live collaboration. Holding other people's files makes him responsible for them.
- **Work laptop:** company rules apply to moving files/data off it. Never help get around workplace security.
- **Direct laptop-to-laptop without a server:** fine on the same wifi (but Windows' firewall prompt likely needs
  admin on the work laptop). Over the internet, routers block incoming connections, so a middle server is needed
  anyway: that's why ELLA exists.

## 5. Loomy-side notes for when the ping gets built
- Loomy already does HTTPS with WinHTTP in `update.c` (`https_get`, with 5 s DNS/connect/send and 10 s read timeouts,
  on the UI thread). Reuse it; call `ui_flush()` before the request so "Pinging ELLA..." shows first.
- Keep Loomy tiny: the server does the heavy lifting, Loomy just makes small requests.
- Nothing secret goes in the exe (anyone can read it). Real auth comes with the security conversation.
