#!/bin/sh
# Desktop test of the Android app's JNI layer and speech parsing (no Android needed).
# Needs a JDK and kotlinc (KOTLINC=/path/to/kotlinc/bin/kotlinc).
set -e
cd "$(dirname "$0")"
export PATH="$PATH:/tmp/kotlinc/bin"
JH=$(dirname $(dirname $(readlink -f $(which javac))))/include
S=../../src
APP=../../pocket/app/src/main
gcc -shared -fPIC -O1 -I$JH -I$JH/linux -I$S -o /tmp/libpocket.so $APP/cpp/pocket_jni.c $S/pocket.c $S/monocypher.c $S/monocypher-ed25519.c
gcc -shared -fPIC -O1 -I$JH -I$JH/linux -I$S -o /tmp/libtestpc.so TestPc.c $S/pocket.c $S/monocypher.c $S/monocypher-ed25519.c
${KOTLINC:-kotlinc} -nowarn Main.kt $APP/java/io/github/jimmyuns/pocketloomy/Native.kt $APP/java/io/github/jimmyuns/pocketloomy/Words.kt $APP/java/io/github/jimmyuns/pocketloomy/Phrases.kt \
    $APP/java/io/github/jimmyuns/pocketloomy/Ellune.kt \
    $APP/java/io/github/jimmyuns/pocketloomy/Sound.kt \
    -include-runtime -d /tmp/jnitest.jar 2>&1 | grep -v "^warning" && exit 1 || true
java -Djava.library.path=/tmp -cp /tmp/jnitest.jar MainKt
# the phone's signed request, checked by Ellune's real Worker code
if command -v node >/dev/null; then
    node ../../ellune/test.mjs /tmp/ellune_cross.json /tmp/ellune_cross_hear.json > /tmp/ellune_out.txt || { cat /tmp/ellune_out.txt; exit 1; }
    grep -v "^ok" /tmp/ellune_out.txt
else echo "(no node: skipped the Ellune cross-check)"; fi
