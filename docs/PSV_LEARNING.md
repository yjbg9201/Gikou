# やねうら王 PSV 教師による学習（G1）

`docs/gikou-ideas.md` の **G1**。技巧の評価関数学習（`--learn-with-rootstrap` / `--learn-with-regression`）の
**補助損失**（RootStrap と勝敗のロジスティック回帰）の教師を、やねうら王の PackedSfenValue（PSV、40 bytes/レコード）
から直接読めるようにしました。**主損失（棋譜の指し手との比較学習）には手を入れていない**ので、
これまでどおり `kifu_db.txt` が必要です。

> 過去チャットで作ったパッチ（`gikou_psv_learning.patch`）の原本は手元になかったため、仕様書の記述をもとに
> 再実装しました。検証は cshogi（独立実装）との照合で行っています。

## コマンド

```
./release --learn-psv           <file.psv> [scale=1.0] [eval_limit=3000] [max=0]
./release --learn-psv-rootstrap <file.psv> [scale=1.0] [eval_limit=3000] [max=0]
./release --convert-psv         <file.psv> [scale=1.0] [eval_limit=3000] [max=0]
                                [positions=teacher_positions.bin] [games=teacher_games.bin]
./release --psv-selftest
```

| コマンド | 相当する既存コマンド | 内容 |
| --- | --- | --- |
| `--learn-psv` | `--learn-with-regression` | RootStrap と勝敗回帰の教師を、どちらも PSV から作って学習 |
| `--learn-psv-rootstrap` | `--learn-with-rootstrap` | RootStrap の教師を PSV から作って学習 |
| `--convert-psv` | — | PSV を `teacher_positions.bin`（RootStrap 用）と `teacher_games.bin`（勝敗回帰用）に変換。既存の `--learn-with-*` でそのまま使える |
| `--psv-selftest` | — | PackedSfen / Move16 / 変換規則のセルフテスト |

| オプション | 既定 | 意味 |
| --- | --- | --- |
| `scale=` | 1.0 | 技巧の score = round(PSV の score × scale)（四捨五入は0から遠い側） |
| `eval_limit=` | 3000 | RootStrap 用の教師から、\|score\|（scale 適用後）がこれを超える局面を除外。0 で無制限 |
| `max=` | 0 | 先頭から読み込む最大レコード数（0 で全部） |

### scale の目安

技巧・やねうら王とも、評価値の勝率換算は sigmoid(v/600) なので既定は 1.0。
駒割の基準を合わせたい場合は 1.11（技巧は歩=100 で学習、やねうら王の PawnValue=90）。

## 変換規則

| 項目 | RootStrap 用（teacher_positions.bin 相当） | 勝敗回帰用（teacher_games.bin 相当） |
| --- | --- | --- |
| 局面 | PSV の PackedSfen を復号し、技巧の HuffmanCode に再符号化 | 同左 |
| score | round(score × scale)。**符号反転なし**（PSV も技巧も手番視点） | 手番側の勝ち → +31000（kScoreKnownWin）、負け → −31000 |
| 引き分け（game_result=0） | **残す**（RootStrap は勝敗を使わない） | **除外**（技巧の teacher_games.bin と同じ） |
| eval_limit | **適用する** | **適用しない**（技巧の原仕様どおり） |
| move | Move16 を USI 文字列経由で技巧の Move に変換。非合法なら kMoveNone（局面は使う）。学習では使われない | 同左 |

スキップするレコード:

- 駒箱に駒がある局面（駒落ちなど）。技巧の HuffmanCode では表現できないため。
- 復号できないビット列、駒数超過、手番でない側の玉に王手がかかっている局面など。

変換の統計（読み込み数・スキップ数・教師数）は実行時に表示されます。

## 付随修正: 教師局面不足のガード

学習ループのミニバッチは復元抽出（`std::iter_swap` によるシャッフル）で作るため、教師局面がバッチサイズ未満だと
範囲外アクセス（未定義動作）になっていました。学習開始前に検査し、明示的なエラーで終了するようにしました。

- RootStrap 用の教師が 80,000 局面未満
- 勝敗回帰用の教師が 80,000 局面未満
- 棋譜DB（kifu_db.txt）の局面が 8,000 局面未満

`teacher_positions.bin` / `teacher_games.bin` が開けない場合も、クラッシュせずエラーで終了します
（これらは失敗時の挙動のみの変更で、正常時の学習結果は変わりません）。

## PackedSfen について（重要）

技巧の `HuffmanCode`（`src/huffman_code.cc`）は、やねうら王の PackedSfen と**ビット一致しません**。
盤上の駒について、技巧は「先後フラグ→成りフラグ」、やねうら王は「成りフラグ→先後フラグ」の順に書くためです
（平手初期局面でも後手の駒で食い違います）。そこで、やねうら王互換の符号化・復号を
`src/sfen_packer.h/.cc` に独立に実装しました（G2 の .ybb 定跡でも使います）。

## 検証

- `--psv-selftest`: cshogi 1.1.0 の `Board.to_psfen()` で生成した 40 局面（後手の成駒・後手の金以外の手駒・駒落ちを含む）
  とのバイト一致、`move16_to_psv()` で生成した 59 手との一致、変換規則（scale、引き分け、eval_limit、駒箱、壊れたビット列、
  不正局面）。ベクトルは `tests/gen_psv_vectors.py` で再生成できます。
- `tests/test_20_psv.sh`（CI）:
  - `--randsfen` の 200 局面について `--sfen-pack` と cshogi `to_psfen()` のバイト一致、`--sfen-unpack` の往復
  - cshogi で作った PSV（935 レコード）を `--convert-psv` し、技巧の HuffmanCode / Move / TeacherPosition を
    Python で独立に実装した `tests/psv_check.py` の期待値と、3 通りのオプションで**バイト一致**
  - 教師不足ガード、不正なオプション・壊れたファイルでのエラー終了
