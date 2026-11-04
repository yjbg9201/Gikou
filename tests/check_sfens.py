"""--randsfen の出力を cshogi（独立実装）で読み直し、合法局面であることと SFEN 表記の一致を確認する."""
import sys
import cshogi

n = 0
for line in open(sys.argv[1]):
    sfen = line.split(None, 1)[1].strip()
    board = cshogi.Board(sfen)
    # cshogi が出力し直した SFEN と完全一致すること（持ち駒の順序・手数を含む）
    assert board.sfen() == sfen, (board.sfen(), sfen)
    # 手番でない側の玉に王手がかかっていないこと（= 合法な局面）
    assert board.is_ok(), sfen
    n += 1
print(f"check_sfens: {n} positions OK")
