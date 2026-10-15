#!/bin/sh
# Build the USB MIDI host box's firmware in Docker: build/fm1buddy-usbhost.uf2
set -e
cd "$(dirname "$0")/.."
mkdir -p build
docker run --rm --platform linux/amd64 -v "$PWD:/work" -w /work ubuntu:24.04 sh -c '
set -e
apt-get update -qq >/dev/null
apt-get install -y -qq git cmake gcc-arm-none-eabi libnewlib-arm-none-eabi build-essential python3 >/dev/null
cd /tmp
git clone -q --depth 1 -b 2.2.0 https://github.com/raspberrypi/pico-sdk
git clone -q --depth 1 -b 0.20.0 https://github.com/hathach/tinyusb
git clone -q --depth 1 -b 0.7.2 https://github.com/sekigon-gonnoc/Pico-PIO-USB
export PICO_SDK_PATH=/tmp/pico-sdk PICO_TINYUSB_PATH=/tmp/tinyusb PICO_PIO_USB_PATH=/tmp/Pico-PIO-USB
cmake -S /work/usbhost -B /tmp/out -DPICO_TINYUSB_PATH=/tmp/tinyusb >/dev/null
make -C /tmp/out -j4 2>&1 | grep -E "error|warning|undefined" || true
cp /tmp/out/fm1buddy-usbhost.uf2 /work/build/
'
echo "build/fm1buddy-usbhost.uf2"
