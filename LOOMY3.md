# Loomy 3.0 — the plan (agreed with Jimmy, Nov 19 2026). Read this first.

Fresh chats start here, then `HANDOVER.md` (who Jimmy is, how he works, the workflow, the 2.x code).
`POCKET-HANDOVER.md` and `ELLUNE-HANDOVER.md` describe the 2.x phone app and server as they are today: still right
about the code, but this file wins on direction.

## 1. Where things stand (Nov 19 2026)
- **Released:** v2.24 (last *finished* version), then test builds v2.24a–**v2.24f** (newest, PR #33 merged, tagged).
  A stray tag **`V2.24e`** (capital V, his mistake, built nothing) still exists: ask him to delete it.
- **Ellune on Cloudflare:** last checked she was **0.8** live; the code is at **1.0.1**. He said he'd forgotten to run
  `npx wrangler deploy` in `ellune/`. Check `https://ellune.loomybot.workers.dev/ping` first: if it isn't 1.0.1, the
  phone's helper features (talk back, weather, reminders, notes, "test" → a reply) don't work properly yet.
- **What 2.24a–f added (all in CHANGELOG.md):** game update questions (Steam on demand, Riot via a 15-min logbook),
  Pocket Loomy talks back (Samsung TTS, his pick), asks when unsure, remembers a minute, new main screen (On/Off card
  on top, mic = talk, Mute / Type / History), typing in the app and in the notification, Ellune 1.0 agent (weather
  with rough location only when asked, reminders with notifications, notes, mood Serious/Chill/Goofy), strict Steam
  name matching, whole KB/s.
- **Never finished from the 2.x helper plan:** web search tool, game update alerts, PC auto-updating games,
  places/restaurants, spoken reminders, "Which view?" spoken, custom voice package (Piper via sherpa-onnx).
  Games outside Steam (Fortnite, Hytale) aren't covered.

## 2. Versions
His rule: every release before a version is done has a letter; the finished one has none. Letters sort after
their number (2.24a > 2.24), so test builds hang off the last finished version. **Suggested to him, not yet
answered:** release the current state as **v2.25** (finished), then 3.0 test builds are v2.25a, b..., and the
finished one is **v3.0**. Ask before tagging anything. (Loomy's history: cmd = 1.0-era, WPF = 1.0, the C app = 2.x.)

## 3. What 3.0 is (his words, condensed)
Loomy becomes a **server-based service**. **LS (Loomy Server)** is the "server-side queen": the brain. The PC and
the phone are its hands and mouth.
- **LS:** holds the AI, his private profile, notes and small files, timers, and talks to both devices. It can run
  commands on his desktop and send notifications to his phone. Nothing about it is static: what it does and when it
  notifies are AI-decided.
- **Desktop Loomy:** keeps the instant local/window commands (display, lp, sysinfo...). Every "smart" request goes
  to LS. The PC holds a live connection to LS so LS can reach it.
- **Pocket Loomy:** a thin client: talk or type in, text or voice out, notifications. No more location/listening
  tricks unless a feature needs them (weather may still send a rough location, only when asked).
- **One user, ever: Jimmy.** No multi-user design. A **private profile on LS**: home and work location, age,
  preferences, habits (e.g. "shutdowns: always force"). Used for things like "how's the weather tomorrow?" (where
  is he / will he be: phone location, home/work from the profile, or she asks "home or work tomorrow?").
  **The profile is data on LS, never in the repo.**

## 4. Decisions so far (all agreed with him)
- **Ellune becomes LS.** He said "Ellune is revoked"; agreed meaning: the *name and role* change, the work doesn't
  get thrown away. Same Cloudflare account and Worker, same device keys + guest list + signed requests, relay,
  Durable Objects, cron, Workers AI. Rename and grow it, don't rebuild.
- **Running commands on his PC (his design):** read-only checks run freely (is the PC on, what's running,
  downloads). Anything that changes something asks first and **shows the exact command** before he says yes.
  His example:
  > him: I think I left my PC on, check and turn it off · LS pings the PC (no permission needed) ·
  > "Your PC is on. Shut it down? `shutdown /s /f /t 0` — Yes / No"
  My additions he accepted: the approval is **signed by the phone's key** for that exact command, **one use,
  expires in ~1–2 min**; the PC only runs phone-signed approvals (so a compromised LS or a wrong AI can't run
  anything alone); a **log** of every command run (when, what, who said yes); risky flags explained in plain words.
  **His standing preference: shutdowns always force (`/f`)**, no warning, still shown in the approval.
  Standing preferences live in his LS profile and change by just telling her.
- **AI notifications:** yes, with brakes I proposed: opt-in kinds, quiet hours, a daily cap, a "less of this"
  button. Push mechanism **not decided**: Google FCM (instant, needs a Google project + a key in LS) vs the phone
  polling LS (no setup, can be late, Samsung battery saver delays it).
- **AI model:** free Workers AI for chatting/picking tools; I recommended a stronger (paid, cheap) model for the
  part that *writes PC commands*. Not decided: ask him. Keep the model a single setting in LS.
- **Files and notes:** Cloudflare R2 (free tier is plenty for notes and small files).
- **The repo goes private** (his worry: Loomy's logic and his life shouldn't be public). Free on GitHub.
  Consequences, all must be handled in step 0/4:
  - **The LoomyBot token is a classic PAT with only `public_repo`: it stops working on a private repo.** He makes a
    **fine-grained token** for this repo only (Contents + Pull requests read/write), puts it in the project's
    `github-bot.txt`, and deletes the old one.
  - Private repos get a monthly GitHub Actions minutes allowance (builds are ~2–3 min each: fine).
  - **The update buttons break** (phone "Check for updates", PC `/update`) because private release links aren't
    public. Fix in 3.0: the release workflow uploads the exe/APK to LS (R2), and the apps ask LS (signed).
- **Deploys:** Cloudflare's own Git connection (**Workers Builds**): Worker → Settings → Builds → Connect, give the
  Cloudflare GitHub app access to this repo only, branch `main`, root `ellune/` (later `ls/`). Every merge deploys
  LS. No Cloudflare token in GitHub, no more forgotten `wrangler deploy`. Worker name must match `wrangler.toml`'s
  `name`. Deploys don't wait for GitHub checks: fine, only he merges to `main`. Rejected: a VPS (costs, upkeep),
  GitLab/Codeberg (nothing gained), keys pasted in chat.
- **Keep 2.x working while 3.0 is built** (he uses it daily): switch pieces over one at a time, test builds as usual.

## 5. Order (proposed, he said yes to starting with step 0)
0. **Setup, with exact clicks for him:** delete tag `V2.24e`; repo → private; new fine-grained token → project
   file; Workers Builds connected; confirm Ellune is on 1.0.1. Then release v2.25 if he agrees (§2).
1. **LS skeleton from Ellune:** rename (keep the URL working or move both apps at once), profile storage, R2 for
   files/notes, the model setting, the command log.
2. **PC ↔ LS:** the PC's live connection, read-only checks (on/off, running apps, downloads), and the approval flow
   end to end with his shutdown example.
3. **Phone as thin client:** talk/type → LS, answers as text/voice, approvals with Yes/No, notifications (decide
   FCM vs polling here).
4. **Updates through LS** (needed once the repo is private; do it before or with going private if possible).
5. **Move the smart features server-side:** reminders/notes to LS (so LS can notify), weather and games as LS tools,
   then the unfinished ones (web search, game alerts, PC auto-update, places).
6. **Profile-aware answers** (home/work, habits, mood).

## 6. Ask him first in the new chat
- Did he deploy Ellune 1.0.1, and does v2.24f work (mic, typing, "test", weather)?
- Release current state as v2.25 and use v2.25a... for 3.0 test builds?
- OK to start step 0 now?

## 7. Rules that still hold
- The LoomyBot rules: Claude never pushes to `main` or creates `v*` tags; he merges and releases. Tags are
  lowercase `v` (workflows only run on `v*`).
- Never print tokens. Nothing personal or secret in the repo, private or not (profile lives on LS).
- He's direct and casual; short answers, honest tradeoffs, a clear recommendation, he decides. When he says "tell me
  your opinion first", don't build.
- When a Cowork session can't push: the task must be started with `JimmyUns/Project-Loomy` attached (or the
  `add_repo` tool used). `/mnt/project` may not exist: read `github-bot.txt` with the Projects tool.
