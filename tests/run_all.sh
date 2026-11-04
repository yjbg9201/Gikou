#!/usr/bin/env bash
# 各タスクのセルフテストを順に実行する。bin/release（と比較用の原本ビルド）が必要。
# 作業ディレクトリは test-work/ 以下に作る（評価ファイル等を読み込むのはカレントディレクトリのため）。
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"
[[ -x bin/release ]] || make -j"$(nproc)" release >/dev/null
export GIKOU_BIN="$root/bin/release"
export WORK="$root/test-work"
rm -rf "$WORK"; mkdir -p "$WORK"

for t in tests/test_*.sh; do
  echo "=== $t"
  bash "$t"
done
echo "ALL TESTS PASSED"
