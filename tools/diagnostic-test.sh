#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build
case "$(uname -s)" in
  Darwin) link_flags='-Wl,-dead_strip' ;;
  *) link_flags='-Wl,--gc-sections' ;;
esac
c++ -std=c++17 -O2 -ffunction-sections -fdata-sections $link_flags \
  -DFM1BUDDY_RECOVERY_READ_ONLY=1 -I src/app \
  test/diagnostic_test.cpp src/app/uboot.cpp -o build/diagnostic_test
build/diagnostic_test
