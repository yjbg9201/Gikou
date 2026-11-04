/*
 * 技巧 (Gikou), a USI shogi (Japanese chess) playing engine.
 * Copyright (C) 2016-2017 Yosuke Demura
 * except where otherwise indicated.
 *
 * The PackedSfenValue (PSV) record format is that of YaneuraOu
 * (https://github.com/yaneurao/YaneuraOu), Copyright (C) Motohiro Isozaki, GPL-3.0.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef YANEURAOU_PSV_H_
#define YANEURAOU_PSV_H_

#if !defined(MINIMUM)

#include <cstdint>
#include <string>
#include <vector>
#include "sfen_packer.h"
#include "teacher_data.h"

/**
 * やねうら王の教師データ形式 PackedSfenValue（PSV、40バイト/レコード）です.
 * score と game_result は、いずれも sfen の局面の手番側から見た値です.
 */
struct PackedSfenValue {
  PackedSfen sfen;     // 局面（やねうら王互換 PackedSfen）
  int16_t score;       // 手番側から見た評価値
  uint16_t move;       // 最善手（Move16）
  uint16_t game_ply;   // 初期局面からの手数
  int8_t game_result;  // 手番側から見た勝敗（1: 勝ち, 0: 引き分け, -1: 負け）
  uint8_t padding;
};
static_assert(sizeof(PackedSfenValue) == 40, "PackedSfenValue must be 40 bytes");

/**
 * PSV → 技巧の教師データ（TeacherPosition）変換の設定です.
 * docs/PSV_LEARNING.md を参照してください.
 */
struct PsvOptions {
  /** 評価値に掛ける倍率（技巧の score = round(PSV の score * scale)） */
  double scale = 1.0;

  /** RootStrap 用の教師から除外する評価値の絶対値（scale 適用後）。0 以下なら制限なし */
  int eval_limit = 3000;

  /** 読み込む最大レコード数（0 なら全部） */
  uint64_t max_records = 0;

  /** "key=value" 形式のオプションを解釈します. 不明なキーや不正な値の場合は false */
  bool Parse(const std::string& option, std::string* error);
};

/**
 * 変換の統計です.
 */
struct PsvStats {
  uint64_t records = 0;           // 読み込んだレコード数
  uint64_t invalid = 0;           // 復号できない・局面として不正なレコード
  uint64_t piece_box = 0;         // 駒箱に駒がある（駒落ちなど）ためスキップしたレコード
  uint64_t illegal_move = 0;      // 指し手が合法手でなかったレコード（局面は教師として使う）
  uint64_t rootstrap = 0;         // RootStrap 用の教師にしたレコード
  uint64_t rootstrap_eval_limit = 0; // eval_limit で RootStrap から除外したレコード
  uint64_t regression = 0;        // 勝敗回帰用の教師にしたレコード
  uint64_t regression_draw = 0;   // 引き分けのため勝敗回帰から除外したレコード

  void Print() const;
};

namespace YaneuraOuPsv {

/**
 * PSV の1レコードを、技巧の教師局面に変換します.
 *
 * @param rootstrap  RootStrap 用の教師（score = round(score * scale)。eval_limit を超えたら作らない）
 * @param regression 勝敗回帰用の教師（score = ±kScoreKnownWin。引き分けなら作らない。eval_limit は適用しない）
 * @return 局面として使えるレコードなら true（rootstrap / regression のどちらも作られない場合もある）
 */
bool ConvertRecord(const PackedSfenValue& psv, const PsvOptions& options,
                   TeacherPosition* rootstrap, bool* has_rootstrap,
                   TeacherPosition* regression, bool* has_regression,
                   PsvStats* stats);

/**
 * PSV ファイルを読み込み、技巧の教師局面に変換します（OpenMP で並列に変換し、順序は保ちます）.
 * @param rootstrap  nullptr なら RootStrap 用の教師は作らない
 * @param regression nullptr なら勝敗回帰用の教師は作らない
 * @return ファイルを開けない・サイズが40の倍数でない場合は false
 */
bool ReadFile(const std::string& file_name, const PsvOptions& options,
              std::vector<TeacherPosition>* rootstrap,
              std::vector<TeacherPosition>* regression,
              PsvStats* stats);

/**
 * PackedSfen / Move16 / PSV 変換のセルフテストを行います（独立実装 cshogi で作った期待値と照合）.
 * @return 失敗した項目の数
 */
int RunSelfTest();

} // namespace YaneuraOuPsv

#endif // !defined(MINIMUM)

#endif /* YANEURAOU_PSV_H_ */
