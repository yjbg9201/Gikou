/*
 * 技巧 (Gikou), a USI shogi (Japanese chess) playing engine.
 * Copyright (C) 2016-2017 Yosuke Demura
 * except where otherwise indicated.
 *
 * The PackedSfen format and the Move16 format implemented in this file are
 * those of YaneuraOu (https://github.com/yaneurao/YaneuraOu,
 * source/extra/sfen_packer.cpp and source/types.h),
 * Copyright (C) 2015-2025 Motohiro Isozaki (yaneurao), GPL-3.0.
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

#ifndef SFEN_PACKER_H_
#define SFEN_PACKER_H_

#include <cstdint>
#include <cstring>
#include <string>
#include "raw_position.h"

/**
 * やねうら王互換の PackedSfen（256ビット = 32バイトに圧縮した局面）です.
 *
 * 注意: 技巧の HuffmanCode（huffman_code.h）とはビット列が一致しません。
 * 盤上の駒の「成りフラグ」と「先後フラグ」の順序が逆で（技巧は先後→成り、やねうら王は成り→先後）、
 * 駒箱（駒落ち）の表現も技巧にはありません。やねうら王の PSV 教師や .ybb 定跡を扱うときは
 * こちらを使ってください。
 *
 * 形式（やねうら王 source/extra/sfen_packer.cpp より）:
 *   手番(1bit) + 先手玉の位置(7bit) + 後手玉の位置(7bit) + 盤上の駒(1一,1二,…,9九の順) + 手駒 + 駒箱
 *   ビットはバイトの下位ビットから順に詰める。玉がいない場合の位置は 81。
 */
struct PackedSfen {
  uint8_t data[32];

  bool operator==(const PackedSfen& rhs) const {
    return std::memcmp(data, rhs.data, sizeof(data)) == 0;
  }
  bool operator!=(const PackedSfen& rhs) const { return !(*this == rhs); }
  bool operator<(const PackedSfen& rhs) const {
    return std::memcmp(data, rhs.data, sizeof(data)) < 0;
  }
};
static_assert(sizeof(PackedSfen) == 32, "PackedSfen must be 32 bytes");

namespace SfenPacker {

/**
 * 局面を PackedSfen に符号化します.
 * 盤上・持ち駒にない駒は駒箱の駒として出力します（cshogi / やねうら王と同じ）.
 * @return 符号化できない局面（駒数超過など）の場合は false
 */
bool Pack(const RawPosition& raw, PackedSfen* out);

/**
 * PackedSfen を復号します.
 * @param has_piece_box 駒箱に駒があった（駒落ちなど、40枚が揃っていない）場合に true を受け取る（nullptr 可）
 * @return 不正なビット列（ビットの過不足、駒数超過など）の場合は false
 */
bool Unpack(const PackedSfen& packed, RawPosition* out, bool* has_piece_box = nullptr);

/** 16進文字列（64文字）に変換します（デバッグ・テスト用）. */
std::string ToHex(const PackedSfen& packed);

/** 16進文字列（64文字）から読み込みます. */
bool FromHex(const std::string& hex, PackedSfen* out);

} // namespace SfenPacker

/**
 * やねうら王の16ビット指し手（Move16）を扱う関数群です.
 *
 *   bit 0- 6: 移動先のマス
 *   bit 7-13: 移動元のマス（打つ手の場合は、やねうら王の駒種: 歩1 香2 桂3 銀4 角5 飛6 金7）
 *   bit 14  : 打つ手のフラグ
 *   bit 15  : 成る手のフラグ
 *
 * マス番号は技巧と同じですが、打つ手の駒種の番号が技巧（金5 角6 飛7）と異なるので、
 * 技巧の Move との変換は USI 文字列を経由して行います（FindLegalMoveFromUsi()）.
 */
namespace YaneMove16 {

constexpr uint16_t kNone   = 0;
constexpr uint16_t kNull   = (1 << 7) + 1;
constexpr uint16_t kResign = (2 << 7) + 2;
constexpr uint16_t kWin    = (3 << 7) + 3;

/**
 * USI 文字列に変換します. 特殊な指し手（none/null/resign/win）や、不正な値の場合は空文字列を返します.
 */
std::string ToUsi(uint16_t move16);

/**
 * USI 文字列から変換します. 不正な文字列の場合は kNone を返します.
 */
uint16_t FromUsi(const std::string& usi);

/**
 * 盤面を180度回転させた指し手を返します（やねうら王 flip_move() と同じ）. 特殊な指し手はそのまま返します.
 */
uint16_t Flip(uint16_t move16);

} // namespace YaneMove16

#endif /* SFEN_PACKER_H_ */
