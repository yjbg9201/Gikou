# やねうら王定跡の読み込み（G2）

`docs/gikou-ideas.md` の **G2**（G2-a〜G2-e）。技巧の `BookFile` に、やねうら王形式の定跡
（`.db` テキスト形式、`.ybb` バイナリ形式。ペタショック定跡を含む）を指定できるようにしました。
ヒットした場合は探索せずに定跡手を返します。**既存の技巧形式 `book.bin` の挙動は変えていません**。

> 原本仕様書 `gikou_yaneuraou_book_spec.md` は手元になかったため、`docs/gikou-ideas.md` の記述と、
> やねうら王の `source/book/book.cpp`（`BookMoveSelector::probe_impl`、`MemoryBook`）を直接参照して実装しました。

## 使い方

```
setoption name BookFile value user_book1.db      # .db
setoption name BookFile value peta_shock.ybb     # .ybb
setoption name BookOnTheFly value true           # 巨大な .ybb をメモリに読まずに使う
isready
```

## 形式の判別（`YaneuraOuBook::DetectFormat`）

1. 先頭16バイトが `YANE-BINBOOK-V1\0` → `.ybb`
2. 先頭行が `#YANEURAOU-DB2016` で始まる → `.db`
3. 拡張子が `.db` → `.db`、`.ybb` → `.ybb`（ヘッダが不正なら読み込みエラー）
4. それ以外 → **技巧形式**（従来どおり `Book::ReadFromFile`）

`.db` を指定してファイルが存在せず、同名の `.ybb` があれば `.ybb` を読みます（やねうら王と同じ）。

## 照合

- 局面のキーは、やねうら王互換の **PackedSfen**（`src/sfen_packer.h`、32バイト）。
  `.db` は読み込み時に各 SFEN を検証（駒数超過などは読み飛ばす）して PackedSfen に変換し、ソート済みの索引にします。
  `.ybb` は索引をそのまま二分探索します（`memcmp` 順）。
- **手数は常に無視**します（やねうら王の `IgnoreBookPly=true` 相当。技巧の `Position::ToSfen()` は手数を持たないため）。
  手数違いの同一局面が `.db` に複数あれば1つにまとめ、同じ指し手は後のもので上書きして採択回数を合算します。
- **FlippedBook**（既定 true）: 見つからなければ先後反転した局面で探し、指し手を反転して返します。
  評価値は**手番視点なので反転しません**。
- 指し手（Move16）は **USI 文字列経由**で技巧の Move に変換し、現局面の合法手と照合します
  （金角飛の駒番号の違いを吸収。技巧の指し手生成が生成しない飛角歩の不成なども合法なら受け付けます）。

## 指し手の選択（やねうら王 `BookMoveSelector::probe_impl` の移植）

1. `BookIgnoreRate`（%）の確率で定跡を使わない
2. 定跡を探す（FlippedBook を含む）。指し手を「採択回数の降順 → 評価値の降順」に安定ソート
3. 非合法手を除き、`info string Error! : Illegal Move In Book DB : move = ... , sfen = ...` で通知
4. 候補手を `info time 0 depth D nodes 0 score cp V multipv N pv MOVE [PONDER]` で出力
5. `NarrowBook`: 採択率10%未満の手を除く（採択回数がある場合のみ）
6. `BookDepthLimit`: 先頭の手の depth が下限未満なら、この局面では定跡を使わない（0 なら無視）
7. `BookEvalDiff` / `BookEvalBlackLimit` / `BookEvalWhiteLimit`:
   評価値が `max(先頭の手の評価値 − BookEvalDiff, 手番側の下限)` 未満の手を除く
8. 残った手から一様に選ぶ（`ConsiderBookMoveCount` なら採択回数に比例して選ぶ）
9. 予想手（ponder）: 定跡に登録されていればそれ、なければ指した後の局面の定跡の先頭の手（合法手のみ）
10. 選んだ手の info 行をもう一度出力し（GUI がその手の評価値を表示するように）、`bestmove MOVE [ponder P]`

定跡を使う条件（`OwnBook`、`BookMaxPly`、`go infinite` / `ponder` / `searchmoves` でないこと）は従来と同じです。

### 評価値の視点

やねうら王の定跡・PSV は**手番視点**、技巧の book.bin は内部で**先手視点**に保存して `Book::Probe` で手番視点に戻しています。
やねうら王定跡はこの反転コードを通さず、手番視点のまま `score cp` に出力し、`BookEvalBlackLimit`（先手番のとき）・
`BookEvalWhiteLimit`（後手番のとき）と比較します。

## USI オプション

| オプション | 既定 | 範囲 | 備考 |
| --- | --- | --- | --- |
| `BookFile` | book.bin | | 既存。形式を自動判別 |
| `OwnBook` | true | | 既存 |
| `NarrowBook` | false | | 既存。やねうら王定跡では「採択率10%未満を除く」 |
| `BookMaxPly` | 20 | 0〜**10000** | 既存。上限を 50 から拡張（既定値は不変） |
| `BookIgnoreRate` | 0 | 0〜100 | 新規 |
| `BookEvalDiff` | 30 | 0〜99999 | 新規 |
| `BookEvalBlackLimit` | 0 | −99999〜99999 | 新規 |
| `BookEvalWhiteLimit` | −140 | −99999〜99999 | 新規 |
| `BookDepthLimit` | 16 | 0〜99999 | 新規。**depth 情報のない定跡**（.db で全手の depth が 0、depth フラグのない .ybb）では自動で無視 |
| `ConsiderBookMoveCount` | false | | 新規 |
| `FlippedBook` | true | | 新規 |
| `BookOnTheFly` | false | | 新規。`.ybb` をメモリに読まずディスク上で二分探索（`.db` は常に全読込） |

新規オプションは、技巧形式の book.bin には影響しません。`TinyBook` は技巧形式専用です。

## 破損ファイル

エンジンを落とさず、`info string` でエラーを表示して**定跡なしで続行**します。

- `.db`: 不正な SFEN・不正な指し手の行は読み飛ばし（件数を表示）、有効な局面が1つもなければ定跡なし
- `.ybb`: ヘッダ不正、未知のフラグ、レコード数がファイルサイズと矛盾、指し手の範囲がファイル外（全読込時は全レコードを検査）
  → 定跡なし。`BookOnTheFly` では読み込み時にヘッダだけ検査し、壊れたレコードに当たったらその局面だけ定跡なし扱い
- ファイルがない → `info string Failed to Open ...`（技巧形式と同じ表示）

## 性能

`isready` のたびに読み直さないよう、同じファイル（名前・サイズ・更新時刻）・同じモードなら再読込しません。

`--bench-yanebook <定跡> <局面ファイル> [onthefly]` で計測できます。cshogi のランダム対局で作った
100万局面・300万手の定跡（.db 140MB / .ybb 62MB）での計測値（クラウド環境の4コアVM、1000局面×10回の照合）:

| 形式 | 読み込み | 照合1回 |
| --- | --- | --- |
| .db（全読込） | 2.9 秒 | 3.4 µs |
| .ybb（全読込） | 0.06 秒 | 2.9 µs |
| .ybb（BookOnTheFly） | 0.00 秒 | 15 µs |

仕様の非機能要件（100万局面で読込数十秒以内、照合1回 1ms 未満）を満たしています。

## テスト

`tests/test_30_yanebook.sh`（CI）:

- `tests/yanebook_test.py`: `.db` / `.ybb`（全読込）/ `.ybb`（BookOnTheFly）それぞれで、平手・後手番（評価値の符号）・
  FlippedBook（ヒットと無効化）・非合法手の混入・飛車の不成・駒落ちのキー・BookEvalDiff・評価値の下限・BookDepthLimit・
  BookIgnoreRate・OwnBook・BookMaxPly・手数違いの重複、`.db` の NarrowBook / ConsiderBookMoveCount、depth のない定跡、
  `.db`→`.ybb` フォールバック、ヘッダによる形式判別、破損・欠損ファイル（7種）、USI オプション一覧。
  `.ybb` は makebook2025.cpp の形式を Python で独立に実装して書き出し、キーは cshogi の `to_psfen()`、
  指し手は `move16_to_psv()` と照合しています。
- `tests/gikou_book_regression.py`: cshogi で作った棋譜DBから `--create-book` で技巧形式の定跡を作り、
  原本（`upstream-original` = ca25edf）のバイナリと、5通りのオプション × 30局面で候補手の出力が完全一致することを確認。
