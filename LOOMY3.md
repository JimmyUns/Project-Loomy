# Loomy 3.0 — the plan (agreed with Jimmy, Nov 19 2026). Read this first.

> **3.0 gets its own NEW private repo: `JimmyUns/ProjectLoomyS`** (his call and name, Nov 19). Loomy 2 and Loomy 3
> are too different to share one. This repo, `JimmyUns/Project-Loomy`, is **Loomy 2: finished at v2.25, then
> archived** (read-only, kept as saved code like the older versions). **He deletes the `ellune` Worker and
> everything in it from Cloudflare** (his call, Nov 19: "why have 2 when only to use 1"). Nothing from 3.0 gets
> merged here. This file is the bridge between the two: copy it into the new repo as its first handover.

> **3.0 is built from zero (his words, Nov 19).** A standalone engine in everything: new server, new desktop app,
> new phone app, new commands, maybe a new design. **No 2.x code, commands, names, keys or habits carry over.**
> What stays is the spirit: **performance and minimalism** (see §3).

Fresh chats start here. `HANDOVER.md` (who Jimmy is, how he works, the workflow) is still right about **him**;
its 2.x code notes, `POCKET-HANDOVER.md` and `ELLUNE-HANDOVER.md` are **Loomy 2 only**: don't use them as a
template for 3.0.

## 1. Where things stand (Nov 19 2026)
- **Loomy 2 is finished:** he confirmed v2.24f works (mic, typing, "test", weather, so Ellune 1.0.1 is live).
  It ships as **v2.25**, the last Loomy 2 (same code, version bumped). Then the repo gets archived: no more fixes.
- **Ellune is being deleted** from Cloudflare (Worker + its storage). After that, everything in Pocket Loomy 2 that
  went through Ellune (remote PC commands, her answers, weather, reminders, notes, game questions) stops working.
  He knows and is fine with it: Loomy 2 is retired, not kept running.
- A stray tag **`V2.24e`** (capital V, built nothing) may still exist in Project-Loomy: remind him to delete it.
- **3.0:** planned (this file), repo name picked (`ProjectLoomyS`), not created yet (as of Nov 19).
- **Never finished in 2.x, to rethink for 3.0 (not port):** web search, game update alerts, PC auto-updating
  games, places/restaurants, spoken reminders, a custom voice. Games outside Steam weren't covered.

## 2. Versions
- **Loomy 2 (Project-Loomy):** v2.25 is the finished and final one; the repo is archived after it.
- **Loomy 3 (ProjectLoomyS), his scheme:** test builds are **`v3.0-pre-release-1`, `v3.0-pre-release-2`...**
  until the finished **`v3.0`**. Tags stay lowercase `v`. The update check must sort these right:
  `3.0-pre-release-1 < 3.0-pre-release-2 < ... < 3.0-pre-release-10 < 3.0` (numbers compared as numbers, not text).
  The phone's versionCode needs a formula that keeps the same order (e.g. pre-release N → `base + N`, finished →
  `base + 999`). Ask him before the first tag if the pattern for later versions (3.1?) isn't obvious by then.

## 3. What 3.0 is
Loomy becomes a **server-based service**. **LS (Loomy Server)** is the "server-side queen": the brain. The PC and
the phone are its hands and mouth.
- **LS:** holds the AI, his private profile, notes and small files, timers, and talks to both devices. It can run
  commands on his desktop and send notifications to his phone. Nothing about it is static: what it does and when it
  notifies are AI-decided.
- **Desktop Loomy 3:** a new app. Has a live connection to LS so LS can reach it. **None of the 2.x commands**
  (no `lp`, `web`, `display`, `net`, `sysinfo`, slash commands...): the command set is designed fresh. Tech and
  look not decided: talk with him before building it.
- **Pocket Loomy 3:** a new app, thin client: talk or type in, text or voice out, notifications. **New package
  name and its own signing key**, so it installs next to the 2.x app and never replaces it.
- **One user, ever: Jimmy.** No multi-user design. A **private profile on LS**: home and work location, age,
  preferences, habits (e.g. "shutdowns: always force"). Used for things like "how's the weather tomorrow?" (where
  is he / will he be: phone location, home/work from the profile, or she asks "home or work tomorrow?").
  **The profile is data on LS, never in the repo.**
- **His design rules (Nov 19):**
  - **Performance + minimalism**, not to an extreme: the desktop app looks and feels smooth; no bloat, no features
    without a reason, no RGB-style effects that eat the GPU.
  - **Assume a fresh PC:** nothing from 2.x is installed or relied on (no old config, files, keys or settings).
    He clears 2.x by hand himself: never ship or run a cleanup of 2.x.
  - **No collisions with leftovers:** Loomy 2 is retired and he removes it by hand, but 3.0 still uses its own
    names, folders, package name, ports and startup entries, so a leftover 2.x install can't clash. Pick the hotkey
    with him (2.x used Ctrl+Alt+Space).
  - The design (look) can be different: talk about it with him later.

## 4. Decisions so far (all agreed with him)
- **LS is built from scratch** (updated Nov 19, replacing "Ellune becomes LS, don't rebuild"). A **new Worker**
  (e.g. `ls`) with its own storage, keys and pairing. The `ellune` Worker and its storage get **deleted** by him
  (Nov 19). Same Cloudflare account (an account isn't 2.x code). Ellune's *ideas* that worked (signed
  requests, a device guest list, Durable Objects for the live connection, cron, Workers AI) can be re-chosen on
  their merits, but the code is written new, not copied.
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
- **Private repo** (his worry: his logic and his life shouldn't be public). Private repos are free.
  - **Access for Claude:** the LoomyBot token is a classic PAT with only `public_repo`: it can't touch a private repo.
    He makes a **fine-grained token** for ProjectLoomyS only (Contents + Pull requests read/write) and puts it in
    the project's `github-bot.txt`. In Cowork, `add_repo` also works
    once the repo exists. Claude can't create the repo: he does.
  - Private repos get a monthly GitHub Actions minutes allowance (builds of a few minutes each: fine).
  - **Updates in 3.0 can't come from GitHub release links** (private links aren't public): the release workflow
    uploads the exe/APK to LS (R2), and the apps ask LS (signed).
- **Deploys:** Cloudflare's own Git connection (**Workers Builds**) on the `ls` Worker: Worker → Settings → Builds
  → Connect, give the Cloudflare GitHub app access to ProjectLoomyS only, branch `main`, LS's folder. Every merge
  deploys LS. No Cloudflare token in GitHub, no forgotten `wrangler deploy`. Worker name must match `wrangler.toml`'s
  `name`. Deploys don't wait for GitHub checks: fine, only he merges to `main`. Connect it once the first LS PR
  (with `wrangler.toml`) is merged. Rejected: a VPS (costs, upkeep), GitLab/Codeberg (nothing gained), keys pasted
  in chat.
- **Loomy 2 is retired, not kept running** (updated Nov 19, replacing "keep 2.x working while 3.0 is built"):
  v2.25 released, repo archived, Ellune deleted. Pocket Loomy 3 still has its own package name and key, so it's a
  separate app, never an update of the 2.x one.

## 5. Order (proposed, he said yes to starting with step 0)
0. **Setup, with exact clicks for him:** he creates **ProjectLoomyS** (private); a fine-grained token for it →
   project file (or `add_repo`); copy this file in as its handover (plus a short `HANDOVER.md` for 3.0: who he is,
   the workflow and the LoomyBot rules, taken from Project-Loomy's `HANDOVER.md` §1–2, nothing about 2.x code).
   In Loomy 2: v2.25 merged + released, tag `V2.24e` deleted, then the repo archived; the `ellune` Worker deleted.
1. **LS skeleton**, written fresh: profile storage, R2 for files/notes, the model setting, the command log, his
   devices' keys + pairing. Then connect Workers Builds (§4).
2. **PC ↔ LS:** first decide the desktop app's tech and look with him (§3 rules). Then the live connection,
   read-only checks (on/off, running apps, downloads), and the approval flow end to end with his shutdown example.
3. **Phone as thin client:** talk/type → LS, answers as text/voice, approvals with Yes/No, notifications (decide
   FCM vs polling here).
4. **Updates through LS** (the repo is private from day one, so this comes before the first pre-release that
   needs updating).
5. **Smart features on LS:** reminders/notes (so LS can notify), weather, games, then web search, game alerts,
   PC auto-update, places.
6. **Profile-aware answers** (home/work, habits, mood).

## 6. Ask him first in the new chat
- Has he created ProjectLoomyS and the fine-grained token? (If not: give him the clicks, §5 step 0.)
- Is v2.25 merged and released, `V2.24e` deleted, Project-Loomy archived, Ellune deleted?
- OK to start step 1 (LS skeleton)? And when to talk about the desktop app's tech/look?

## 7. Rules that still hold
- The LoomyBot rules: Claude never pushes to `main` or creates `v*` tags; he merges and releases. Tags are
  lowercase `v` (workflows only run on `v*`).
- Never print tokens. Nothing personal or secret in the repo, private or not (profile lives on LS).
- He's direct and casual; short answers, honest tradeoffs, a clear recommendation, he decides. When he says "tell me
  your opinion first", don't build.
- When a Cowork session can't push: the task must be started with the repo attached (or the `add_repo` tool used).
  For 3.0 that's **ProjectLoomyS**; Project-Loomy is archived (read-only). `/mnt/project` may not exist: read
  `github-bot.txt` with the Projects tool.
