#!/bin/sh
./gradlew build $@
cp app/build/outputs/apk/release/app-release.apk ~/test.apk
