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

#include "raw_position.h"

#include <cstring>
#include <sstream>
#include <vector>
#include "movegen.h"
#include "position.h"

namespace {

// SFEN の駒文字（大文字=先手）に対応する技巧の PieceType
int PieceTypeFromChar(char c) {
  switch (c) {
    case 'P': case 'p': return kPawn;
    case 'L': case 'l': return kLance;
    case 'N': case 'n': return kKnight;
    case 'S': case 's': return kSilver;
    case 'G': case 'g': return kGold;
    case 'B': case 'b': return kBishop;
    case 'R': case 'r': return kRook;
    case 'K': case 'k': return kKing;
    default: return 0;
  }
}

const char kPieceChars[] = " PLNSGBRK";

// 駒種ごとの最大枚数（技巧の PieceType で引く。玉は各手番1枚なので2）
const int kMaxPieces[9] = {0, 18, 4, 4, 4, 4, 2, 2, 2};

void SetError(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
}

} // namespace

void RawPosition::Clear() {
  std::memset(board, 0, sizeof(board));
  std::memset(hand, 0, sizeof(hand));
  side_to_move = kBlack;
  game_ply = 0;
}

bool RawPosition::ParseSfen(const std::string& sfen, std::string* error) {
  Clear();

  std::istringstream is(sfen);
  std::string board_str, stm_str, hand_str, ply_str;
  if (!(is >> board_str >> stm_str >> hand_str)) {
    SetError(error, "SFEN must have at least 3 fields");
    return false;
  }
  is >> ply_str; // 省略可

  // 1. 盤面
  int rank = 0, file = 8; // 技巧の File は 0=1筋、8=9筋。SFENは9筋から書かれる
  bool promoted = false;
  for (size_t i = 0; i < board_str.size(); ++i) {
    const char c = board_str[i];
    if (c == '/') {
      if (promoted || file != -1) {
        SetError(error, "invalid rank length in SFEN");
        return false;
      }
      ++rank;
      file = 8;
      if (rank >= 9) {
        SetError(error, "too many ranks in SFEN");
        return false;
      }
    } else if (c >= '1' && c <= '9') {
      if (promoted) {
        SetError(error, "'+' followed by a digit");
        return false;
      }
      file -= (c - '0');
      if (file < -1) {
        SetError(error, "rank overflow in SFEN");
        return false;
      }
    } else if (c == '+') {
      if (promoted) {
        SetError(error, "double '+'");
        return false;
      }
      promoted = true;
    } else {
      int pt = PieceTypeFromChar(c);
      if (pt == 0 || file < 0) {
        SetError(error, std::string("invalid piece or rank overflow: ") + c);
        return false;
      }
      if (promoted) {
        if (pt == kGold || pt == kKing) {
          SetError(error, "promoted gold or king");
          return false;
        }
        pt += 8;
      }
      const Color color = (c >= 'a' && c <= 'z') ? kWhite : kBlack;
      board[file * 9 + rank] = static_cast<uint8_t>(16 * color + pt);
      --file;
      promoted = false;
    }
  }
  if (rank != 8 || file != -1 || promoted) {
    SetError(error, "SFEN board must have 9x9 squares");
    return false;
  }

  // 2. 手番
  if (stm_str == "b") {
    side_to_move = kBlack;
  } else if (stm_str == "w") {
    side_to_move = kWhite;
  } else {
    SetError(error, "invalid side to move: " + stm_str);
    return false;
  }

  // 3. 持ち駒
  if (hand_str != "-") {
    int n = 0;
    for (size_t i = 0; i < hand_str.size(); ++i) {
      const char c = hand_str[i];
      if (c >= '0' && c <= '9') {
        n = n * 10 + (c - '0');
        if (n > 18) {
          SetError(error, "too many pieces in hand");
          return false;
        }
        continue;
      }
      const int pt = PieceTypeFromChar(c);
      if (pt == 0 || pt == kKing) {
        SetError(error, std::string("invalid hand piece: ") + c);
        return false;
      }
      const Color color = (c >= 'a' && c <= 'z') ? kWhite : kBlack;
      const int count = (n == 0) ? 1 : n;
      if (hand[color][pt] + count > kMaxPieces[pt]) {
        SetError(error, "too many pieces in hand");
        return false;
      }
      hand[color][pt] = static_cast<uint8_t>(hand[color][pt] + count);
      n = 0;
    }
    if (n != 0) {
      SetError(error, "hand ends with a number");
      return false;
    }
  }

  // 4. 手数（省略可。数字以外なら不正）
  if (!ply_str.empty()) {
    int ply = 0;
    for (char c : ply_str) {
      if (c < '0' || c > '9' || ply > 100000000) {
        SetError(error, "invalid move count: " + ply_str);
        return false;
      }
      ply = ply * 10 + (c - '0');
    }
    game_ply = ply;
  }

  // 5. 駒数の検査（駒種ごとの最大枚数、玉は各手番1枚まで）
  int counts[9] = {0};
  int kings[2] = {0, 0};
  for (int sq = 0; sq < 81; ++sq) {
    if (board[sq] == 0) continue;
    const int pt = board[sq] & 15;
    if (pt == kKing) {
      kings[board[sq] >> 4]++;
    } else {
      counts[pt & 7]++;
    }
  }
  for (int c = 0; c < 2; ++c) {
    for (int pt = kPawn; pt <= kRook; ++pt) {
      counts[pt] += hand[c][pt];
    }
  }
  for (int pt = kPawn; pt <= kRook; ++pt) {
    if (counts[pt] > kMaxPieces[pt]) {
      SetError(error, std::string("too many pieces: ") + kPieceChars[pt]);
      return false;
    }
  }
  if (kings[0] > 1 || kings[1] > 1) {
    SetError(error, "too many kings");
    return false;
  }

  return true;
}

std::string RawPosition::ToSfen(int ply) const {
  std::string sfen;
  for (int rank = 0; rank < 9; ++rank) {
    int empty = 0;
    for (int file = 8; file >= 0; --file) {
      const int p = board[file * 9 + rank];
      if (p == 0) {
        ++empty;
        continue;
      }
      if (empty > 0) {
        sfen += char('0' + empty);
        empty = 0;
      }
      const bool is_king = (p & 15) == kKing;
      if (!is_king && (p & 8)) sfen += '+';
      char c = kPieceChars[is_king ? kKing : (p & 7)];
      if ((p >> 4) == kWhite) c = char(c - 'A' + 'a');
      sfen += c;
    }
    if (empty > 0) sfen += char('0' + empty);
    if (rank != 8) sfen += '/';
  }
  sfen += side_to_move == kBlack ? " b " : " w ";

  std::string hand_str;
  const int order[] = {kRook, kBishop, kGold, kSilver, kKnight, kLance, kPawn};
  for (int c = 0; c < 2; ++c) {
    for (int pt : order) {
      const int n = hand[c][pt];
      if (n == 0) continue;
      if (n >= 2) hand_str += std::to_string(n);
      hand_str += c == kBlack ? kPieceChars[pt] : char(kPieceChars[pt] - 'A' + 'a');
    }
  }
  sfen += hand_str.empty() ? "-" : hand_str;

  if (ply > 0) {
    sfen += " " + std::to_string(ply);
  }
  return sfen;
}

RawPosition RawPosition::FromPosition(const Position& pos) {
  RawPosition raw;
  for (Square s : Square::all_squares()) {
    raw.board[int(s)] = static_cast<uint8_t>(int(pos.piece_on(s)));
  }
  for (Color c : {kBlack, kWhite}) {
    for (PieceType pt : Piece::all_hand_types()) {
      raw.hand[c][pt] = static_cast<uint8_t>(pos.hand(c).count(pt));
    }
  }
  raw.side_to_move = pos.side_to_move();
  raw.game_ply = 0;
  return raw;
}

Position RawPosition::ToPosition() const {
  return Position::FromSfen(ToSfen(1));
}

RawPosition RawPosition::Flipped() const {
  RawPosition r;
  for (int sq = 0; sq < 81; ++sq) {
    const int p = board[sq];
    r.board[80 - sq] = static_cast<uint8_t>(p == 0 ? 0 : (p ^ 16));
  }
  for (int pt = 0; pt < 8; ++pt) {
    r.hand[kBlack][pt] = hand[kWhite][pt];
    r.hand[kWhite][pt] = hand[kBlack][pt];
  }
  r.side_to_move = ~side_to_move;
  r.game_ply = game_ply;
  return r;
}

int RawPosition::king_square(Color c) const {
  const int king = 16 * c + kKing;
  for (int sq = 0; sq < 81; ++sq) {
    if (board[sq] == king) return sq;
  }
  return -1;
}

bool RawPosition::HasPiecesInBox() const {
  int counts[8] = {0};
  for (int sq = 0; sq < 81; ++sq) {
    const int p = board[sq];
    if (p != 0 && (p & 15) != kKing) counts[p & 7]++;
  }
  for (int c = 0; c < 2; ++c)
    for (int pt = kPawn; pt <= kRook; ++pt) counts[pt] += hand[c][pt];
  for (int pt = kPawn; pt <= kRook; ++pt) {
    if (counts[pt] != kMaxPieces[pt]) return true;
  }
  return king_square(kBlack) < 0 || king_square(kWhite) < 0;
}

bool RawPosition::operator==(const RawPosition& rhs) const {
  return std::memcmp(board, rhs.board, sizeof(board)) == 0
      && std::memcmp(hand, rhs.hand, sizeof(hand)) == 0
      && side_to_move == rhs.side_to_move;
}

bool IsValidPositionForSearch(const Position& pos) {
  if (!pos.king_exists(kBlack) || !pos.king_exists(kWhite)) {
    return false;
  }
  const Color stm = pos.side_to_move();
  // 手番でない側の玉に王手がかかっている（玉を取れる）局面は不正
  return pos.AttackersTo(pos.king_square(~stm), pos.pieces(), stm).none();
}

Move FindLegalMoveFromUsi(const Position& pos, const std::string& usi_move) {
  if (usi_move.size() < 4 || usi_move.size() > 5) {
    return kMoveNone;
  }
  for (const ExtMove& em : SimpleMoveList<kAllMoves, true>(pos)) {
    if (em.move.ToSfen() == usi_move) {
      return em.move;
    }
  }
  return kMoveNone;
}

bool ParsePositionCommand(const std::string& line, Position* const pos,
                          std::string* const error) {
  std::istringstream is(line);
  std::vector<std::string> tokens;
  for (std::string t; is >> t;) tokens.push_back(t);
  size_t i = 0;
  if (i < tokens.size() && tokens[i] == "position") ++i;
  if (i >= tokens.size()) {
    SetError(error, "empty position");
    return false;
  }

  RawPosition raw;
  if (tokens[i] == "startpos") {
    raw.ParseSfen("lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1");
    ++i;
  } else {
    if (tokens[i] == "sfen") ++i;
    std::string sfen;
    size_t n = 0;
    for (; i < tokens.size() && n < 4 && tokens[i] != "moves"; ++i, ++n) {
      sfen += (n ? " " : "") + tokens[i];
    }
    if (!raw.ParseSfen(sfen, error)) {
      return false;
    }
  }
  *pos = raw.ToPosition();

  if (i < tokens.size()) {
    if (tokens[i] != "moves") {
      SetError(error, "unexpected token: " + tokens[i]);
      return false;
    }
    for (++i; i < tokens.size(); ++i) {
      const Move move = FindLegalMoveFromUsi(*pos, tokens[i]);
      if (move == kMoveNone) {
        SetError(error, "illegal move: " + tokens[i]);
        return false;
      }
      pos->MakeMove(move);
    }
  }
  return true;
}
