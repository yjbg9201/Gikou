#!/usr/bin/env bash
# G0: USI の基本応答（評価ファイルなしでも起動・応答できること）
set -euo pipefail
d="$WORK/smoke"; mkdir -p "$d"; cd "$d"
out="$(printf 'usi\nisready\nposition startpos\ngo depth 1\n' | timeout 60 "$GIKOU_BIN")"
grep -q '^usiok$'    <<<"$out"
grep -q '^readyok$'  <<<"$out"
grep -q '^bestmove ' <<<"$out"
echo "smoke OK"
