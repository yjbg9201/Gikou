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

#ifndef CLI_H_
#define CLI_H_

/**
 * コマンド・ライン・インターフェースを実現するためのクラスです.
 */
class Cli {
 public:
  /**
   * シェルから渡されるオプションを解析したうえで、コマンドを実行します.
   *
   * @param argc main()関数に渡された引数の数
   * @param argv main()関数に渡された引数
   *
   * コマンドの一覧：
   *   - --bench              探索のベンチマークを行う
   *   - --bench-movegen      指し手生成のベンチマークテストを行う
   *   - --bench-mate1        １手詰関数のベンチマークテストを行う
   *   - --bench-mate3        ３手詰関数のベンチマークテストを行う
   *   - --cluster            疎結合並列探索（GPS将棋風クラスタ）のマスターを起動する
   *   - --compute-all-quiets すべてのquiet movesを列挙する
   *   - --consultation       合議アルゴリズムを用いたクラスタのマスターを起動する
   *   - --create-book        棋譜DBファイルから定跡DBファイルを作成する
   *   - --db-stats           棋譜DBファイルの統計データを計算して表示する
   *   - --learn              評価関数の学習を行う
   *   - --learn-progress     進行度推定関数の学習を行う
   *   - --learn-probability  指し手の実現確率の学習を行う
   *   - --compute-ratings    棋譜DBファイルに登場するプレイヤーのレーティングを計算する
   *   - --learn-psv          やねうら王のPSV教師で RootStrap + 勝敗回帰 を併用して学習する（G1）
   *   - --learn-psv-rootstrap やねうら王のPSV教師で RootStrap を併用して学習する（G1）
   *   - --convert-psv        やねうら王のPSV教師を teacher_positions.bin / teacher_games.bin に変換する（G1）
   *   - --psv-selftest       PackedSfen / Move16 / PSV 変換のセルフテストを行う（G1）
   *   - --sfen-pack / --sfen-unpack  SFEN と やねうら王互換PackedSfen を相互変換する（テスト用）
   *   - --bench-yanebook     やねうら王定跡の読み込み時間・照合時間を計測する（G2）
   *   - --randparams         テスト用のランダムな params.bin を生成する（G3）
   *   - --randsfen           ランダム自己対局で局面を生成し、SFENを出力する（G3）
   *   - --evaldump           局面ごとの評価値・進行度・項目別内訳を出力する（G3）
   *   - --eval-selftest      PP対称性と、差分評価・全計算の一致を検査する（G3）
   */
  static void ExecuteCommand(int argc, char* argv[]);
};

#endif /* CLI_H_ */
