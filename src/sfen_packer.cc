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

#include "sfen_packer.h"

namespace {

// やねうら王の駒種番号（歩1 香2 桂3 銀4 角5 飛6 金7）
enum YanePieceType { kYNone = 0, kYPawn, kYLance, kYKnight, kYSilver, kYBishop, kYRook, kYGold };

// 技巧の PieceType（成っていない駒、kPawn..kRook）→ やねうら王の駒種
const int kGikouToYane[8] = {kYNone, kYPawn, kYLance, kYKnight, kYSilver, kYGold, kYBishop, kYRook};
// やねうら王の駒種 → 技巧の PieceType
const int kYaneToGikou[8] = {0, kPawn, kLance, kKnight, kSilver, kBishop, kRook, kGold};

struct HuffmanedPiece {
  int code;
  int bits;
};

// 盤上の駒のハフマン符号（やねうら王 huffman_table。やねうら王の駒種で引く）
const HuffmanedPiece kHuffmanTable[8] = {
  {0x00, 1}, // NO_PIECE
  {0x01, 2}, // PAWN
  {0x03, 4}, // LANCE
  {0x0b, 4}, // KNIGHT
  {0x07, 4}, // SILVER
  {0x1f, 6}, // BISHOP
  {0x3f, 6}, // ROOK
  {0x0f, 5}, // GOLD
};

// 駒箱の駒のハフマン符号（やねうら王 huffman_table_piecebox。cshogi と同じ）
const HuffmanedPiece kHuffmanTablePieceBox[8] = {
  {0x00, 1}, // not used
  {0x02, 2}, // PAWN
  {0x09, 4}, // LANCE
  {0x0d, 4}, // KNIGHT
  {0x0b, 4}, // SILVER
  {0x2f, 6}, // BISHOP
  {0x3f, 6}, // ROOK
  {0x1b, 5}, // GOLD
};

// 手駒・駒箱の出力順（Apery / cshogi と同じ: 歩香桂銀金角飛）。やねうら王の駒種で表す
const int kHandOrder[7] = {kYPawn, kYLance, kYKnight, kYSilver, kYGold, kYBishop, kYRook};

// 駒種ごとの枚数（やねうら王の駒種で引く）
const int kPieceCounts[8] = {0, 18, 4, 4, 4, 2, 2, 4};

class BitStream {
 public:
  explicit BitStream(uint8_t* data) : data_(data), cursor_(0) {}

  bool WriteOneBit(int b) {
    if (cursor_ >= 256) {
      ++cursor_;
      return false;
    }
    if (b) data_[cursor_ / 8] |= uint8_t(1 << (cursor_ & 7));
    ++cursor_;
    return true;
  }

  bool WriteNBit(int d, int n) {
    bool ok = true;
    for (int i = 0; i < n; ++i) ok &= WriteOneBit(d & (1 << i));
    return ok;
  }

  // 256ビットを超えて読もうとした場合は -1 を返す
  int ReadOneBit() {
    if (cursor_ >= 256) return -1;
    const int b = (data_[cursor_ / 8] >> (cursor_ & 7)) & 1;
    ++cursor_;
    return b;
  }

  int ReadNBit(int n) {
    int result = 0;
    for (int i = 0; i < n; ++i) {
      const int b = ReadOneBit();
      if (b < 0) return -1;
      result |= b << i;
    }
    return result;
  }

  int cursor() const { return cursor_; }

 private:
  uint8_t* data_;
  int cursor_;
};

// 盤上の駒を1枚書き出す（piece は技巧の Piece の整数値）
bool WriteBoardPiece(BitStream& stream, int piece) {
  if (piece == 0) {
    return stream.WriteNBit(kHuffmanTable[kYNone].code, kHuffmanTable[kYNone].bits);
  }
  const int ypt = kGikouToYane[piece & 7];
  bool ok = stream.WriteNBit(kHuffmanTable[ypt].code, kHuffmanTable[ypt].bits);
  if (ypt != kYGold) {
    ok &= stream.WriteOneBit((piece & 8) ? 1 : 0); // 成りフラグ
  }
  ok &= stream.WriteOneBit(piece >> 4);            // 先後フラグ
  return ok;
}

// 手駒を1枚書き出す
bool WriteHandPiece(BitStream& stream, int color, int ypt) {
  bool ok = stream.WriteNBit(kHuffmanTable[ypt].code >> 1, kHuffmanTable[ypt].bits - 1);
  if (ypt != kYGold) {
    ok &= stream.WriteOneBit(0); // 成りフラグ（手駒は常に0）
  }
  ok &= stream.WriteOneBit(color);
  return ok;
}

// 駒箱の駒を1枚書き出す
bool WritePieceBoxPiece(BitStream& stream, int ypt) {
  bool ok = stream.WriteNBit(kHuffmanTablePieceBox[ypt].code, kHuffmanTablePieceBox[ypt].bits);
  if (ypt != kYGold) {
    ok &= stream.WriteOneBit(0);
  }
  return ok;
}

// 盤上の駒を1枚読み込む。戻り値は技巧の Piece の整数値（空きマスは0）。不正なら -1
int ReadBoardPiece(BitStream& stream) {
  int code = 0;
  for (int bits = 1; bits <= 6; ++bits) {
    const int b = stream.ReadOneBit();
    if (b < 0) return -1;
    code |= b << (bits - 1);
    for (int ypt = kYNone; ypt <= kYGold; ++ypt) {
      if (kHuffmanTable[ypt].code == code && kHuffmanTable[ypt].bits == bits) {
        if (ypt == kYNone) return 0;
        int promoted = 0;
        if (ypt != kYGold) {
          promoted = stream.ReadOneBit();
          if (promoted < 0) return -1;
        }
        const int color = stream.ReadOneBit();
        if (color < 0) return -1;
        return 16 * color + kYaneToGikou[ypt] + (promoted ? 8 : 0);
      }
    }
  }
  return -1;
}

// 手駒（または駒箱の駒）を1枚読み込む。戻り値は技巧の Piece の整数値で、
// 駒箱の駒の場合は成り駒（bit 8）が立った値を返す（やねうら王 read_hand_piece_from_stream と同じ）。不正なら -1
int ReadHandPiece(BitStream& stream) {
  int code = 0;
  for (int bits = 1; bits <= 5; ++bits) {
    const int b = stream.ReadOneBit();
    if (b < 0) return -1;
    code |= b << (bits - 1);
    for (int ypt = kYPawn; ypt <= kYGold; ++ypt) {
      if ((kHuffmanTable[ypt].code >> 1) == code && kHuffmanTable[ypt].bits - 1 == bits) {
        int promoted = 0;
        if (ypt != kYGold) {
          promoted = stream.ReadOneBit();
          if (promoted < 0) return -1;
        }
        const int color = stream.ReadOneBit();
        if (color < 0) return -1;
        return 16 * color + kYaneToGikou[ypt] + (promoted ? 8 : 0);
      }
    }
  }
  return -1;
}

int HexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// USI のマス表記（"7g" など）→ マス番号。不正なら -1
int SquareFromUsi(char file, char rank) {
  if (file < '1' || file > '9' || rank < 'a' || rank > 'i') return -1;
  return (file - '1') * 9 + (rank - 'a');
}

std::string SquareToUsi(int sq) {
  return std::string{char('1' + sq / 9), char('a' + sq % 9)};
}

const char kYaneDropChars[8] = {' ', 'P', 'L', 'N', 'S', 'B', 'R', 'G'};

} // namespace

namespace SfenPacker {

bool Pack(const RawPosition& raw, PackedSfen* out) {
  if (!raw.Validate()) {
    return false;
  }
  std::memset(out->data, 0, sizeof(out->data));
  BitStream stream(out->data);

  // 駒箱の枚数（盤上・持ち駒に出てきた分を引いていく）
  int box[8];
  for (int i = 0; i < 8; ++i) box[i] = kPieceCounts[i];

  // 1. 手番
  bool ok = stream.WriteOneBit(raw.side_to_move);

  // 2. 先手玉・後手玉の位置（いなければ81）
  for (Color c : {kBlack, kWhite}) {
    const int ksq = raw.king_square(c);
    ok &= stream.WriteNBit(ksq < 0 ? 81 : ksq, 7);
  }

  // 3. 盤上の駒（玉以外）
  for (int sq = 0; sq < 81; ++sq) {
    const int p = raw.board[sq];
    if (p != 0 && (p & 15) == kKing) continue;
    ok &= WriteBoardPiece(stream, p);
    if (p != 0) box[kGikouToYane[p & 7]]--;
  }

  // 4. 手駒（先手→後手、歩香桂銀金角飛の順）
  for (int c = 0; c < 2; ++c) {
    for (int ypt : kHandOrder) {
      const int n = raw.hand[c][kYaneToGikou[ypt]];
      for (int i = 0; i < n; ++i) ok &= WriteHandPiece(stream, c, ypt);
      box[ypt] -= n;
    }
  }

  // 5. 駒箱
  for (int ypt : kHandOrder) {
    if (box[ypt] < 0) return false;
    for (int i = 0; i < box[ypt]; ++i) ok &= WritePieceBoxPiece(stream, ypt);
  }

  return ok && stream.cursor() == 256;
}

bool Unpack(const PackedSfen& packed, RawPosition* out, bool* has_piece_box) {
  PackedSfen copy = packed;
  BitStream stream(copy.data);
  RawPosition& raw = *out;
  raw.Clear();
  bool piece_box = false;

  // 1. 手番
  const int stm = stream.ReadOneBit();
  raw.side_to_move = stm == 0 ? kBlack : kWhite;

  // 2. 玉の位置
  for (int c = 0; c < 2; ++c) {
    const int ksq = stream.ReadNBit(7);
    if (ksq < 0 || ksq > 81) return false;
    if (ksq == 81) {
      piece_box = true; // 玉がいない
      continue;
    }
    if (raw.board[ksq] != 0) return false; // 両玉が同じマス
    raw.board[ksq] = uint8_t(16 * c + kKing);
  }

  // 3. 盤上の駒
  for (int sq = 0; sq < 81; ++sq) {
    if (raw.board[sq] != 0) continue; // 玉がいるマス
    const int p = ReadBoardPiece(stream);
    if (p < 0) return false;
    raw.board[sq] = uint8_t(p);
  }

  // 4. 手駒（256ビットになるまで。成りフラグが立っているものは駒箱の駒）
  while (stream.cursor() < 256) {
    const int p = ReadHandPiece(stream);
    if (p < 0) return false;
    if (p & 8) {
      piece_box = true;
      continue;
    }
    const int color = p >> 4, pt = p & 7;
    if (raw.hand[color][pt] >= 18) return false;
    raw.hand[color][pt]++;
  }

  if (has_piece_box != nullptr) {
    *has_piece_box = piece_box || raw.HasPiecesInBox();
  }
  return stream.cursor() == 256 && raw.Validate();
}

std::string ToHex(const PackedSfen& packed) {
  static const char kDigits[] = "0123456789abcdef";
  std::string s;
  for (uint8_t b : packed.data) {
    s += kDigits[b >> 4];
    s += kDigits[b & 15];
  }
  return s;
}

bool FromHex(const std::string& hex, PackedSfen* out) {
  if (hex.size() != 64) return false;
  for (int i = 0; i < 32; ++i) {
    const int hi = HexValue(hex[2 * i]), lo = HexValue(hex[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    out->data[i] = uint8_t(hi * 16 + lo);
  }
  return true;
}

} // namespace SfenPacker

namespace YaneMove16 {

std::string ToUsi(uint16_t m) {
  const int to = m & 0x7f;
  const int from = (m >> 7) & 0x7f;
  const bool drop = (m & (1 << 14)) != 0;
  const bool promote = (m & (1 << 15)) != 0;
  if (to >= 81) return "";
  if (drop) {
    if (promote || from < kYPawn || from > kYGold) return "";
    return std::string{kYaneDropChars[from], '*'} + SquareToUsi(to);
  }
  if (from >= 81 || from == to) return ""; // none / null / resign / win を含む
  return SquareToUsi(from) + SquareToUsi(to) + (promote ? "+" : "");
}

uint16_t FromUsi(const std::string& usi) {
  if (usi.size() == 4 && usi[1] == '*') {
    int ypt = 0;
    for (int i = kYPawn; i <= kYGold; ++i) {
      if (usi[0] == kYaneDropChars[i]) ypt = i;
    }
    const int to = SquareFromUsi(usi[2], usi[3]);
    if (ypt == 0 || to < 0) return kNone;
    return uint16_t(to | (ypt << 7) | (1 << 14));
  }
  if (usi.size() == 4 || (usi.size() == 5 && usi[4] == '+')) {
    const int from = SquareFromUsi(usi[0], usi[1]);
    const int to = SquareFromUsi(usi[2], usi[3]);
    if (from < 0 || to < 0 || from == to) return kNone;
    return uint16_t(to | (from << 7) | (usi.size() == 5 ? (1 << 15) : 0));
  }
  return kNone;
}

uint16_t Flip(uint16_t m) {
  if (ToUsi(m).empty()) {
    return m; // 特殊な指し手・不正な値は変換しない
  }
  const int to = m & 0x7f;
  const int from = (m >> 7) & 0x7f;
  const bool drop = (m & (1 << 14)) != 0;
  const int flags = m & 0xc000;
  return uint16_t((80 - to) | ((drop ? from : 80 - from) << 7) | flags);
}

} // namespace YaneMove16
