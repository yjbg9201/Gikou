# 評価関数の検証用 CLI（G3）

`docs/gikou-ideas.md` の **G3**。技巧の評価値を、他エンジン上の再実装（R1）や外部トレーナー（T1）と
整数レベルで突き合わせるための正解値ジェネレータです。`make release` のバイナリに入ります
（`MINIMUM` ビルドには入りません）。

> 過去チャットで作った `gikou_main.cc_testdump.patch` の原本は手元になかったため、仕様書の記述をもとに
> 再実装しました。出力形式は原本と異なる可能性があります（原本が見つかったら差分を確認してください）。

すべてのコマンドはカレントディレクトリの `params.bin` / `progress.bin` / `probability.bin` を読みます
（起動時に `info string Failed to open ...` と表示されるのは、ファイルがないときの技巧本体の既存の挙動です）。

## `--randparams <出力ファイル> [seed=1] [amplitude=262144]`

テスト用のランダムな params.bin（259,336,912 bytes）を生成します。

- 全パラメータを `[-amplitude, +amplitude]`（単位は 1/65536 点）の一様乱数で埋めます。
- KP の第4スロット（進行度の重み）は amplitude/64 に縮めます（進行度が 0/1 に張り付かないように）。
- **PP 対称性** `two_pieces[a][b] == two_pieces[b][a]` を満たすように対称化します。
- 駒の価値（`material`、探索の駒得計算に使われる）は標準値（歩=100、香=300、…）に固定します。
- 乱数は環境に依存しない SplitMix64 なので、同じ seed からは常に同じバイト列になります（CI でチェックサムを確認）。

## `--randsfen <局面数> [seed=1] [最大手数=128]`

平手初期局面から一様ランダムな合法手（不成を含む）で 1〜最大手数 手進めた局面を、
`sfen <盤面> <手番> <持ち駒> <手数>` の形式で標準出力に書きます。詰んだらそこで止めます。
同じ seed からは常に同じ局面列になります。

## `--evaldump [入力ファイル|-]`

入力（省略時は標準入力）の各行について、評価値と内訳を JSON Lines で出力します。

入力行の形式（空行・`#` で始まる行は無視）:

```
startpos
startpos moves 7g7f 3c3d
sfen lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1
position sfen <SFEN> moves ...
<SFEN>
```

SFEN は駒数超過などを検査してから読み込み、指し手は合法手と照合します。不正な行は
`{"input": ..., "error": ...}` を出力して処理を続け、最後に終了コード 1 で終わります。

出力例（1行1局面）:

```json
{"sfen":"lnsgkgsnl/1r5b1/pppppp1pp/6p2/9/2P6/PP1PPPPPP/1B5R1/LNSGKGSNL b - 1","side_to_move":"b",
 "score":-3,"progress":0.940667233,"score_black":-3,"progress_weight":181104,
 "kp_black":[...4],"kp_white":[...4],"two_pieces":[...4],"controls":[...4],
 "king_safety":[...4],"sliders":[...4],"tempo":[...4],"num_psq":38}
```

| キー | 意味 |
| --- | --- |
| `score` | `Evaluation::Evaluate()` の値（手番視点、歩=100） |
| `score_black` | 先手視点に直した値 |
| `progress` | 進行度 σ(progress_weight / 2^16) |
| `progress_weight` | KP 第4スロットの総和（`kp_black[3] + kp_white[3]`） |
| `kp_black` / `kp_white` | `EvalDetail::kp`（3x1系: 序盤/中盤/終盤/進行度重み。`kp_white` は先後反転済み） |
| `two_pieces`, `controls`, `king_safety`, `sliders` | `EvalDetail` の各項目（2x2系: 序盤/手番/終盤/手番） |
| `tempo` | `EvalParameters::tempo`（3x1系。局面によらず一定） |
| `num_psq` | 駒の位置インデックスの数 |

最終評価値は `EvalDetail::ComputeFinalScore()` の式（`src/evaluation.cc`）で上記の値から再現できます。

## `--eval-selftest [対局数=20] [seed=1]`

1. **PP 対称性**: `two_pieces` の全組について `T[a][b] == T[b][a]` を検査します。
2. **差分評価と全計算の一致**: ランダム対局（最大256手）の各局面で、`Node::Evaluate()`（差分計算）と
   `Evaluation::EvaluateAll()`（全計算）の評価値・進行度が完全一致することを検査します。

不一致があれば終了コード 1。params.bin がない（全ゼロ）と検査の意味がないので警告を出します。

## CI での使い方

`tests/test_10_eval_tools.sh`:

1. `--randparams params.bin 1` のチェックサムが固定値であること
2. `--randsfen` の出力を cshogi（独立実装）で読み直し、SFEN 表記の完全一致と合法性を確認
3. `--evaldump` が全局面を出力し、形式が正しいこと
4. 不正入力（駒数超過、10筋、非合法手）がエラー行になること
5. `--eval-selftest` が合格すること
