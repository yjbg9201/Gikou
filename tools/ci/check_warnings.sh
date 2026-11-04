#!/usr/bin/env bash
# 原本（upstream-original）時点で既に出ている警告を基準にして、
# 新しい警告が増えていないかを確認する。
#
# 使い方: tools/ci/check_warnings.sh [make-target]   (既定: release)
#         UPDATE_BASELINE=1 tools/ci/check_warnings.sh  で基準ファイルを更新
#
# 行番号・桁番号は編集でずれるため取り除き、「ファイル: 警告文 [フラグ]」の
# 集合として比較する。
set -euo pipefail

target="${1:-release}"
root="$(cd "$(dirname "$0")/../.." && pwd)"
baseline="$root/tools/ci/warnings_baseline_${target}.txt"
log="$(mktemp)"
trap 'rm -f "$log"' EXIT

cd "$root"
rm -rf "obj/$target" "bin/$target"
make -j"$(nproc)" "$target" 2>"$log" >/dev/null || { cat "$log"; exit 1; }

current="$(grep -E ': warning: ' "$log" \
  | sed -E 's/^([^:]+):[0-9]+:[0-9]+: warning: /\1: /' \
  | sort -u || true)"

if [[ "${UPDATE_BASELINE:-0}" == "1" ]]; then
  printf '%s\n' "$current" > "$baseline"
  echo "baseline updated: $baseline ($(printf '%s\n' "$current" | grep -c . || true) entries)"
  exit 0
fi

new="$(comm -13 <(sort -u "$baseline") <(printf '%s\n' "$current") | grep . || true)"
if [[ -n "$new" ]]; then
  echo "New compiler warnings (target=$target):"
  printf '%s\n' "$new"
  exit 1
fi
echo "OK: no new warnings (target=$target)"
