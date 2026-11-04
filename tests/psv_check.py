"""G1: --convert-psv の結果を、Python の独立実装と照合する.

1. cshogi でランダム対局を行い、やねうら王 PSV（40 bytes/レコード）を作る
   （後手番・成駒・後手の手駒・駒落ち（駒箱）・引き分け・非合法手・壊れたレコードを含む）
2. 技巧に --convert-psv させる
3. 技巧の HuffmanCode / Move（32bit）/ TeacherPosition の形式を Python で独立に実装し、
   期待される teacher_positions.bin / teacher_games.bin を作ってバイト一致を確認する

使い方: python3 psv_check.py make <out.psv>
        python3 psv_check.py check <in.psv> <positions.bin> <games.bin> <scale> <eval_limit>
"""
import math
import random
import struct
import sys

import cshogi
import numpy as np

KNOWN_WIN = 31000

# --- cshogi の駒番号（Apery 方式: 歩1 香2 桂3 銀4 角5 飛6 金7 玉8、成り+8、後手+16）→ 技巧の駒 ---
CSHOGI_TO_GIKOU_TYPE = {1: 1, 2: 2, 3: 3, 4: 4, 5: 6, 6: 7, 7: 5, 8: 8,
                        9: 9, 10: 10, 11: 11, 12: 12, 13: 14, 14: 15}


def gikou_piece(cp):
    """cshogi の駒 → 技巧の Piece（16*手番 + PieceType）"""
    if cp == 0:
        return 0
    color = 1 if cp >= 17 else 0
    return 16 * color + CSHOGI_TO_GIKOU_TYPE[cp & 15]


# 技巧のハフマン符号（src/huffman_code.cc の g_huffman_code_table。技巧の PieceType で引く）
GIKOU_HUFFMAN = {0: (0x00, 1), 1: (0x01, 2), 2: (0x03, 4), 3: (0x0b, 4), 4: (0x07, 4),
                 5: (0x0f, 5), 6: (0x1f, 6), 7: (0x3f, 6)}
GOLD = 5


class Bits:
    def __init__(self):
        self.bits = []

    def put(self, v, n):
        for i in range(n):
            self.bits.append((v >> i) & 1)

    def to_bytes(self):
        assert len(self.bits) == 256, len(self.bits)
        out = bytearray(32)
        for i, b in enumerate(self.bits):
            out[i // 8] |= b << (i % 8)
        return bytes(out)


def gikou_huffman(board):
    """技巧の HuffmanCode::EncodePosition() の独立実装（手番→先手玉→後手玉→盤上→持ち駒）"""
    pieces = [gikou_piece(p) for p in board.pieces]  # cshogi のマス番号も 0=1一 … 80=9九
    bits = Bits()
    bits.put(board.turn, 1)
    bits.put(pieces.index(8), 7)
    bits.put(pieces.index(24), 7)
    for p in pieces:
        if p & 15 == 8:
            continue
        if p == 0:
            bits.put(0, 1)
            continue
        pt = p & 15
        code, n = GIKOU_HUFFMAN[pt & 7]
        bits.put(code, n)
        bits.put(p >> 4, 1)                 # 先後フラグ（技巧は先後→成りの順）
        if pt != GOLD:
            bits.put(1 if pt & 8 else 0, 1)  # 成りフラグ
    hands = board.pieces_in_hand  # [先手, 後手] の各 [歩,香,桂,銀,金,角,飛]
    for color in (0, 1):
        for pt, n in zip((1, 2, 3, 4, 5, 6, 7), hands[color]):
            code, length = GIKOU_HUFFMAN[pt]
            for _ in range(n):
                bits.put(code >> 1, length - 1)
                bits.put(color, 1)
                if pt != GOLD:
                    bits.put(0, 1)
    return bits.to_bytes()


def gikou_move(board, usi):
    """技巧の Move（32bit）の独立実装。合法手でなければ 0（kMoveNone）"""
    legal = {cshogi.move_to_usi(m) for m in board.legal_moves}
    if usi not in legal:
        return 0
    stm = board.turn
    def sq(s):
        return (ord(s[0]) - ord('1')) * 9 + (ord(s[1]) - ord('a'))
    if usi[1] == '*':
        pt = {'P': 1, 'L': 2, 'N': 3, 'S': 4, 'G': 5, 'B': 6, 'R': 7}[usi[0]]
        return (1 << 15) | ((16 * stm + pt) << 16) | sq(usi[2:4])
    frm, to = sq(usi[0:2]), sq(usi[2:4])
    moved = gikou_piece(board.pieces[frm])
    captured = gikou_piece(board.pieces[to])
    promo = 1 if usi.endswith('+') else 0
    return (moved << 16) | (frm << 8) | to | (promo << 7) | (captured << 21)


def round_half_away(x):
    return int(math.copysign(math.floor(abs(x) + 0.5), x))


def make(path):
    rng = random.Random(2026)
    records = []
    buf = np.zeros(32, dtype=np.uint8)
    starts = [cshogi.STARTING_SFEN,
              "lnsgkgsn1/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL w - 1",  # 香落ち（駒箱あり）
              ]
    for game in range(60):
        board = cshogi.Board(starts[1] if game % 10 == 9 else starts[0])
        for ply in range(rng.randint(20, 160)):
            moves = list(board.legal_moves)
            if not moves:
                break
            move = rng.choice(moves)
            if rng.random() < 0.15:
                board.to_psfen(buf)
                score = rng.randint(-5000, 5000)
                m16 = cshogi.move16_to_psv(cshogi.move16(move))
                if rng.random() < 0.05:
                    m16 = (m16 + 1) & 0x7f7f  # 非合法手になりやすい値
                result = rng.choice([1, 0, -1])
                records.append(struct.pack('<32shHHbB', bytes(buf), score, m16, ply + 1, result, 0))
            board.push(move)
    records.append(struct.pack('<32shHHbB', b'\xff' * 32, 0, 0, 1, 1, 0))  # 壊れたレコード
    with open(path, 'wb') as f:
        f.write(b''.join(records))
    print(f"psv_check: wrote {len(records)} records to {path}")


def check(psv_path, positions_path, games_path, scale, eval_limit):
    data = open(psv_path, 'rb').read()
    exp_pos, exp_games = [], []
    skipped = 0
    for off in range(0, len(data), 40):
        psfen, score, m16, _ply, result, _pad = struct.unpack('<32shHHbB', data[off:off + 40])
        if psfen == b'\xff' * 32:
            skipped += 1  # 壊れたレコード（cshogi に渡すとクラッシュするので先に除く）
            continue
        board = cshogi.Board()
        board.set_psfen(np.frombuffer(psfen, dtype=np.uint8))
        if sum(sum(h) for h in board.pieces_in_hand) + \
                sum(1 for p in board.pieces if p) != 40:
            skipped += 1  # 駒箱に駒がある局面はスキップされる
            continue
        huff = gikou_huffman(board)
        usi = cshogi.move_to_usi(board.move_from_psv(m16)) if m16 else ''
        move = gikou_move(board, usi)
        s = round_half_away(score * scale)
        if eval_limit <= 0 or abs(s) <= eval_limit:
            exp_pos.append(struct.pack('<32sIi', huff, move, s))
        if result != 0:
            exp_games.append(struct.pack('<32sIi', huff, move, KNOWN_WIN if result > 0 else -KNOWN_WIN))
    got_pos = open(positions_path, 'rb').read()
    got_games = open(games_path, 'rb').read()
    assert len(got_pos) == 40 * len(exp_pos), (len(got_pos) // 40, len(exp_pos))
    assert len(got_games) == 40 * len(exp_games), (len(got_games) // 40, len(exp_games))
    for i, e in enumerate(exp_pos):
        assert got_pos[40 * i:40 * i + 40] == e, f"teacher_positions record {i} differs"
    for i, e in enumerate(exp_games):
        assert got_games[40 * i:40 * i + 40] == e, f"teacher_games record {i} differs"
    print(f"psv_check: byte-identical (rootstrap={len(exp_pos)}, regression={len(exp_games)}, "
          f"skipped={skipped})")


if __name__ == '__main__':
    if sys.argv[1] == 'make':
        make(sys.argv[2])
    else:
        check(sys.argv[2], sys.argv[3], sys.argv[4], float(sys.argv[5]), int(sys.argv[6]))
