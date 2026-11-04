"""--evaldump の出力形式を確認する."""
import json
import sys

positions = [l.split(None, 1)[1].strip() for l in open(sys.argv[1])]
records = [json.loads(l) for l in open(sys.argv[2])]
assert len(records) == len(positions), (len(records), len(positions))
keys = ["kp_black", "kp_white", "two_pieces", "controls", "king_safety", "sliders", "tempo"]
nonzero = 0
for pos, r in zip(positions, records):
    assert "error" not in r, r
    # 手数以外は入力と同じ局面であること
    assert r["sfen"].rsplit(" ", 1)[0] == pos.rsplit(" ", 1)[0], (r["sfen"], pos)
    assert r["side_to_move"] == pos.split()[1]
    assert r["score_black"] == (r["score"] if r["side_to_move"] == "b" else -r["score"])
    assert 0.0 <= r["progress"] <= 1.0
    for k in keys:
        assert len(r[k]) == 4 and all(isinstance(v, int) for v in r[k]), k
    nonzero += r["score"] != 0
assert nonzero > len(records) // 2, "scores are mostly zero; params.bin not loaded?"
print(f"check_evaldump: {len(records)} records OK")
