"""やねうら王互換 PackedSfen / Move16 のテストベクトル（src/psv_selftest_vectors.inc）を cshogi で生成する.

使い方: python3 tests/gen_psv_vectors.py > vectors.inc
"""
import cshogi, numpy as np, random
random.seed(12345)
sfens = [
  "lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1",
  "lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL w - 1",
  # 後手の成駒・後手の金以外の手駒を含む局面（技巧/Apery/やねうら王の差が出る）
  "ln1g3nl/1r1sk1g2/p1pppp1pp/6p2/9/2P1P4/PPSP1PPPP/2G2S1R1/LN2KG1NL w BPbs 30",
  "8l/1l+R2P3/p2pBG1pp/kps1p4/Nn1P2G2/P1P1P2PP/1PS6/1KSG3+r1/LN2+p3L w Sbgn2p 120",
  "+B+R5n1/5gk2/p1pps1g2/5ppp1/1p2p1PbP/2P6/PPSPPPN2/2GK1S3/LN1G4+r b SNLPlp 80",
  # 駒落ち（駒箱あり）
  "lnsgkgsn1/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL w - 1",
  "lnsgkgsnl/9/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL w - 1",
  "4k4/9/9/9/9/9/9/9/4K4 b - 1",
]
# ランダム対局局面（成駒・持ち駒を多く含むように長手数）
b = cshogi.Board()
while len(sfens) < 40:
    b.reset()
    for _ in range(random.randint(40, 200)):
        moves = list(b.legal_moves)
        if not moves: break
        b.push(random.choice(moves))
    sfens.append(b.sfen())
buf = np.zeros(32, dtype=np.uint8)
print("const struct { const char* sfen; const char* packed; } kPackedSfenVectors[] = {")
for s in sfens:
    bb = cshogi.Board(s)
    assert bb.is_ok(), s
    bb.to_psfen(buf)
    print(f'  {{"{s}",\n   "{bytes(buf).hex()}"}},')
print("};")
# Move16 vectors
b = cshogi.Board()
mv = set()
for _ in range(30):
    b.reset()
    for _ in range(random.randint(10, 150)):
        moves = list(b.legal_moves)
        if not moves: break
        m = random.choice(moves)
        mv.add((cshogi.move_to_usi(m), cshogi.move16_to_psv(cshogi.move16(m))))
        b.push(m)
mv = sorted(mv)
drops = [x for x in mv if '*' in x[0]]; proms=[x for x in mv if x[0].endswith('+')]
sel = mv[:20] + drops[:20] + proms[:20]
print("const struct { const char* usi; uint16_t move16; } kMove16Vectors[] = {")
for u, m in sorted(set(sel)):
    print(f'  {{"{u}", {m}}},')
print("};")
