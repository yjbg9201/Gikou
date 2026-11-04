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

#include "eval_tools.h"

#if !defined(MINIMUM)

#include <cinttypes>
#include <cstring>
#include <memory>
#include <string>
#include "evaluation.h"
#include "movegen.h"
#include "node.h"
#include "position.h"
#include "progress.h"
#include "psq.h"
#include "raw_position.h"

namespace {

// 標準的な駒の価値（歩=100）。探索の駒得計算（Material）で使われるため、乱数にはしない。
void SetStandardMaterial(EvalParameters* params) {
  struct { PieceType pt; int value; } table[] = {
    {kPawn, 100}, {kLance, 300}, {kKnight, 350}, {kSilver, 500}, {kGold, 550},
    {kBishop, 700}, {kRook, 850}, {kPPawn, 550}, {kPLance, 550}, {kPKnight, 550},
    {kPSilver, 550}, {kHorse, 950}, {kDragon, 1100},
  };
  for (const auto& e : table) {
    params->material[e.pt] = static_cast<Score>(e.value);
  }
}

std::string JsonEscape(const std::string& s) {
  std::string r;
  for (char c : s) {
    if (c == '"' || c == '\\') { r += '\\'; r += c; }
    else if (static_cast<unsigned char>(c) < 0x20) { r += ' '; }
    else { r += c; }
  }
  return r;
}

void PrintPacked(std::FILE* out, const char* name, const PackedScore& s) {
  std::fprintf(out, ",\"%s\":[%d,%d,%d,%d]", name,
               int(s[0]), int(s[1]), int(s[2]), int(s[3]));
}

/**
 * 全マスの PP（two_pieces）が対称であるかを調べ、非対称な組の数を返します.
 */
int64_t CountPpAsymmetry() {
  int64_t count = 0;
  for (PsqIndex i : PsqIndex::all_indices()) {
    for (PsqIndex j : PsqIndex::all_indices()) {
      if (int(j) <= int(i)) continue;
      const PackedScore& a = g_eval_params->two_pieces[i][j];
      const PackedScore& b = g_eval_params->two_pieces[j][i];
      if (a[0] != b[0] || a[1] != b[1] || a[2] != b[2] || a[3] != b[3]) {
        ++count;
      }
    }
  }
  return count;
}

} // namespace

namespace EvalTools {

bool WriteRandomParams(const char* file_name, uint64_t seed, int32_t amplitude) {
  static_assert(sizeof(EvalParameters) % sizeof(int32_t) == 0, "");
  std::unique_ptr<EvalParameters> params(new EvalParameters);
  Rng rng(seed);

  // 1. 全パラメータを [-amplitude, +amplitude] の一様乱数で埋める
  //    （EvalParameters は int32_t のみからなる構造体）
  int32_t* p = reinterpret_cast<int32_t*>(params.get());
  const size_t n = sizeof(EvalParameters) / sizeof(int32_t);
  const uint64_t width = 2 * static_cast<uint64_t>(amplitude) + 1;
  for (size_t i = 0; i < n; ++i) {
    p[i] = static_cast<int32_t>(static_cast<int64_t>(rng.Below(width)) - amplitude);
  }

  // KP の第4スロット（進行度の重み）は、そのままだと進行度が 0 か 1 に張り付くので小さくする
  for (Square sq : Square::all_squares()) {
    for (PsqIndex i : PsqIndex::all_indices()) {
      params->king_piece[sq][i][3] /= 64;
    }
  }

  // 2. 駒の価値は標準値に固定する
  for (PieceType pt : Piece::all_piece_types()) {
    params->material[pt] = kScoreZero;
  }
  params->material[kNoPieceType] = kScoreZero;
  SetStandardMaterial(params.get());

  // 3. PP 対称性（差分評価の整合性を支えるハード不変条件）を満たすようにする
  for (PsqIndex i : PsqIndex::all_indices()) {
    for (PsqIndex j : PsqIndex::all_indices()) {
      if (int(j) < int(i)) {
        params->two_pieces[i][j] = params->two_pieces[j][i];
      }
    }
  }

  // 4. 書き出す
  std::FILE* fp = std::fopen(file_name, "wb");
  if (fp == nullptr) {
    std::fprintf(stderr, "Failed to open %s.\n", file_name);
    return false;
  }
  const bool ok = std::fwrite(params.get(), sizeof(EvalParameters), 1, fp) == 1;
  std::fclose(fp);
  if (!ok) {
    std::fprintf(stderr, "Failed to write %s.\n", file_name);
  }
  return ok;
}

void GenerateRandomSfens(int num_positions, uint64_t seed, int max_ply,
                         std::FILE* out) {
  Rng rng(seed);
  max_ply = std::max(max_ply, 0);
  for (int n = 0; n < num_positions; ++n) {
    Position pos = Position::CreateStartPosition();
    const int target_ply = max_ply == 0 ? 0 : 1 + int(rng.Below(uint64_t(max_ply)));
    int ply = 0;
    for (; ply < target_ply; ++ply) {
      SimpleMoveList<kAllMoves, true> moves(pos);
      if (moves.size() == 0) {
        break; // 詰み（合法手なし）
      }
      pos.MakeMove(moves[size_t(rng.Below(moves.size()))].move);
    }
    std::fprintf(out, "sfen %s\n", RawPosition::FromPosition(pos).ToSfen(ply + 1).c_str());
  }
}

int DumpEvaluation(std::istream& in, std::FILE* out) {
  int num_errors = 0;
  for (std::string line; std::getline(in, line);) {
    // 空行・コメント行は読み飛ばす
    size_t first = line.find_first_not_of(" \t\r");
    if (first == std::string::npos || line[first] == '#') {
      continue;
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();

    Position pos;
    std::string error;
    if (!ParsePositionCommand(line, &pos, &error)) {
      std::fprintf(out, "{\"input\":\"%s\",\"error\":\"%s\"}\n",
                   JsonEscape(line).c_str(), JsonEscape(error).c_str());
      ++num_errors;
      continue;
    }

    const PsqList psq_list(pos);
    const EvalDetail detail = Evaluation::EvaluateAll(pos, psq_list);
    double progress = 0.0;
    const Score score = detail.ComputeFinalScore(pos.side_to_move(), &progress);
    const Score full = Evaluation::Evaluate(pos);

    std::fprintf(out, "{\"sfen\":\"%s\",\"side_to_move\":\"%c\",\"score\":%d,\"progress\":%.9f",
                 RawPosition::FromPosition(pos).ToSfen(1).c_str(),
                 pos.side_to_move() == kBlack ? 'b' : 'w', int(full), progress);
    std::fprintf(out, ",\"score_black\":%d", pos.side_to_move() == kBlack ? int(full) : -int(full));
    std::fprintf(out, ",\"progress_weight\":%d", int(detail.kp[kBlack][3] + detail.kp[kWhite][3]));
    PrintPacked(out, "kp_black", detail.kp[kBlack]);
    PrintPacked(out, "kp_white", detail.kp[kWhite]);
    PrintPacked(out, "two_pieces", detail.two_pieces);
    PrintPacked(out, "controls", detail.controls);
    PrintPacked(out, "king_safety", detail.king_safety);
    PrintPacked(out, "sliders", detail.sliders);
    PrintPacked(out, "tempo", g_eval_params->tempo);
    std::fprintf(out, ",\"num_psq\":%d", int(psq_list.size()));
    std::fprintf(out, "}\n");

    // 内訳から計算した値と Evaluation::Evaluate() は同じ値でなければならない
    if (score != full && pos.king_exists(kBlack) && pos.king_exists(kWhite)) {
      std::fprintf(stderr, "internal error: detail score %d != Evaluate() %d\n",
                   int(score), int(full));
      ++num_errors;
    }
  }
  return num_errors;
}

int RunSelfTest(int num_games, uint64_t seed) {
  int failures = 0;

  // 1. PP 対称性
  const int64_t asymmetry = CountPpAsymmetry();
  std::printf("pp_symmetry: %s (asymmetric pairs = %" PRId64 ")\n",
              asymmetry == 0 ? "OK" : "NG", asymmetry);
  if (asymmetry != 0) {
    ++failures;
  }

  // パラメータが全てゼロだと検査の意味がないので警告する
  {
    const int32_t* p = reinterpret_cast<const int32_t*>(g_eval_params.get());
    bool all_zero = true;
    for (size_t i = 0; i < sizeof(EvalParameters) / sizeof(int32_t) && all_zero; i += 4099) {
      all_zero = (p[i] == 0);
    }
    if (all_zero) {
      std::printf("warning: params.bin looks empty (all zero). Use --randparams to create one.\n");
    }
  }

  // 2. 差分評価と全計算の一致
  Rng rng(seed);
  int64_t num_checked = 0, num_mismatch = 0;
  for (int game = 0; game < num_games; ++game) {
    Node node(Position::CreateStartPosition());
    node.Evaluate();
    for (int ply = 0; ply < 256; ++ply) {
      SimpleMoveList<kAllMoves, true> moves(node);
      if (moves.size() == 0) break;
      const Move move = moves[size_t(rng.Below(moves.size()))].move;
      node.MakeMove(move);

      double progress_diff = 0.0;
      const Score diff = node.Evaluate(&progress_diff);

      const Position pos = node;
      const PsqList psq_list(pos);
      double progress_full = 0.0;
      const Score full = Evaluation::EvaluateAll(pos, psq_list)
                             .ComputeFinalScore(pos.side_to_move(), &progress_full);
      ++num_checked;
      if (diff != full || progress_diff != progress_full) {
        if (num_mismatch < 10) {
          std::printf("mismatch: game=%d ply=%d move=%s diff=%d full=%d progress=%.9f/%.9f sfen=%s\n",
                      game, ply + 1, move.ToSfen().c_str(), int(diff), int(full),
                      progress_diff, progress_full,
                      RawPosition::FromPosition(pos).ToSfen(ply + 2).c_str());
        }
        ++num_mismatch;
      }
    }
  }
  std::printf("incremental_eval: %s (checked=%" PRId64 ", mismatch=%" PRId64 ")\n",
              num_mismatch == 0 ? "OK" : "NG", num_checked, num_mismatch);
  if (num_mismatch != 0) {
    ++failures;
  }

  std::printf("eval selftest: %s\n", failures == 0 ? "PASSED" : "FAILED");
  return failures;
}

} // namespace EvalTools

#endif // !defined(MINIMUM)
