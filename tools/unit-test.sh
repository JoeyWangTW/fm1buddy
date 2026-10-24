#!/bin/sh
# Builds and runs the tests of the app's plain logic (test/*_test.cpp), on the computer
set -e
cd "$(dirname "$0")/.."
mkdir -p build
c++ -std=gnu++17 -Wall -I src/app -o build/midimap_test test/midimap_test.cpp src/app/midimap.cpp
build/midimap_test
