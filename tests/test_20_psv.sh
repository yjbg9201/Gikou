#!/usr/bin/env bash
# G1: やねうら王 PSV 教師の取り込み
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
d="$WORK/psv"; mkdir -p "$d"; cd "$d"

# 1. 組み込みのセルフテスト（cshogi で作ったベクトルとの一致）
"$GIKOU_BIN" --psv-selftest | grep -v '^info string'

# 2. SFEN ⇔ PackedSfen の変換を cshogi と照合（ランダム局面）
"$GIKOU_BIN" --randsfen 200 11 200 | grep '^sfen ' > positions.txt
cut -d' ' -f2- positions.txt | "$GIKOU_BIN" --sfen-pack | grep -v '^info string' > packed.txt
python3 -I - positions.txt packed.txt <<'PY'
import sys, numpy as np, cshogi
buf = np.zeros(32, dtype=np.uint8)
sfens = [l.split(None, 1)[1].strip() for l in open(sys.argv[1])]
hexes = [l.strip() for l in open(sys.argv[2])]
assert len(sfens) == len(hexes)
for s, h in zip(sfens, hexes):
    cshogi.Board(s).to_psfen(buf)
    assert bytes(buf).hex() == h, (s, h)
print(f"sfen-pack vs cshogi: {len(sfens)} positions byte-identical")
PY
"$GIKOU_BIN" --sfen-unpack < packed.txt | grep -v '^info string' > unpacked.txt
diff <(cut -d' ' -f2-4 positions.txt) <(cut -d' ' -f1-3 unpacked.txt)

# 3. --convert-psv の結果を Python 独立実装とバイト単位で照合
python3 -I "$here/psv_check.py" make test.psv
for args in "scale=1.0 eval_limit=3000" "scale=1.11 eval_limit=1000" "scale=0.5 eval_limit=0"; do
  set -- $args
  "$GIKOU_BIN" --convert-psv test.psv "$@" positions=tp.bin games=tg.bin | grep -v '^info string'
  python3 -I "$here/psv_check.py" check test.psv tp.bin tg.bin "${1#scale=}" "${2#eval_limit=}"
done

# 4. 不正な入力
if "$GIKOU_BIN" --convert-psv test.psv scale=abc >/dev/null; then echo "expected failure (scale)"; exit 1; fi
head -c 41 test.psv > broken.psv
if "$GIKOU_BIN" --convert-psv broken.psv >/dev/null; then echo "expected failure (size)"; exit 1; fi

# 5. 教師局面が 80,000 未満なら、学習は未定義動作にならずエラーで終了する
#    a. 棋譜DBの局面がミニバッチ（8,000）未満
printf '1 2017/01/01 a b 1 2 名人戦 矢倉\n7776FU3334FU\n' > kifu_db.txt
if out="$("$GIKOU_BIN" --learn-psv-rootstrap test.psv 2>&1)"; then echo "expected failure (kifu guard)"; exit 1; fi
grep -q 'game database (kifu_db.txt) has only 2 positions' <<<"$out"
#    b. 棋譜DBは十分だが、PSV の教師局面が 80,000 未満
python3 -I - <<'PY'
import random, cshogi
rng = random.Random(1)
with open('kifu_db.txt', 'w', encoding='utf-8') as f:
    for g in range(120):
        b = cshogi.Board(); moves = []
        for _ in range(90):
            legal = list(b.legal_moves)
            if not legal: break
            m = rng.choice(legal); moves.append(cshogi.move_to_csa(m)); b.push(m)
        f.write(f"{g} 2017/01/01 a b {1 + g % 2} {len(moves)} 名人戦 矢倉\n{''.join(moves)}\n")
PY
if out="$("$GIKOU_BIN" --learn-psv-rootstrap test.psv 2>&1)"; then echo "expected failure (teacher guard)"; exit 1; fi
grep -q 'RootStrap teacher data has only 487 positions' <<<"$out"
if out="$("$GIKOU_BIN" --learn-psv test.psv eval_limit=0 2>&1)"; then echo "expected failure (teacher guard)"; exit 1; fi
grep -q 'RootStrap teacher data has only 803 positions' <<<"$out"
echo "psv OK"
