#!/bin/sh
# Cross-compile from Linux (needs mingw-w64)
set -e
cd "$(dirname "$0")"
CC=x86_64-w64-mingw32-gcc
WINDRES=x86_64-w64-mingw32-windres
$WINDRES -i res/loomy.rc -o res.o -O coff -I res
$CC -Os -s -municode -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables \
    -fno-tree-loop-distribute-patterns -ffunction-sections \
    -Wall -Wno-unused-parameter \
    src/*.c res.o -o loomy.exe \
    -nostdlib -nostartfiles -e entry -mwindows -Wl,--gc-sections,--subsystem,windows \
    -lkernel32 -luser32 -lgdi32 -lshell32 -ladvapi32 -ldwmapi -liphlpapi -lws2_32 -lwinhttp -lgcc
rm -f res.o
ls -l loomy.exe
