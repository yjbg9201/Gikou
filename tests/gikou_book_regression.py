"""G2 受け入れ基準(1): 既存の技巧形式 book.bin の挙動が変わっていないことを、原本のバイナリと比較して確認する.

1. cshogi で棋譜DB（kifu_db.txt。技巧の形式）を作り、--create-book で技巧形式の定跡を作る
2. 原本（upstream-original = ca25edf）のバイナリと、現在のバイナリに同じ定跡・同じ局面を与え、
   定跡の候補手の info 出力（決定的）が完全に一致し、bestmove が候補手の中にあることを確認する

使い方: python3 gikou_book_regression.py <current engine> <upstream engine> <作業ディレクトリ>
"""
import os
import random
import subprocess
import sys

import cshogi

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from yanebook_util import run_engine  # noqa: E402

CURRENT, UPSTREAM, WORK = (os.path.abspath(p) for p in sys.argv[1:4])
os.makedirs(WORK, exist_ok=True)
rng = random.Random(7)

# 1. 棋譜DB: いくつかの定跡手順を共有する対局を作る（同じ手が2回以上・勝ち1回以上で定跡に登録される）
LINES = [
    "7g7f 3c3d 2g2f 8c8d 2f2e 8d8e 6i7h 4a3b",
    "7g7f 8c8d 2g2f 8d8e 8h7g 3c3d 7i8h 4a3b",
    "2g2f 8c8d 2f2e 8d8e 6i7h 4a3b 2e2d 2c2d",
    "7g7f 3c3d 6g6f 8c8d 1g1f 1c1d 2h6h 7a6b",
]
games = []
for g in range(80):
    b = cshogi.Board()
    moves = []
    line = LINES[g % len(LINES)].split()[:rng.randint(4, 8)]
    for usi in line:
        m = b.move_from_usi(usi)
        moves.append(cshogi.move_to_csa(m))
        b.push(m)
    for _ in range(rng.randint(20, 60)):
        legal = list(b.legal_moves)
        if not legal:
            break
        m = rng.choice(legal)
        moves.append(cshogi.move_to_csa(m))
        b.push(m)
    result = 1 + (g // 4) % 2  # 先手勝ち・後手勝ちを交互に
    games.append(f"{g} 2017/01/01 A B {result} {len(moves)} 名人戦 矢倉\n{''.join(moves)}\n")
with open(os.path.join(WORK, "kifu_db.txt"), "w", encoding="utf-8") as f:
    f.write("".join(games))

os.makedirs(os.path.join(WORK, "books"), exist_ok=True)
subprocess.run([CURRENT, "--create-book", "books"], cwd=WORK, check=True, stdout=subprocess.DEVNULL)
book = "books/00_全戦型.bin"
assert os.path.getsize(os.path.join(WORK, book)) > 1000

# 2. 原本と比較する
positions = ["startpos"]
for line in LINES:
    moves = line.split()
    for n in range(1, len(moves) + 1):
        positions.append("startpos moves " + " ".join(moves[:n]))
positions.append("startpos moves 1g1f")  # 定跡にない局面
positions = list(dict.fromkeys(positions))

option_sets = [{}, {"NarrowBook": "true"}, {"TinyBook": "true"}, {"BookMaxPly": "3"}, {"OwnBook": "false"}]
hits = 0
for options in option_sets:
    for position in positions:
        cmds = [f"setoption name BookFile value {book}"]
        cmds += [f"setoption name {k} value {v}" for k, v in options.items()]
        cmds += ["isready", f"position {position}", "go btime 0 wtime 0 byoyomi 100"]
        results = []
        for engine in (UPSTREAM, CURRENT):
            out = run_engine(engine, cmds, WORK)
            listing = [l for l in out if l.startswith("info time 0 depth")]
            best = [l for l in out if l.startswith("bestmove")]
            assert best, out
            results.append((listing, best[0].split()[1]))
        (old_listing, old_best), (new_listing, new_best) = results
        # 候補手の一覧（最後の1行は選ばれた手の再出力なので除く）は決定的なので完全一致する
        assert old_listing[:-1] == new_listing[:-1], (options, position, old_listing, new_listing)
        if new_listing:
            hits += 1
            candidates = {l.split(" pv ")[1].split()[0] for l in new_listing}
            assert old_best in candidates and new_best in candidates, (position, candidates)
            assert new_listing[-1].split(" pv ")[1].split()[0] == new_best
        else:
            assert not old_listing
print(f"gikou book regression: {len(option_sets) * len(positions)} cases identical to upstream "
      f"({hits} book hits)")
assert hits >= 20, hits
