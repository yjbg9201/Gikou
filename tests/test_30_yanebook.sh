#!/usr/bin/env bash
# G2: やねうら王定跡（.db / .ybb）の読み込み、および既存の技巧形式 book.bin の回帰テスト
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"

# 1. やねうら王定跡のシナリオテスト（平手・後手番・FlippedBook・非合法手・各オプション・破損ファイル）
python3 -I "$here/yanebook_test.py" "$GIKOU_BIN" "$WORK/yanebook"

# 2. 技巧形式 book.bin の回帰テスト（原本 upstream-original = ca25edf のバイナリと出力を比較）
upstream_bin="${UPSTREAM_BIN:-}"
if [[ -z "$upstream_bin" ]]; then
  src="$WORK/upstream-src"
  rm -rf "$src"; mkdir -p "$src"
  git -C "$root" archive ca25edf | tar -x -C "$src"
  mkdir -p "$src/obj/release/src" "$src/bin"   # 原本の Makefile は並列ビルドでディレクトリ作成が間に合わないため
  make -C "$src" -j"$(nproc)" release >/dev/null 2>&1
  upstream_bin="$src/bin/release"
fi
python3 -I "$here/gikou_book_regression.py" "$GIKOU_BIN" "$upstream_bin" "$WORK/gikou-book"
echo "yanebook OK"
