#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build
case "$(uname -s)" in
  Darwin) link_flags='-Wl,-dead_strip' ;;
  *) link_flags='-Wl,--gc-sections' ;;
esac
c++ -std=gnu++17 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
  -ffunction-sections -fdata-sections $link_flags -DFM1BUDDY_RECOVERY_READ_ONLY=1 \
  -I src/app test/recovery_policy_test.cpp src/app/uboot.cpp -o build/recovery_policy_test
build/recovery_policy_test
sh tools/diagnostic-test.sh
