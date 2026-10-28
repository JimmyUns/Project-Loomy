#!/bin/sh
# Runs Loomy's command logic on Linux (no Windows needed) against a fake windows.h.
# Covers: strict args, modes, web/webconfig + files, Tab completion, panel keys, sysinfo + Flow,
# version compare, and the /update release check (against a real GitHub release JSON).
# Not part of the build; CI ignores this folder.
set -e
cd "$(dirname "$0")"
gcc -std=gnu11 -w -Ishim -I../src -o /tmp/loomy_test test.c shim.c \
    ../src/modes.c ../src/web.c ../src/display.c ../src/sysinfo.c ../src/commands.c ../src/config.c ../src/util.c ../src/version.c
/tmp/loomy_test
