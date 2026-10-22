# Setting up auto-updates (one time, ~10 minutes)

This makes it so: you change the code → GitHub builds the new `loomy.exe` by itself →
you type `update` in Loomy at home and it grabs the new version. After this setup you
never do it again.

You need **git** on your computer. If you don't have it: https://git-scm.com/download/win
(just click Next through the installer).

---

## 1. Create the repo on GitHub
1. Go to github.com, click the **+** at the top-right, **New repository**.
2. Repository name: **Project-Loomy** (no spaces — GitHub uses the dash).
3. Set it to **Public**.
4. Don't tick "Add a README" or anything — leave it empty.
5. Click **Create repository**.

## 2. Upload the code (first time)
(Your username is already set in the code — nothing to edit.)
Open a terminal **in the Loomy folder** (the one with build.sh in it) and run these,
replacing nothing — just run them:

    git init
    git add .
    git commit -m "Loomy"
    git branch -M main
    git remote add origin https://github.com/JimmyUns/Project-Loomy.git
    git push -u origin main

(Git may ask you to log in to GitHub the first time — that's normal.)

## 3. Publish the first version
A "release" is what your `update` command downloads. To make one, you push a version tag.
The number here must match `LOOMY_VERSION` in `src/loomy.h` (currently 2.7):

    git tag v2.7
    git push origin v2.7

Now go look at your repo's **Actions** tab on GitHub. You'll see the robot building.
After a minute or two it finishes and a **Release** appears with `loomy.exe` attached.
That's the one `update` pulls.

---

## From now on, the whole loop is:
1. I hand you changed code (or you edit it).
2. You run:

       git add .
       git commit -m "what changed"
       git push

3. Bump the version: edit `LOOMY_VERSION` in `src/loomy.h` (e.g. to 2.5), commit that too,
   then:

       git tag v2.7
       git push origin v2.7

4. The robot builds and releases it automatically.
5. On your home PC, open Loomy, switch to core mode (`/m switch core`) and type **/update**. Done.

> Tip: the version in `loomy.h` and the tag (`v2.5`) must match, or `update` won't know
> a new one exists. If you forget, just push the right tag.

---

## If something goes wrong
- **`update` says "isn't wired up yet"** → you didn't change `GH_USER` in step 2.
- **`update` says "no releases found"** → the first release (step 4) hasn't finished, or
  the tag wasn't pushed. Check the Actions tab.
- **`update` says "couldn't reach GitHub"** → connection issue, or the repo is private
  (it must be public for the simple updater).
- **The robot build failed (red X in Actions)** → open it, read the red step. Usually a
  typo in the code. Tell me what it says.
