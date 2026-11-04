/*
 * 技巧 (Gikou), a USI shogi (Japanese chess) playing engine.
 * Copyright (C) 2016-2017 Yosuke Demura
 * except where otherwise indicated.
 *
 * The book file formats (.db "YANEURAOU-DB2016" and .ybb "YANE-BINBOOK-V1") and the
 * book move selection logic (BookMoveSelector::probe_impl) are ported from YaneuraOu
 * (https://github.com/yaneurao/YaneuraOu, source/book/book.cpp),
 * Copyright (C) Motohiro Isozaki (yaneurao), GPL-3.0.
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

#ifndef YANEURA_BOOK_H_
#define YANEURA_BOOK_H_

#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>
#include "move.h"
#include "sfen_packer.h"
class Position;
class UsiOptions;

/**
 * やねうら王定跡の、ある局面での指し手1つです（やねうら王 Book::BookMove に相当）.
 */
struct YaneBookMove {
  uint16_t move;       // 指し手（やねうら王の Move16）
  uint16_t ponder;     // 予想される相手の指し手（なければ 0）
  int32_t value;       // 指し手を指したあとの局面の評価値（手番側から見た値）
  int32_t depth;       // 探索深さ（なければ 0）
  uint64_t move_count; // 採択回数（.ybb にはないので 0）
};

/**
 * 技巧で、やねうら王形式の定跡ファイルを読み込み、定跡手を選択するクラスです（docs/gikou-ideas.md の G2）.
 *
 * 対応形式:
 *   - .db  : やねうら王の標準定跡フォーマット（"#YANEURAOU-DB2016"）。メモリに全部読み込む。
 *   - .ybb : やねうら王バイナリ定跡（"YANE-BINBOOK-V1"）。メモリに全部読み込むか、
 *            BookOnTheFly=true ならディスク上で二分探索する。
 *
 * 局面のキーは、やねうら王互換 PackedSfen（32バイト）で、手数は常に無視します
 * （やねうら王の IgnoreBookPly=true 相当。技巧の Position は実際の手数を持たないため）。
 * 評価値は、やねうら王と同じく「手番側から見た値」のまま扱います（技巧の book.bin のような先手視点への変換はしません）。
 */
class YaneuraOuBook {
 public:
  enum Format {
    kGikouFormat, // 技巧形式（book.bin）
    kDbFormat,    // やねうら王 .db
    kYbbFormat,   // やねうら王 .ybb
  };

  YaneuraOuBook();
  ~YaneuraOuBook();
  YaneuraOuBook(const YaneuraOuBook&) = delete;
  YaneuraOuBook& operator=(const YaneuraOuBook&) = delete;

  /**
   * 定跡ファイルの形式を判別します.
   *   1. 先頭16バイトが "YANE-BINBOOK-V1\0" → .ybb
   *   2. 先頭行が "#YANEURAOU-DB2016" で始まる → .db
   *   3. 拡張子が .db → .db、拡張子が .ybb → .ybb（ヘッダが不正なら読み込み時にエラー）
   *   4. それ以外 → 技巧形式
   * ファイルが存在せず、拡張子が .db で同名の .ybb が存在する場合は、.ybb を読みます（resolved_file_name に返す）.
   */
  static Format DetectFormat(const std::string& file_name, std::string* resolved_file_name);

  /**
   * やねうら王形式の定跡ファイルを読み込みます.
   * 同じファイル（名前・サイズ・更新時刻）・同じモードで読み込み済みなら、何もしません。
   * 読み込みに失敗した場合（ファイルがない・壊れている）は、info string でエラーを表示し、定跡なしの状態になります.
   * @param on_the_fly .ybb をメモリに読み込まず、ディスク上で二分探索する（.db では無視して全読込）
   * @return 定跡が使える状態なら true
   */
  bool Load(const std::string& file_name, Format format, bool on_the_fly);

  /** 定跡を破棄します. */
  void Clear();

  /** 定跡が読み込まれていれば true. */
  bool loaded() const { return loaded_; }

  /** 局面数（.ybb の on the fly モードではヘッダに書かれた数）. */
  uint64_t num_positions() const;

  /**
   * 局面の定跡手を探します（反転なし）. 手数は無視します.
   * @return 見つかれば true（moves はファイル上の順序）
   */
  bool Find(const PackedSfen& key, std::vector<YaneBookMove>* moves);

  /**
   * 現局面の定跡手を探します. 見つからず flipped_book が true なら、先後反転した局面でも探し、
   * 見つかった指し手を現局面の指し手に反転して返します（評価値は手番視点なので反転不要）.
   * 返す指し手は、やねうら王と同じく「採択回数の多い順 → 評価値の高い順」に安定ソートされています.
   */
  bool Probe(const Position& pos, bool flipped_book, std::vector<YaneBookMove>* moves,
             bool* hit_flipped = nullptr);

  /**
   * 定跡手を１手選びます（やねうら王 BookMoveSelector::probe_impl の移植）.
   * 候補手を "info ... multipv ... score cp ... pv ..." で出力します.
   * 使う USI オプション: BookIgnoreRate, NarrowBook, BookDepthLimit, BookEvalDiff,
   *                      BookEvalBlackLimit, BookEvalWhiteLimit, ConsiderBookMoveCount, FlippedBook
   * @param ponder_move 予想手（定跡に登録された ponder、なければ次の局面の定跡の最善手。合法手のみ）
   * @return 定跡手（定跡にない・条件を満たす手がない場合は kMoveNone）
   */
  Move GetOneBookMove(const Position& pos, const UsiOptions& usi_options, Move* ponder_move);

  /** 乱数のシードを設定します（テスト用）. */
  void SetRandomSeed(uint64_t seed) { random_.seed(static_cast<std::mt19937::result_type>(seed)); }

  /**
   * 定跡にdepthの情報があるか（.db で全ての指し手の depth が 0、または depth を持たない .ybb なら false）.
   * false の場合、BookDepthLimit は無視します（すべての定跡手が除外されてしまうのを防ぐ救済措置）.
   */
  bool has_depth() const { return has_depth_; }

  /** 指し手を並べ替えます（やねうら王 BookMove::operator< と同じ: 採択回数の降順 → 評価値の降順。安定ソート）. */
  static void SortMoves(std::vector<YaneBookMove>* moves);

 private:
  struct IndexEntry {
    PackedSfen key;
    uint32_t first; // moves_ の先頭位置
    uint32_t count; // 指し手の数
  };

  bool LoadDb(const std::string& file_name);
  bool LoadYbb(const std::string& file_name, bool on_the_fly);
  bool FindInYbb(const PackedSfen& key, std::vector<YaneBookMove>* moves);

  bool loaded_ = false;
  bool has_depth_ = false;
  std::string file_name_;
  uint64_t file_size_ = 0;
  int64_t file_mtime_ = 0;
  Format format_ = kGikouFormat;
  bool on_the_fly_ = false;

  // .db（と、これに変換した .ybb は使わない）: 局面のインデックス（key でソート済み）と指し手
  std::vector<IndexEntry> index_;
  std::vector<YaneBookMove> moves_;

  // .ybb
  std::vector<uint8_t> ybb_data_;   // メモリに全部読み込んだ場合のファイル内容
  std::FILE* ybb_file_ = nullptr;   // on the fly の場合のファイル
  uint64_t ybb_record_count_ = 0;
  uint64_t ybb_flags_ = 0;
  uint64_t ybb_moves_base_ = 0;

  std::mt19937 random_;
};

#endif /* YANEURA_BOOK_H_ */
