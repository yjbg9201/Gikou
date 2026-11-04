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

#include "yaneuraou_psv.h"

#if !defined(MINIMUM)

#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <omp.h>
#include "huffman_code.h"
#include "position.h"
#include "raw_position.h"

namespace {

#include "psv_selftest_vectors.inc"

bool ParseInt64(const std::string& s, int64_t* out) {
  if (s.empty()) return false;
  char* end = nullptr;
  long long v = std::strtoll(s.c_str(), &end, 10);
  if (end == nullptr || *end != '\0') return false;
  *out = v;
  return true;
}

void Add(PsvStats* lhs, const PsvStats& rhs) {
  lhs->records              += rhs.records;
  lhs->invalid              += rhs.invalid;
  lhs->piece_box            += rhs.piece_box;
  lhs->illegal_move         += rhs.illegal_move;
  lhs->rootstrap            += rhs.rootstrap;
  lhs->rootstrap_eval_limit += rhs.rootstrap_eval_limit;
  lhs->regression           += rhs.regression;
  lhs->regression_draw      += rhs.regression_draw;
}

} // namespace

bool PsvOptions::Parse(const std::string& option, std::string* error) {
  const size_t eq = option.find('=');
  if (eq == std::string::npos) {
    *error = "option must be key=value: " + option;
    return false;
  }
  const std::string key = option.substr(0, eq), value = option.substr(eq + 1);
  if (key == "scale") {
    char* end = nullptr;
    double v = std::strtod(value.c_str(), &end);
    if (value.empty() || *end != '\0' || !(v > 0.0) || v > 100.0) {
      *error = "invalid scale: " + value;
      return false;
    }
    scale = v;
  } else if (key == "eval_limit") {
    int64_t v;
    if (!ParseInt64(value, &v) || v < 0 || v > 100000) {
      *error = "invalid eval_limit: " + value;
      return false;
    }
    eval_limit = int(v);
  } else if (key == "max") {
    int64_t v;
    if (!ParseInt64(value, &v) || v < 0) {
      *error = "invalid max: " + value;
      return false;
    }
    max_records = uint64_t(v);
  } else {
    *error = "unknown option: " + key;
    return false;
  }
  return true;
}

void PsvStats::Print() const {
  std::printf("PSV records          : %" PRIu64 "\n", records);
  std::printf("  invalid (skipped)  : %" PRIu64 "\n", invalid);
  std::printf("  piece box (skipped): %" PRIu64 "\n", piece_box);
  std::printf("  illegal move       : %" PRIu64 " (position is still used)\n", illegal_move);
  std::printf("  rootstrap teachers : %" PRIu64 " (excluded by eval_limit: %" PRIu64 ")\n",
              rootstrap, rootstrap_eval_limit);
  std::printf("  regression teachers: %" PRIu64 " (excluded draws: %" PRIu64 ")\n",
              regression, regression_draw);
}

namespace YaneuraOuPsv {

bool ConvertRecord(const PackedSfenValue& psv, const PsvOptions& options,
                   TeacherPosition* const rootstrap, bool* const has_rootstrap,
                   TeacherPosition* const regression, bool* const has_regression,
                   PsvStats* const stats) {
  *has_rootstrap = false;
  *has_regression = false;
  stats->records++;

  // 1. やねうら王形式の PackedSfen を復号する
  RawPosition raw;
  bool piece_box = false;
  if (!SfenPacker::Unpack(psv.sfen, &raw, &piece_box)) {
    stats->invalid++;
    return false;
  }

  // 2. 駒箱に駒がある局面（駒落ち等）は、技巧のハフマン符号で表せないのでスキップする
  if (piece_box) {
    stats->piece_box++;
    return false;
  }

  // 3. 局面として不正（手番でない側に王手がかかっている等）ならスキップする
  const Position pos = raw.ToPosition();
  if (!IsValidPositionForSearch(pos)) {
    stats->invalid++;
    return false;
  }

  // 4. 共通部分（局面・最善手）
  TeacherPosition teacher;
  teacher.huffman_code = HuffmanCode::EncodePosition(pos);
  teacher.move = kMoveNone;
  const std::string usi = YaneMove16::ToUsi(psv.move);
  if (!usi.empty()) {
    teacher.move = FindLegalMoveFromUsi(pos, usi);
  }
  if (teacher.move == kMoveNone) {
    stats->illegal_move++;
  }

  // 5. RootStrap 用: 評価値（手番視点同士なので符号反転は不要）。eval_limit を適用し、引き分けも残す
  if (rootstrap != nullptr) {
    const double scaled = std::round(double(psv.score) * options.scale);
    const int score = int(std::max(std::min(scaled, double(kScoreMate)), -double(kScoreMate)));
    if (options.eval_limit > 0 && std::abs(score) > options.eval_limit) {
      stats->rootstrap_eval_limit++;
    } else {
      *rootstrap = teacher;
      rootstrap->score = static_cast<Score>(score);
      *has_rootstrap = true;
      stats->rootstrap++;
    }
  }

  // 6. 勝敗回帰用: 手番側の勝ちなら +kScoreKnownWin（技巧の teacher_games.bin と同じ表現）。
  //    引き分けは除外し、eval_limit は適用しない（技巧の原仕様どおり）
  if (regression != nullptr) {
    if (psv.game_result == 0) {
      stats->regression_draw++;
    } else {
      *regression = teacher;
      regression->score = psv.game_result > 0 ? kScoreKnownWin : -kScoreKnownWin;
      *has_regression = true;
      stats->regression++;
    }
  }

  return true;
}

bool ReadFile(const std::string& file_name, const PsvOptions& options,
              std::vector<TeacherPosition>* const rootstrap,
              std::vector<TeacherPosition>* const regression,
              PsvStats* const stats) {
  std::FILE* fp = std::fopen(file_name.c_str(), "rb");
  if (fp == nullptr) {
    std::printf("Failed to open %s.\n", file_name.c_str());
    return false;
  }

  // ファイルサイズが40バイトの倍数であることを確認する
  std::fseek(fp, 0, SEEK_END);
  const long long file_size = std::ftell(fp);
  std::fseek(fp, 0, SEEK_SET);
  if (file_size < 0 || file_size % sizeof(PackedSfenValue) != 0) {
    std::printf("%s: file size (%lld) is not a multiple of %zu bytes. Not a PSV file?\n",
                file_name.c_str(), file_size, sizeof(PackedSfenValue));
    std::fclose(fp);
    return false;
  }
  uint64_t num_records = uint64_t(file_size) / sizeof(PackedSfenValue);
  if (options.max_records != 0) {
    num_records = std::min(num_records, options.max_records);
  }

  // チャンク単位で読み込み、並列に変換する（出力の順序はファイルの順序と同じ）
  const size_t kChunkSize = 1 << 18;
  std::vector<PackedSfenValue> chunk;
  struct Result {
    TeacherPosition rootstrap, regression;
    bool has_rootstrap, has_regression;
  };
  std::vector<Result> results;
  const int num_threads = std::max(1, omp_get_max_threads());
  std::vector<PsvStats> thread_stats(num_threads);

  for (uint64_t done = 0; done < num_records;) {
    const size_t n = size_t(std::min<uint64_t>(kChunkSize, num_records - done));
    chunk.resize(n);
    if (std::fread(chunk.data(), sizeof(PackedSfenValue), n, fp) != n) {
      std::printf("Failed to read %s.\n", file_name.c_str());
      std::fclose(fp);
      return false;
    }
    results.resize(n);
#pragma omp parallel for schedule(dynamic, 1024)
    for (size_t i = 0; i < n; ++i) {
      Result& r = results[i];
      ConvertRecord(chunk[i], options,
                    &r.rootstrap, &r.has_rootstrap,
                    &r.regression, &r.has_regression,
                    &thread_stats[omp_get_thread_num()]);
      if (rootstrap == nullptr) r.has_rootstrap = false;
      if (regression == nullptr) r.has_regression = false;
    }
    for (const Result& r : results) {
      if (r.has_rootstrap) rootstrap->push_back(r.rootstrap);
      if (r.has_regression) regression->push_back(r.regression);
    }
    done += n;
  }
  std::fclose(fp);

  // rootstrap / regression を作らない場合は、その統計を数えない
  for (PsvStats& s : thread_stats) {
    if (rootstrap == nullptr) { s.rootstrap = 0; s.rootstrap_eval_limit = 0; }
    if (regression == nullptr) { s.regression = 0; s.regression_draw = 0; }
    Add(stats, s);
  }
  return true;
}

int RunSelfTest() {
  int failures = 0;
  auto check = [&](bool ok, const std::string& what) {
    if (!ok) {
      std::printf("NG: %s\n", what.c_str());
      ++failures;
    }
  };

  // 1. PackedSfen: cshogi の to_psfen() とのバイト一致、復号の一致、技巧ハフマン符号の往復
  int num_packed = 0;
  for (const auto& v : kPackedSfenVectors) {
    const std::string sfen(v.sfen);
    RawPosition raw;
    std::string error;
    if (!raw.ParseSfen(sfen, &error)) {
      check(false, "ParseSfen " + sfen + ": " + error);
      continue;
    }
    PackedSfen packed;
    check(SfenPacker::Pack(raw, &packed), "Pack " + sfen);
    check(SfenPacker::ToHex(packed) == v.packed,
          "Pack mismatch " + sfen + "\n    got      " + SfenPacker::ToHex(packed) +
          "\n    expected " + v.packed);

    PackedSfen expected;
    check(SfenPacker::FromHex(v.packed, &expected), "FromHex");
    RawPosition unpacked;
    bool piece_box = false;
    check(SfenPacker::Unpack(expected, &unpacked, &piece_box), "Unpack " + sfen);
    check(unpacked == raw, "Unpack mismatch " + sfen + " -> " + unpacked.ToSfen());
    check(piece_box == raw.HasPiecesInBox(), "piece box flag " + sfen);

    // 反転局面も往復できること
    PackedSfen flipped;
    RawPosition back;
    check(SfenPacker::Pack(raw.Flipped(), &flipped) && SfenPacker::Unpack(flipped, &back)
          && back == raw.Flipped() && back.Flipped() == raw, "flip round trip " + sfen);

    // 技巧のハフマン符号（駒箱なしの局面のみ）の往復
    if (!piece_box) {
      const Position pos = raw.ToPosition();
      const Position decoded = HuffmanCode::DecodePosition(HuffmanCode::EncodePosition(pos));
      check(RawPosition::FromPosition(decoded) == raw, "Gikou HuffmanCode round trip " + sfen);
    }
    ++num_packed;
  }
  std::printf("packed_sfen: %d vectors\n", num_packed);

  // 2. Move16: cshogi の move16_to_psv() との一致
  int num_moves = 0;
  for (const auto& v : kMove16Vectors) {
    check(YaneMove16::FromUsi(v.usi) == v.move16, std::string("Move16 FromUsi ") + v.usi);
    check(YaneMove16::ToUsi(v.move16) == v.usi, std::string("Move16 ToUsi ") + v.usi);
    check(YaneMove16::Flip(YaneMove16::Flip(v.move16)) == v.move16, std::string("Move16 Flip ") + v.usi);
    ++num_moves;
  }
  check(YaneMove16::ToUsi(YaneMove16::kNone).empty(), "Move16 none");
  check(YaneMove16::ToUsi(YaneMove16::kNull).empty(), "Move16 null");
  check(YaneMove16::ToUsi(YaneMove16::kResign).empty(), "Move16 resign");
  check(YaneMove16::ToUsi(YaneMove16::kWin).empty(), "Move16 win");
  check(YaneMove16::FromUsi("7g7f") == YaneMove16::Flip(YaneMove16::FromUsi("3c3d")), "Move16 Flip 7g7f");
  check(YaneMove16::FromUsi("P*5e") == YaneMove16::Flip(YaneMove16::FromUsi("P*5e")), "Move16 Flip drop");
  check(YaneMove16::FromUsi("Z*5e") == YaneMove16::kNone, "Move16 invalid drop");
  check(YaneMove16::FromUsi("7g7g") == YaneMove16::kNone, "Move16 from == to");
  std::printf("move16: %d vectors\n", num_moves);

  // 3. PSV レコードの変換規則
  {
    auto make = [](const std::string& sfen, int score, const std::string& move, int result) {
      PackedSfenValue psv;
      std::memset(&psv, 0, sizeof(psv));
      RawPosition raw;
      raw.ParseSfen(sfen);
      SfenPacker::Pack(raw, &psv.sfen);
      psv.score = int16_t(score);
      psv.move = YaneMove16::FromUsi(move);
      psv.game_ply = 1;
      psv.game_result = int8_t(result);
      return psv;
    };
    const std::string startpos = "lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1";
    const std::string white = "lnsgkgsnl/1r5b1/ppppppppp/9/9/2P6/PP1PPPPPP/1B5R1/LNSGKGSNL w - 2";
    TeacherPosition rs, rg;
    bool has_rs, has_rg;
    PsvStats stats;
    PsvOptions opt;

    // a. 通常: score はそのまま、勝ちは +kScoreKnownWin
    check(ConvertRecord(make(startpos, 123, "7g7f", 1), opt, &rs, &has_rs, &rg, &has_rg, &stats)
          && has_rs && has_rg && rs.score == 123 && rg.score == kScoreKnownWin
          && rs.move.ToSfen() == "7g7f"
          && RawPosition::FromPosition(HuffmanCode::DecodePosition(rs.huffman_code)).ToSfen(1) == startpos,
          "PSV basic");

    // b. 後手番: 手番視点のまま（符号反転しない）、負けは -kScoreKnownWin
    check(ConvertRecord(make(white, -250, "3c3d", -1), opt, &rs, &has_rs, &rg, &has_rg, &stats)
          && has_rs && has_rg && rs.score == -250 && rg.score == -kScoreKnownWin
          && rs.move.ToSfen() == "3c3d"
          && HuffmanCode::DecodePosition(rs.huffman_code).side_to_move() == kWhite,
          "PSV white to move");

    // c. scale（四捨五入）
    PsvOptions opt_scale;
    opt_scale.scale = 1.11;
    check(ConvertRecord(make(startpos, 123, "7g7f", 1), opt_scale, &rs, &has_rs, &rg, &has_rg, &stats)
          && rs.score == 137 && rg.score == kScoreKnownWin, "PSV scale");

    // d. 引き分けは RootStrap に残し、勝敗回帰からは除外
    check(ConvertRecord(make(startpos, 10, "7g7f", 0), opt, &rs, &has_rs, &rg, &has_rg, &stats)
          && has_rs && !has_rg, "PSV draw");

    // e. eval_limit は RootStrap にだけ適用
    check(ConvertRecord(make(startpos, 3001, "7g7f", 1), opt, &rs, &has_rs, &rg, &has_rg, &stats)
          && !has_rs && has_rg, "PSV eval_limit");
    check(ConvertRecord(make(startpos, -3000, "7g7f", -1), opt, &rs, &has_rs, &rg, &has_rg, &stats)
          && has_rs && rs.score == -3000, "PSV eval_limit boundary");

    // f. 非合法手は kMoveNone（局面は使う）
    check(ConvertRecord(make(startpos, 0, "7g7e", 1), opt, &rs, &has_rs, &rg, &has_rg, &stats)
          && has_rs && rs.move == kMoveNone, "PSV illegal move");

    // g. 駒箱に駒がある局面はスキップ
    check(!ConvertRecord(make("lnsgkgsn1/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL w - 1", 0, "3c3d", 1),
                         opt, &rs, &has_rs, &rg, &has_rg, &stats) && !has_rs && !has_rg, "PSV piece box");

    // h. 壊れたビット列はスキップ（クラッシュしない）
    PackedSfenValue broken = make(startpos, 0, "7g7f", 1);
    std::memset(broken.sfen.data, 0xff, sizeof(broken.sfen.data));
    check(!ConvertRecord(broken, opt, &rs, &has_rs, &rg, &has_rg, &stats), "PSV broken bits");

    // i. 手番でない側の玉に王手がかかっている局面はスキップ
    check(!ConvertRecord(make("4k4/9/9/9/4R4/9/9/9/4K4 b r2b4g4s4n4l18p 1", 0, "5e5d", 1),
                         opt, &rs, &has_rs, &rg, &has_rg, &stats), "PSV illegal position");
  }
  std::printf("psv_conversion: done\n");

  std::printf("psv selftest: %s\n", failures == 0 ? "PASSED" : "FAILED");
  return failures;
}

} // namespace YaneuraOuPsv

#endif // !defined(MINIMUM)
