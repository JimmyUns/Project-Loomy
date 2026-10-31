# Pocket Loomy (Android)

The phone half of Pocket Loomy: say "display extend" and Loomy on your PC does it, over Bluetooth.
The PC half is Loomy's `/phone` (`/pkg add phone`). Plain Kotlin, no libraries, no internet permission.
The crypto and wire protocol are Loomy's own `src/pocket.c` + Monocypher, compiled through the NDK, so both
sides run identical code. Design and protocol: `POCKET-HANDOVER.md`.

## Get it
- **Release:** `pocket-loomy.apk` on the GitHub release, next to `loomy.exe` (once a signing key is set up).
- **Dev builds:** every push to a `claude/` branch builds "Pocket Loomy dev" (Actions > the run > Artifacts).
  It installs next to the real app, signed with `dev.keystore`, which is in the repo on purpose and is not a secret.

## Use it
1. Pair the phone and the PC in Bluetooth settings once (Android, then accept on Windows).
2. In the app: **Choose PC**. On the PC: `/phone` shows a code. In the app: **Pair** and say the 8 digits.
3. **Talk** (or type) a command: `display extend`, `web gmail`, `rb`, `si`, `open`, `hide`.

## Build locally
Android Studio, open this `pocket/` folder. Or with Gradle 8.14 + JDK 17 + the Android SDK: `gradle assembleDebug`.
