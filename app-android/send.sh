#!/bin/sh
./gradlew build $@
cp app/build/outputs/apk/debug/app-debug.apk ~/test.apk
