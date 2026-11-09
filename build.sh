#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# ./build.sh            PRO-FM-78 as a module:    build/profm78.dock (deploy it: fm1-dock's tools/dock.py deploy)
# ./build.sh firmware   PRO-FM-78 as a firmware:  build/pro-fm-78.fwsc (fm1-dock with PRO-FM-78 as its module,
#                       starting in it; identity FM-1_986). RELEASE=X.Y names it pro-fm-78-X.Y.fwsc and sets its version.
# Needs fm1-dock's tree (FM1_DOCK, default ../fm1-dock next to this repository), its JieLi toolchain
# (JIELI_TOOLCHAIN) and, for the firmware, the AC79 SDK (AC79_SDK): see fm1-dock's README.
set -eu
cd "$(dirname "$0")"
HERE=$PWD
HB="${FM1_DOCK:-$(cd .. && pwd)/fm1-dock}"
[ -f "$HB/tools/build.py" ] || { echo "no fm1-dock at $HB (set FM1_DOCK)"; exit 1; }
mkdir -p build
# newer FM-1s carry another flash chip (UC25HQ80): without this fm1-dock refuses to write it (SAVE fails)
if ! grep -q 0xB36014u "$HB/firmware/hal/fm1_flash.h"; then
    echo "fm1-dock: applying patches/fm1-dock-uc25-flash.patch (newer FM-1s' flash)"
    git -C "$HB" apply "$HERE/patches/fm1-dock-uc25-flash.patch"
fi
export HB_MOUNT="$HERE"
if [ "${1:-}" = firmware ]; then
    (cd "$HB" && python3 tools/build.py --modules "$HERE/profm78" --name pro-fm-78 --app-name "PRO-FM-78" \
        --identity FM-1_986 ${RELEASE:+--release "$RELEASE"}) | tail -4
    cp "$HB"/build/pro-fm-78*.fwsc "$HB/build/pro-fm-78.syms" build/
    ls build/*.fwsc
else
    (cd "$HB" && python3 tools/hbmod.py "$HERE/profm78") | tail -1
    cp "$HB/build/modules/profm78.dock" build/
fi
