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

#ifndef RAW_POSITION_H_
#define RAW_POSITION_H_

#include <cstdint>
#include <string>
#include "move.h"
#include "piece.h"
#include "types.h"
class Position;

/**
 * 盤面・持ち駒・手番だけを持つ、軽量な局面表現です.
 *
 * Position::FromSfen() は入力を検証しない（駒数超過で固定長リストが溢れる等）ため、
 * 外部から来たSFEN（定跡ファイル、教師データ、CLIの入力）は、まずこのクラスで検証してから
 * Position に変換します。また、やねうら王形式の PackedSfen との相互変換（sfen_packer.h）の
 * 中間表現としても使います。
 *
 * マス番号は技巧の Square と同じ（0=1一、1=1二、…、80=9九）で、やねうら王の Square とも一致します。
 * 駒の値は技巧の Piece（16*手番 + PieceType）です。
 */
struct RawPosition {
  /** 盤上の駒（技巧の Piece の整数値。0 は空きマス） */
  uint8_t board[81];

  /** 持ち駒の枚数 [手番][技巧の PieceType（kPawn..kRook）] */
  uint8_t hand[2][8];

  /** 手番 */
  Color side_to_move;

  /** SFEN に書かれていた手数（書かれていなければ 0） */
  int game_ply;

  RawPosition() { Clear(); }

  void Clear();

  /**
   * SFEN 文字列（"sfen " の接頭辞は付けない。手数は省略可）を読み込みます.
   * 盤面の形式、駒の文字、駒種ごとの最大枚数（玉は各1枚まで）を検査し、
   * 不正な場合は false を返します（このとき *this の内容は不定）.
   */
  bool ParseSfen(const std::string& sfen, std::string* error = nullptr);

  /**
   * 駒種ごとの最大枚数（玉は各手番1枚まで）と、駒の値の範囲を検査します.
   */
  bool Validate(std::string* error = nullptr) const;

  /**
   * SFEN 文字列を返します（持ち駒は飛角金銀桂香歩の順、先手→後手）.
   * @param ply 末尾に付ける手数（0 以下なら手数を付けない）
   */
  std::string ToSfen(int ply = 0) const;

  /** 技巧の Position から作ります. */
  static RawPosition FromPosition(const Position& pos);

  /** 技巧の Position に変換します（ParseSfen() に成功した局面であること）. */
  Position ToPosition() const;

  /** 盤面を180度回転し、先後と手番を入れ替えた局面を返します. */
  RawPosition Flipped() const;

  /** 指定した手番の玉のマス（いなければ -1）. */
  int king_square(Color c) const;

  /** 駒箱に駒が残っている（全40枚が盤上・持ち駒にない）場合は true. */
  bool HasPiecesInBox() const;

  bool operator==(const RawPosition& rhs) const;
  bool operator!=(const RawPosition& rhs) const { return !(*this == rhs); }
};

/**
 * 局面として成立しているか（双方の玉が1枚ずつあり、手番でない側の玉に王手がかかっていない）を返します.
 */
bool IsValidPositionForSearch(const Position& pos);

/**
 * USI 形式の指し手文字列（例: "7g7f", "8h2b+", "P*5e"）を、局面の合法手の中から探します.
 * 合法手でなければ kMoveNone を返します（Move::FromSfen と異なり、不正入力でも安全）.
 */
Move FindLegalMoveFromUsi(const Position& pos, const std::string& usi_move);

/**
 * "startpos [moves ...]" / "sfen <SFEN> [moves ...]" / "position ..." / "<SFEN>" のいずれかの行から
 * 局面を作ります. 不正な入力なら false を返し、error に理由を入れます.
 */
bool ParsePositionCommand(const std::string& line, Position* pos, std::string* error);

#endif /* RAW_POSITION_H_ */
