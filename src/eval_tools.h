/*
 * 技巧 (Gikou), a USI shogi (Japanese chess) playing engine.
 * Copyright (C) 2016-2017 Yosuke Demura
 * except where otherwise indicated.
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

#ifndef EVAL_TOOLS_H_
#define EVAL_TOOLS_H_

#if !defined(MINIMUM)

#include <cstdint>
#include <cstdio>
#include <istream>
#include <string>

/**
 * 評価関数の検証用ツール群です（docs/gikou-ideas.md の G3）.
 *
 * 他エンジン上の再実装（R1）や外部トレーナー（T1）と、評価値を整数レベルで突き合わせるための
 * 正解値ジェネレータとして使います。詳細は docs/EVAL_TOOLS.md を参照してください。
 */
namespace EvalTools {

/**
 * 決定的な（環境に依存しない）擬似乱数生成器（SplitMix64）.
 * std::uniform_int_distribution は実装依存なので、出力を再現させたい用途では使いません。
 */
class Rng {
 public:
  explicit Rng(uint64_t seed) : state_(seed) {}
  uint64_t Next() {
    uint64_t z = (state_ += UINT64_C(0x9E3779B97F4A7C15));
    z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31);
  }
  /** [0, n) の一様乱数（n > 0） */
  uint64_t Below(uint64_t n) { return Next() % n; }
 private:
  uint64_t state_;
};

/**
 * テスト用のランダムな評価パラメータを生成し、params.bin 形式でファイルに書き出します.
 * PP 対称性（two_pieces[a][b] == two_pieces[b][a]）を満たし、駒の価値は標準的な値に固定します.
 * @param amplitude 各パラメータの絶対値の上限（単位は 1/kFvScale 点）
 * @return 書き込みに成功すれば true
 */
bool WriteRandomParams(const char* file_name, uint64_t seed, int32_t amplitude);

/**
 * 平手初期局面からランダムな合法手で局面を進め、SFEN を1行ずつ出力します.
 * 出力は "sfen <盤面> <手番> <持ち駒> <手数>" の形式で、seed が同じなら常に同じ結果になります.
 */
void GenerateRandomSfens(int num_positions, uint64_t seed, int max_ply, std::FILE* out);

/**
 * 入力の各行（"startpos [moves ...]" / "sfen ... [moves ...]" / SFEN）について、評価値・進行度・
 * 評価項目別の内訳を JSON Lines 形式で出力します.
 * @return 不正な行があれば、その行数（すべて正常なら 0）
 */
int DumpEvaluation(std::istream& in, std::FILE* out);

/**
 * 評価関数の整合性を検査します.
 *   1. PP 対称性（two_pieces[a][b] == two_pieces[b][a]）
 *   2. ランダム対局の各局面で、差分計算（Node::Evaluate）と全計算（Evaluation::Evaluate）が一致すること
 * @return 不一致の件数（0 なら合格）
 */
int RunSelfTest(int num_games, uint64_t seed);

} // namespace EvalTools

#endif // !defined(MINIMUM)

#endif /* EVAL_TOOLS_H_ */
