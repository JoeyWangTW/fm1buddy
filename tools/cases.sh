#!/bin/sh
# Every case as STLs, in build/case/: fm1buddy-case-CONFIG[-battery]-PART.stl. With --png, also the pictures the
# installer page shows (site/case/CONFIG.png, background made transparent by png_key.py; kept in git: rendering
# pictures needs a screen, which CI hasn't)
#   tools/cases.sh            needs openscad (a 2025+ snapshot renders in seconds), or:
#   docker run --rm -v "$PWD:/w" -w /w openscad/openscad:dev.2026-09-28 tools/cases.sh
set -e
cd "$(dirname "$0")/.."
out=build/case
mkdir -p "$out" site/case
backend=
openscad --help 2>&1 | grep -q -- --backend && backend=--backend=manifold
for config in s3 bare oled sd sd_oled cyd lite; do
  for battery in false true; do
    [ "$config" = lite ] && [ "$battery" = true ] && continue  # its battery is a power bank
    name=$config
    [ "$battery" = true ] && name=$config-battery
    for part in base lid; do
      openscad $backend -q -D "config=\"$config\"" -D "battery=$battery" -D "part=\"$part\"" \
        --export-format=binstl -o "$out/fm1buddy-case-$name-$part.stl" case/fm1buddy-case.scad
    done
    if [ "$1" = --png ] && [ "$battery" = false ]; then
      openscad $backend -q -D "config=\"$config\"" -D "battery=$battery" -D 'part="preview"' --render \
        --imgsize=900,700 --viewall --autocenter --camera=0,0,0,55,0,25,0 --colorscheme=Tomorrow \
        -o "$out/$config.png" case/fm1buddy-case.scad
      python3 -I tools/png_key.py "$out/$config.png" "site/case/$config.png"
    fi
    echo "$name"
  done
done
