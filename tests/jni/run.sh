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
${KOTLINC:-kotlinc} -nowarn Main.kt $APP/java/io/github/jimmyuns/pocketloomy/Native.kt $APP/java/io/github/jimmyuns/pocketloomy/Words.kt \
    -include-runtime -d /tmp/jnitest.jar 2>&1 | grep -v "^warning" || true
java -Djava.library.path=/tmp -cp /tmp/jnitest.jar MainKt
