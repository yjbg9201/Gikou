"""G2: やねうら王定跡（.db / .ybb）読み込みのシナリオテスト.

使い方: python3 yanebook_test.py <engine> <作業ディレクトリ>
"""
import os
import random
import sys

import cshogi

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from yanebook_util import (check_move16_against_cshogi, flip_sfen, flip_usi, go_book,  # noqa: E402
                           run_engine, write_db, write_ybb)

ENGINE, WORK = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
os.makedirs(WORK, exist_ok=True)

STARTPOS = "lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1"


def sfen_after(moves):
    b = cshogi.Board()
    for m in moves:
        b.push_usi(m)
    return b.sfen()


def book_hit(out):
    """定跡にヒットして探索しなかったか（探索すると "info depth ..." の読み筋が出る。定跡は "info time 0 depth ..."）"""
    return not any(l.startswith("info depth ") for l in out)


def score_of(out, move):
    """選ばれた手（最後に出力される info 行）の評価値"""
    lines = [l for l in out if l.startswith("info time 0") and f" pv {move}" in l]
    assert lines, out
    return int(lines[-1].split(" score cp ")[1].split()[0])


passed = 0


def ok(name):
    global passed
    passed += 1
    print(f"  ok: {name}")


# ---------------------------------------------------------------------------
# 定跡の内容
# ---------------------------------------------------------------------------
P_WHITE = sfen_after(["7g7f"])                       # 後手番
P_FLIP_SRC = sfen_after(["2g2f", "8c8d", "7g7f"])     # 後手番。この局面は反転形だけを定跡に入れる
P_FLIP_STORED = flip_sfen(P_FLIP_SRC)                 # 先手番
P_ILLEGAL = sfen_after(["2g2f"])                      # 非合法手が混入した局面
P_NONPROMO = "4k4/9/9/9/9/9/9/4R4/4K4 b - 1"          # 飛車の不成（技巧の指し手生成では生成されない合法手）。駒箱あり
P_DUP = sfen_after(["5g5f"])                          # 手数違いの重複エントリー
P_LIMIT = sfen_after(["9g9f"])                        # 後手の評価値下限
assert cshogi.Board(P_FLIP_STORED).is_ok()

# 反転局面で合法な手を選んで登録する（現局面ではその反転手が期待される）
flip_legal = sorted(cshogi.move_to_usi(m) for m in cshogi.Board(P_FLIP_STORED).legal_moves)
FLIP_MOVE = "3g3f" if "3g3f" in flip_legal else flip_legal[0]

DB_ENTRIES = [
    (STARTPOS, [("7g7f", "3c3d", 50, 20, 10), ("2g2f", "8c8d", 40, 20, 5), ("5g5f", "none", 10, 20, 1)]),
    (P_WHITE, [("8c8d", "none", -100, 20, 3), ("3c3d", "none", -200, 20, 2)]),
    (P_FLIP_STORED.replace(" 1", " 4"), [(FLIP_MOVE, "none", 77, 20, 1)]),
    (P_ILLEGAL.replace(" 2", " 9"), [("7g7f", "none", 500, 20, 100), ("8c8d", "none", 10, 20, 1)]),
    (P_NONPROMO, [("5h5b", "none", 300, 20, 1)]),
    (P_DUP, [("8c8d", "none", 0, 20, 1)]),
    (P_DUP.replace(" 2", " 30"), [("3c3d", "none", -5, 20, 1)]),
    (P_LIMIT, [("1c1d", "none", -150, 20, 1)]),
]
# テスト側の Move16 変換も cshogi と照合しておく（合法手のみ）
for sfen, moves in DB_ENTRIES:
    legal = {cshogi.move_to_usi(m) for m in cshogi.Board(sfen).legal_moves}
    for m in moves:
        if m[0] in legal:
            check_move16_against_cshogi(sfen, m[0])

write_db(f"{WORK}/main.db", DB_ENTRIES)
write_ybb(f"{WORK}/main.ybb", DB_ENTRIES)


def scenarios(book, fmt, options=None):
    opts = dict(options or {})
    det = dict(opts, BookEvalDiff=0)  # 最善手だけを選ぶ（決定的）

    # 1. 平手（先手番）: 最善の評価値の手、評価値の符号はそのまま
    best, ponder, out = go_book(ENGINE, WORK, book, "startpos", det)
    assert best == "7g7f" and book_hit(out), out
    assert score_of(out, "7g7f") == 50
    if fmt == "db":
        assert ponder == "3c3d", out           # 定跡に登録された予想手
    else:
        assert ponder == "8c8d", out           # .ybb は予想手を持たないので、次の局面の定跡手
    ok(f"[{fmt}] startpos best move and score sign")

    # 2. 後手番: 評価値は手番（後手）から見た値のまま
    best, _, out = go_book(ENGINE, WORK, book, "startpos moves 7g7f", det)
    assert best == "8c8d" and score_of(out, "8c8d") == -100, out
    ok(f"[{fmt}] white to move keeps the side-to-move score")

    # 3. FlippedBook: 反転局面にヒットし、指し手を反転して返す（評価値の符号は変えない）
    best, _, out = go_book(ENGINE, WORK, book, "startpos moves 2g2f 8c8d 7g7f", det)
    assert best == flip_usi(FLIP_MOVE) and book_hit(out), (best, FLIP_MOVE, out)
    assert score_of(out, best) == 77
    best, _, out = go_book(ENGINE, WORK, book, "startpos moves 2g2f 8c8d 7g7f", dict(det, FlippedBook="false"))
    assert not book_hit(out), out
    ok(f"[{fmt}] FlippedBook hit (and off)")

    # 4. 非合法手の混入: 除外して info string で通知し、合法な手を選ぶ
    best, _, out = go_book(ENGINE, WORK, book, "startpos moves 2g2f", det)
    assert best == "8c8d", out
    assert any("Illegal Move In Book DB : move = 7g7f" in l for l in out), out
    ok(f"[{fmt}] illegal book move is removed")

    # 5. 飛車の不成（合法だが技巧の指し手生成にない手）、駒箱のある局面のキー
    best, _, out = go_book(ENGINE, WORK, book, f"sfen {P_NONPROMO}", det)
    assert best == "5h5b", out
    ok(f"[{fmt}] non-promotion and handicap key")

    # 6. BookEvalDiff（既定30）: 最善手から30以内の手のみ
    seen = set()
    for _ in range(12):
        best, _, out = go_book(ENGINE, WORK, book, "startpos", opts)
        seen.add(best)
    assert seen <= {"7g7f", "2g2f"} and len(seen) == 2, seen
    assert any("BookEvalDiff = 30 , BookEvalBlackLimit = 0 , 3 moves to 2 moves." in l for l in out), out
    ok(f"[{fmt}] BookEvalDiff")

    # 7. BookEvalBlackLimit / BookEvalWhiteLimit
    best, _, out = go_book(ENGINE, WORK, book, "startpos", dict(det, BookEvalBlackLimit=60))
    assert not book_hit(out) and any("3 moves to 0 moves" in l for l in out), out
    best, _, out = go_book(ENGINE, WORK, book, "startpos moves 9g9f", det)
    assert not book_hit(out), out                       # -150 < -140
    best, _, out = go_book(ENGINE, WORK, book, "startpos moves 9g9f", dict(det, BookEvalWhiteLimit=-200))
    assert best == "1c1d" and book_hit(out), out
    ok(f"[{fmt}] BookEvalBlackLimit / BookEvalWhiteLimit")

    # 8. BookDepthLimit（depth 20 < 25 なら使わない。0 なら無視）
    best, _, out = go_book(ENGINE, WORK, book, "startpos", dict(det, BookDepthLimit=25))
    assert not book_hit(out) and any("BookDepthLimit is lower" in l for l in out), out
    best, _, out = go_book(ENGINE, WORK, book, "startpos", dict(det, BookDepthLimit=0))
    assert book_hit(out), out
    ok(f"[{fmt}] BookDepthLimit")

    # 9. BookIgnoreRate=100 / OwnBook=false なら使わない
    _, _, out = go_book(ENGINE, WORK, book, "startpos", dict(det, BookIgnoreRate=100))
    assert not book_hit(out), out
    _, _, out = go_book(ENGINE, WORK, book, "startpos", dict(det, OwnBook="false"))
    assert not book_hit(out), out
    ok(f"[{fmt}] BookIgnoreRate / OwnBook")

    # 10. BookMaxPly（手数の条件は従来どおり。上限は10000に拡張）
    _, _, out = go_book(ENGINE, WORK, book, "startpos moves 7g7f", dict(det, BookMaxPly=1))
    assert not book_hit(out), out
    _, _, out = go_book(ENGINE, WORK, book, "startpos moves 7g7f", dict(det, BookMaxPly=10000))
    assert book_hit(out), out
    ok(f"[{fmt}] BookMaxPly")

    # 11. 手数違いの重複エントリーはまとめる（手数を無視して照合）
    best, _, out = go_book(ENGINE, WORK, book, "startpos moves 5g5f", dict(opts, BookEvalDiff=100))
    pvs = {l.split(" pv ")[1].split()[0] for l in out if l.startswith("info time 0")}
    assert pvs == {"8c8d", "3c3d"}, out
    ok(f"[{fmt}] duplicate positions with different ply are merged")


print("yanebook: .db")
scenarios("main.db", "db")

# .db 固有: NarrowBook / ConsiderBookMoveCount（採択回数は .db にしかない）
_, _, out = go_book(ENGINE, WORK, "main.db", "startpos", {"BookEvalDiff": 100, "NarrowBook": "true"})
assert any("NarrowBook : 3 moves to 2 moves." in l for l in out), out  # 1/16 < 10%
ok("[db] NarrowBook")
write_db(f"{WORK}/count.db", [(STARTPOS, [("7g7f", "none", 0, 20, 5), ("2g2f", "none", 0, 20, 0)])])
for _ in range(8):
    best, _, out = go_book(ENGINE, WORK, "count.db", "startpos", {"ConsiderBookMoveCount": "true"})
    assert best == "7g7f", out
ok("[db] ConsiderBookMoveCount")

# depth を持たない .db では BookDepthLimit を無視する
write_db(f"{WORK}/nodepth.db", [(STARTPOS, [("7g7f", "none", 0, 0, 1)])])
best, _, out = go_book(ENGINE, WORK, "nodepth.db", "startpos", {})
assert best == "7g7f" and any("no depth information" in l for l in out), out
ok("[db] book without depth ignores BookDepthLimit")

print("yanebook: .ybb (in memory)")
scenarios("main.ybb", "ybb")
print("yanebook: .ybb (BookOnTheFly)")
scenarios("main.ybb", "ybb", {"BookOnTheFly": "true"})

# depth を持たない .ybb
write_ybb(f"{WORK}/nodepth.ybb", [(STARTPOS, [("7g7f", "none", 0, 0, 1)])], with_depth=False)
best, _, out = go_book(ENGINE, WORK, "nodepth.ybb", "startpos", {})
assert best == "7g7f", out
ok("[ybb] book without depth ignores BookDepthLimit")

# .db が存在せず同名の .ybb がある場合は .ybb を読む
write_ybb(f"{WORK}/fallback.ybb", DB_ENTRIES)
best, _, out = go_book(ENGINE, WORK, "fallback.db", "startpos", {"BookEvalDiff": 0})
assert best == "7g7f" and any("fallback.ybb (ybb" in l for l in out), out
ok(".db -> .ybb fallback")

# 拡張子が .db でなくても、ヘッダで .db と判別する
write_db(f"{WORK}/book_db.txt", DB_ENTRIES)
best, _, out = go_book(ENGINE, WORK, "book_db.txt", "startpos", {"BookEvalDiff": 0})
assert best == "7g7f", out
ok("format detection by header")

# ---------------------------------------------------------------------------
# 破損ファイル: エンジンを落とさず、定跡なしで続行する
# ---------------------------------------------------------------------------
rng = random.Random(3)
with open(f"{WORK}/garbage.db", "wb") as f:
    f.write(bytes(rng.randrange(256) for _ in range(5000)))
with open(f"{WORK}/badheader.ybb", "wb") as f:
    f.write(b"NOT-A-YBB-FILE\0\0" + bytes(100))
good = open(f"{WORK}/main.ybb", "rb").read()
with open(f"{WORK}/truncated.ybb", "wb") as f:
    f.write(good[:len(good) - 7])
with open(f"{WORK}/badcount.ybb", "wb") as f:
    f.write(good[:16] + (10**12).to_bytes(8, "little") + good[24:])
with open(f"{WORK}/broken_lines.db", "w") as f:
    f.write("#YANEURAOU-DB2016 1.00\nsfen this is not sfen\n7g7f none 0 0 1\n"
            "sfen lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b 19P 1\n7g7f\n"
            f"sfen {STARTPOS}\nzzzz none\n7g7f none abc def\n\n")
# BookOnTheFly では全レコードを事前に検査できないので、壊れていないレコードは使い、
# 壊れたレコードは info string で通知して定跡なし扱いにする（expect_no_book=False）
for name, opts, expect_no_book in [
        ("garbage.db", {}, True), ("badheader.ybb", {}, True), ("truncated.ybb", {}, True),
        ("truncated.ybb", {"BookOnTheFly": "true"}, False), ("badcount.ybb", {}, True),
        ("badcount.ybb", {"BookOnTheFly": "true"}, True), ("missing.db", {}, True), ("missing.ybb", {}, True)]:
    for position in ["startpos", "startpos moves 7g7f", "startpos moves 5g5f"]:
        out = run_engine(ENGINE, [f"setoption name BookFile value {name}"]
                         + [f"setoption name {k} value {v}" for k, v in opts.items()]
                         + ["isready", f"position {position}", "go btime 0 wtime 0 byoyomi 200"], WORK)
        assert "readyok" in out and any(l.startswith("bestmove") for l in out), (name, out)
        if expect_no_book:
            assert not book_hit(out), (name, out)
ok("corrupted / missing files: engine keeps running")

best, _, out = go_book(ENGINE, WORK, "broken_lines.db", "startpos", {})
assert best == "7g7f" and book_hit(out), out   # 壊れた行は読み飛ばし、正しい行は使う
assert any("invalid positions" in l for l in out), out
ok("broken lines in .db are skipped")

# USI オプションの一覧
out = run_engine(ENGINE, ["usi"], WORK)
for expected in ["option name BookMaxPly type spin default 20 min 0 max 10000",
                 "option name BookIgnoreRate type spin default 0 min 0 max 100",
                 "option name BookEvalDiff type spin default 30 min 0 max 99999",
                 "option name BookEvalBlackLimit type spin default 0 min -99999 max 99999",
                 "option name BookEvalWhiteLimit type spin default -140 min -99999 max 99999",
                 "option name BookDepthLimit type spin default 16 min 0 max 99999",
                 "option name ConsiderBookMoveCount type check default false",
                 "option name FlippedBook type check default true",
                 "option name BookOnTheFly type check default false",
                 "option name BookFile type filename default book.bin",
                 "option name OwnBook type check default true",
                 "option name NarrowBook type check default false"]:
    assert expected in out, (expected, out)
ok("USI options")

print(f"yanebook: {passed} checks passed")
