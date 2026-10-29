#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
content=$(cd -- "${1:?usage: test-host.sh EXTERNAL_CONTENT_DIRECTORY}" && pwd)
cd "$root"
mkdir -p build/host
make -j4 -f tests/host.mk > build/host/compile.log 2>&1
export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
for mode in idle keyboard pad; do
  build/host/msx1 "$content/triton.rom" "$content/cbios_main_msx1.rom" \
    "build/host/final-$mode" "$mode" 3600 > "build/host/final-$mode.log" 2>&1
  tail -n 4 "build/host/final-$mode.log"
done
