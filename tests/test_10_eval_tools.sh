#!/usr/bin/env bash
# G3: --randparams / --randsfen / --evaldump / --eval-selftest
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
d="$WORK/eval"; mkdir -p "$d"; cd "$d"

# 1. ランダム params.bin（seed 固定で毎回同じバイト列になること）
"$GIKOU_BIN" --randparams params.bin 1 >/dev/null
sum="$(md5sum params.bin | cut -d' ' -f1)"
[[ "$sum" == "714577546616d3b301c2a82e74548915" || "${SKIP_GOLDEN:-0}" == "1" ]] || {
  echo "params.bin checksum changed: $sum"; exit 1; }
[[ "$(stat -c %s params.bin)" == "259336912" ]]

# 2. ランダム局面（cshogi で読み直して合法な局面であることを確認する）
"$GIKOU_BIN" --randsfen 300 7 160 | grep '^sfen ' > positions.txt
[[ "$(wc -l < positions.txt)" == "300" ]]
python3 -I "$here/check_sfens.py" positions.txt

# 3. 評価値ダンプ（全局面が JSON で出力され、エラーがないこと）
"$GIKOU_BIN" --evaldump positions.txt | grep '^{' > dump.jsonl
python3 -I "$here/check_evaldump.py" positions.txt dump.jsonl

# 4. 不正な入力はエラー行になり、終了コードが非0になること
printf 'sfen lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b 19P 1\nsfen 9/9/9/9/9/9/9/9/10 b - 1\nstartpos moves 7g7e\n' > bad.txt
if "$GIKOU_BIN" --evaldump bad.txt > bad.jsonl; then echo "expected failure"; exit 1; fi
[[ "$(grep -c '"error"' bad.jsonl)" == "3" ]]

# 5. 差分評価と全計算の一致・PP 対称性
"$GIKOU_BIN" --eval-selftest 30 1 | grep -v '^info string'
echo "eval tools OK"
