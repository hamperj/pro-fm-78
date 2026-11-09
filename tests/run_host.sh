#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# PRO-FM-78 on this computer (tests/profm78_host.c): every check, build/host/profm78.wav and the screens as PNG.
# Needs fm1-dock's tree for the module interface and the host's canvas (FM1_DOCK, default ../fm1-dock) and PIL.
set -e
cd "$(dirname "$0")/.."
HB="${FM1_DOCK:-$(cd .. && pwd)/fm1-dock}"
mkdir -p build/host
cc -O2 -Wall -Wno-unused-function -ffp-contract=off -I"$HB/firmware/dock" -I"$HB/modules/lib" -I"$HB/firmware/src" \
    -I"$HB/firmware/gen" -o build/host/profm78 tests/profm78_host.c
build/host/profm78 build/host/profm78
python3 - <<'PY'
from pathlib import Path
from PIL import Image
for p in sorted(Path("build/host").glob("profm78-*.ppm")):
    Image.open(p).resize((480, 480), Image.NEAREST).save(p.with_suffix(".png"))
    p.unlink()
PY
