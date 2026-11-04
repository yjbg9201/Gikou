# 既存仕様書の原本置き場

`docs/gikou-ideas.md` §1 で棚卸しした、過去の仕様書・成果物の原本を置く場所です。
原本は claude.ai の過去チャットで作成されたもので、このリポジトリの初回取り込み時点では
手元から渡されていません（`docs/gikou-ideas.md` §6 の未決事項）。

| ファイル（予定） | 内容 | 本リポジトリでの扱い |
| --- | --- | --- |
| `gikou_yaneuraou_book_spec.md` | 技巧でやねうら王定跡を読む改造（G2） | 原本なし。やねうら王 `source/book/book.cpp` を直接参照して実装した（`docs/YANEURAOU_BOOK.md`） |
| `gikou_eval_on_yaneuraou_spec.md` | やねうら王上での技巧評価関数（R1） | 対象外（参考） |
| `gikou_params_trainer_spec.md` | params.bin GPU トレーナー（T1） | 未着手 |
| `shogi_teacher_converter_spec.md` | 教師データ相互変換 | §4 の落とし穴として CLAUDE.md に転記済み |
| `gikou-psv/PSV_LEARNING.md` | PSV 学習パッチ（G1） | パッチ原本なし。仕様書の記述から再実装した（`docs/PSV_LEARNING.md`） |
| `gikou_main.cc_testdump.patch` | `--evaldump` / `--randsfen`（G3） | パッチ原本なし。再実装した（`docs/EVAL_TOOLS.md`） |

原本が見つかった場合はこのディレクトリに置き、再実装との差分（オプション名・出力形式など）を
確認してください。
