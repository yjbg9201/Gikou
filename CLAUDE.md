# CLAUDE.md — 技巧（Gikou 2）改造リポジトリ

技巧2（GPL-3.0）を土台にした改造リポジトリです。改造アイデアの親仕様書は
[`docs/gikou-ideas.md`](docs/gikou-ideas.md)、既存仕様書の原本置き場は [`docs/specs/`](docs/specs/) です。
タスクは「`docs/gikou-ideas.md` の G2 フェーズ1」のように ID で発注されます。

## ビルド

```
make release        # Mac / Linux 用（bin/release）
make gikou          # MINIMUM ビルド（Windows 配布用設定、-static）。学習・CLI 系コードは入らない
make development    # -O2 -g3、assert 有効
```

- `-std=c++11 -fno-exceptions -fno-rtti`。例外は使えないので、外部入力のパースに `std::stoi` 等の
  例外を投げる関数を使わない（`strtol` 等で自前に検証する）。
- 原本の時点で g++ 13 では `-Wclass-memaccess` などの警告が出る。**警告を増やさない**ことが受け入れ基準。
  `tools/ci/check_warnings.sh release` / `tools/ci/check_warnings.sh gikou` で基準（`tools/ci/warnings_baseline_*.txt`）
  と比較できる。
- 原本は `upstream-original` タグ。パッチ再生成・比較の基準にする。

## テスト

評価ファイル（params.bin 約248MiB）や定跡はリポジトリに入れない。テストはランダム生成した params.bin と
Python（cshogi）で作るデータで行う。CI（`.github/workflows/ci.yml`）と同じものをローカルで回すには:

```
pip install cshogi numpy
make -j release
tests/run_all.sh            # 各タスクのセルフテスト一式
```

## 追加したコマンド（CLI。MINIMUM ビルドには入らない）

| コマンド | ID | 説明 |
| --- | --- | --- |
| `--randparams <out> [seed]` | G3 | テスト用のランダム params.bin（PP 対称性を満たす）を生成 |
| `--randsfen <n> [seed] [maxply]` | G3 | ランダム自己対局で局面を生成し SFEN を出力 |
| `--evaldump [file]` | G3 | 局面ごとの評価値・進行度・項目別内訳を出力 |
| `--eval-selftest [n] [seed]` | G3 | 差分評価と全計算の一致、PP 対称性を検査 |
| `--learn-psv <psv> [opts]` | G1 | PSV 教師で RootStrap＋勝敗回帰の補助損失を使って学習 |
| `--learn-psv-rootstrap <psv> [opts]` | G1 | PSV 教師で RootStrap のみ使って学習 |
| `--convert-psv <psv> [opts]` | G1 | PSV を teacher_positions.bin / teacher_games.bin に変換 |
| `--psv-selftest` | G1 | PackedSfen / Move16 変換のセルフテスト |
| `--sfen-pack` / `--sfen-unpack` | G1/G2 | SFEN ⇔ やねうら王互換 PackedSfen（16進）の変換（テスト用） |

詳細: [`docs/EVAL_TOOLS.md`](docs/EVAL_TOOLS.md)（G3）、[`docs/PSV_LEARNING.md`](docs/PSV_LEARNING.md)（G1）、
[`docs/YANEURAOU_BOOK.md`](docs/YANEURAOU_BOOK.md)（G2）。

## やねうら王定跡（G2）

`BookFile` に `.db` / `.ybb` を指定すると、ヘッダ（または拡張子）で判別して `src/yaneura_book.cc` で読み込む。
それ以外は従来どおり技巧形式（`src/book.cc`）。新規 USI オプション: `BookIgnoreRate`, `BookEvalDiff`,
`BookEvalBlackLimit`, `BookEvalWhiteLimit`, `BookDepthLimit`, `ConsiderBookMoveCount`, `FlippedBook`, `BookOnTheFly`。
`BookMaxPly` の上限は 10000（既定値 20 は不変）。

## 原則

- **後方互換**: 既存の `book.bin`、`teacher_positions.bin` / `teacher_games.bin` 経路、USI オプションの既定値の挙動を変えない。
  新機能は新しいオプション・新しいファイル形式・新しいコマンドで追加する。
- **検証は独立実装と照合**: 実装同士の突き合わせではなく、やねうら王原典の転写や cshogi など別実装との一致で確認する。
- **移植コードには出典と原著作権表示**を残す（やねうら王も GPL-3.0）。
- 1 タスク 1 PR（または 1 コミット群）。`docs/gikou-ideas.md` の該当 ID をコミットメッセージに書く。

## 落とし穴チェックリスト（docs/gikou-ideas.md §4 より）

- **金・角・飛の PieceType 番号差**: 技巧 5/6/7 = 金角飛、やねうら王 5/6/7 = 角飛金。打ち駒の Move16、手駒、HandSet に効く。
- **手駒の列挙順**: 技巧は PieceType 昇順（歩香桂銀金角飛）。PP が非対称なので順序違いで評価値が ±1 ずれる。
- **評価値の視点**: 技巧定跡（book.bin）の内部保存は先手視点、やねうら王定跡と PSV は手番視点。PSV→技巧学習では反転不要。
  やねうら王定跡を技巧の反転コード（`Book::Probe`）に通さない。
- **SFEN の手数**: 技巧 `Position::ToSfen()` は常に ` 1` を出力する。手数を含むキーで照合しない。
- **マス番号**: 技巧とやねうら王は同じ（0=1一, 1=1二, …, 80=9九、`file*9+rank`）。
- **PackedSfen のビット一致**: 技巧の `HuffmanCode` は**やねうら王 PackedSfen と一致しない**（盤上駒で先後フラグと成りフラグの
  順序が逆）。やねうら王互換が必要なら `src/sfen_packer.h` を使う。cshogi `to_psfen()` との一致テストあり。
- **ハフマン形式の3系統**: 技巧・Apery・やねうら王で成り/先後フラグの順序が異なる。平手や先手のみ手駒の局面では
  誤ってテストが通ることがあるので、後手の成駒・後手の手駒を含む局面でテストする。
- **不正 SFEN**: 駒数超過の局面を受理すると固定長リストが溢れる。駒種ごとの最大枚数で検査する（`Position::FromSfen` は検査しない）。
- **PackedScore 符号規約**: 3x1系（KP・tempo: 序盤/中盤/終盤＋進行度重み）と 2x2系（その他: 序盤/手番/終盤/手番）を混同しない。
- **PP 対称性** `two_pieces[a][b] == two_pieces[b][a]`: 違反しても即クラッシュせず差分評価が静かに壊れる。
- **80,000 局面ガード**: 学習の RootStrap / 勝敗回帰は 80,000 局面未満だと復元抽出が未定義動作。明示エラーにしてある。
