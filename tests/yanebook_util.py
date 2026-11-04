"""G2 のテスト用ユーティリティ: やねうら王定跡（.db / .ybb）の書き出しと、USI エンジンの実行.

.ybb の書き出しは、やねうら王 source/book/makebook2025.cpp の形式（ヘッダ32バイト + 索引44バイト×N + 指し手）を
Python で独立に実装したもの。局面のキー（PackedSfen）は cshogi の to_psfen()、指し手は cshogi の move16_to_psv() で作る。
"""
import struct
import subprocess

import cshogi
import numpy as np

YBB_MAGIC = b"YANE-BINBOOK-V1\0"


def flip_sfen(sfen):
    """先後反転した局面の SFEN（手数は1）"""
    board, stm, hand = sfen.split()[:3]
    rows = board.split('/')
    flipped_rows = []
    for row in reversed(rows):
        cells = []
        i = 0
        while i < len(row):
            c = row[i]
            if c == '+':
                cells.append('+' + row[i + 1].swapcase())
                i += 2
                continue
            if c.isdigit():
                cells.extend(['1'] * int(c))
            else:
                cells.append(c.swapcase())
            i += 1
        cells.reverse()
        # 空きマスをまとめ直す
        out, empty = '', 0
        for cell in cells:
            if cell == '1':
                empty += 1
            else:
                if empty:
                    out += str(empty)
                    empty = 0
                out += cell
        if empty:
            out += str(empty)
        flipped_rows.append(out)
    new_hand = '-' if hand == '-' else hand.swapcase()
    # cshogi で持ち駒の順序を正規化する
    return cshogi.Board(f"{'/'.join(flipped_rows)} {'w' if stm == 'b' else 'b'} {new_hand} 1").sfen()


def flip_usi(move):
    def sq(s):
        return f"{10 - int(s[0])}{chr(ord('a') + ord('i') - ord(s[1]))}"
    if move[1] == '*':
        return move[:2] + sq(move[2:4])
    return sq(move[0:2]) + sq(move[2:4]) + move[4:]


def move16(usi):
    """USI → やねうら王 Move16（cshogi の move16_to_psv を使う。盤面に依存しない形式）"""
    if usi in ('none', ''):
        return 0
    to = lambda s: (ord(s[0]) - ord('1')) * 9 + (ord(s[1]) - ord('a'))
    if usi[1] == '*':
        pt = {'P': 1, 'L': 2, 'N': 3, 'S': 4, 'B': 5, 'R': 6, 'G': 7}[usi[0]]
        m = to(usi[2:4]) | (pt << 7) | (1 << 14)
    else:
        m = to(usi[2:4]) | (to(usi[0:2]) << 7) | ((1 << 15) if usi.endswith('+') else 0)
    return m


def check_move16_against_cshogi(sfen, usi):
    b = cshogi.Board(sfen)
    m = b.move_from_usi(usi)
    assert cshogi.move16_to_psv(cshogi.move16(m)) == move16(usi), usi


def write_db(path, entries, header=True):
    """entries: [(sfen_with_ply, [(move, ponder, value, depth, count), ...]), ...]"""
    with open(path, 'w', newline='\n') as f:
        if header:
            f.write("#YANEURAOU-DB2016 1.00\n")
        for sfen, moves in entries:
            f.write(f"sfen {sfen}\n")
            for m in moves:
                f.write(' '.join(str(x) for x in m) + "\n")


def packed(sfen):
    buf = np.zeros(32, dtype=np.uint8)
    cshogi.Board(sfen).to_psfen(buf)
    return bytes(buf)


def write_ybb(path, entries, with_depth=True):
    """entries は write_db と同じ形式. 同じ局面は1つにまとめる（後の指し手で上書き）"""
    records = {}
    for sfen, moves in entries:
        key = packed(sfen)
        ply = int(sfen.split()[3]) if len(sfen.split()) > 3 else 0
        rec = records.setdefault(key, [ply, {}])
        for m in moves:
            rec[1][m[0]] = m
    keys = sorted(records)
    header = YBB_MAGIC + struct.pack('<QQ', len(keys), 1 if with_depth else 0)
    index, body, body_size = [], [], 0
    record_size = 6 if with_depth else 4
    for key in keys:
        ply, moves = records[key]
        index.append(key + struct.pack('<QHH', body_size, ply, len(moves)))
        for move, _ponder, value, depth, _count in moves.values():
            body.append(struct.pack('<Hh', move16(move), value))
            if with_depth:
                body.append(struct.pack('<H', depth))
            body_size += record_size
    with open(path, 'wb') as f:
        f.write(header + b''.join(index) + b''.join(body))


def run_engine(engine, commands, cwd, timeout=60):
    """USI コマンドを流して、標準出力の行のリストを返す"""
    script = "\n".join(commands + ["quit"]) + "\n"
    p = subprocess.run([engine], input=script.encode(), cwd=cwd, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, timeout=timeout)
    return p.stdout.decode('utf-8', 'replace').splitlines()


def go_book(engine, cwd, book, position, options=None, extra=None):
    """定跡を設定して1回 go し、(bestmove, ponder, 出力行) を返す"""
    cmds = [f"setoption name BookFile value {book}"]
    for k, v in (options or {}).items():
        cmds.append(f"setoption name {k} value {v}")
    cmds += ["isready", f"position {position}", "go btime 0 wtime 0 byoyomi 200"]
    cmds += extra or []
    out = run_engine(engine, cmds, cwd)
    best = [l for l in out if l.startswith("bestmove")]
    assert best, out
    parts = best[0].split()
    return parts[1], (parts[3] if len(parts) >= 4 else None), out
