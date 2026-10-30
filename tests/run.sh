#!/bin/sh
# Runs Loomy's command logic on Linux (no Windows needed) against a fake windows.h.
# Covers: strict args, modes, web/webconfig + files, Tab completion, panel keys, sysinfo + Flow,
# version compare, the /update release check (against a real GitHub release JSON), and Pocket Loomy:
# the protocol (CPace test vectors, engine against engine, tampering, fuzzing) and phone.c
# (pairing, commands, lockouts, trust expiry) against a fake Bluetooth radio.
# Not part of the build; CI ignores this folder.
set -e
cd "$(dirname "$0")"
CRYPTO="../src/pocket.c ../src/monocypher.c ../src/monocypher-ed25519.c"
gcc -std=gnu11 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wno-unused-parameter \
    -o /tmp/pocket_test pocket_test.c $CRYPTO
/tmp/pocket_test
gcc -std=gnu11 -w -DLOOMY_TESTS -Ishim -I../src -o /tmp/loomy_test test.c shim.c \
    ../src/modes.c ../src/web.c ../src/display.c ../src/features.c ../src/sysinfo.c ../src/commands.c ../src/config.c \
    ../src/util.c ../src/version.c ../src/phone.c $CRYPTO
/tmp/loomy_test
