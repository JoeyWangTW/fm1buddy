#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build
case "$(uname -s)" in
  Darwin) link_flags='-Wl,-dead_strip' ;;
  *) link_flags='-Wl,--gc-sections' ;;
esac
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -DFM1BUDDY_STOCK_RESTORE=1 -I src/app -c src/app/stock_restore.c -o build/stock_restore.o
c++ -std=gnu++17 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
  -ffunction-sections -fdata-sections $link_flags -DFM1BUDDY_STOCK_RESTORE=1 \
  -I src/app test/stock_restore_test.cpp src/app/uboot.cpp build/stock_restore.o -o build/stock_restore_test
build/stock_restore_test
python3 -m unittest discover -s test -p 'test_prepare_v15.py'
