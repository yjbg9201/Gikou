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

#include "yaneura_book.h"

#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <sys/stat.h>
#include "position.h"
#include "raw_position.h"
#include "synced_printf.h"
#include "usi.h"

namespace {

const char kDbHeader[] = "#YANEURAOU-DB2016";
const char kYbbMagic[16] = {'Y', 'A', 'N', 'E', '-', 'B', 'I', 'N',
                            'B', 'O', 'O', 'K', '-', 'V', '1', '\0'};

bool EndsWith(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool FileExists(const std::string& file_name) {
  struct stat st;
  return ::stat(file_name.c_str(), &st) == 0;
}

/** 文字列の先頭から、空白で区切られたトークンを1つ取り出す */
bool NextToken(const char*& p, const char* end, std::string* token) {
  while (p < end && (*p == ' ' || *p == '\t')) ++p;
  if (p >= end) return false;
  const char* begin = p;
  while (p < end && *p != ' ' && *p != '\t') ++p;
  token->assign(begin, p);
  return true;
}

/** 整数のトークンを読む。なければ（または数でなければ）default_value */
int64_t NextNumber(const char*& p, const char* end, int64_t default_value) {
  std::string token;
  if (!NextToken(p, end, &token)) return default_value;
  char* e = nullptr;
  long long v = std::strtoll(token.c_str(), &e, 10);
  if (e == token.c_str()) return default_value;
  return v;
}

uint16_t ReadU16(const uint8_t* p) {
  return uint16_t(p[0] | (p[1] << 8));
}

uint64_t ReadU64(const uint8_t* p) {
  uint64_t v = 0;
  for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
  return v;
}

int SeekFile(std::FILE* fp, uint64_t offset) {
#if defined(_WIN32)
  return _fseeki64(fp, static_cast<__int64>(offset), SEEK_SET);
#else
  return fseeko(fp, static_cast<off_t>(offset), SEEK_SET);
#endif
}

constexpr uint64_t kYbbHeaderSize = 32;
constexpr uint64_t kYbbIndexRecordSize = 44;
constexpr uint64_t kYbbFlagMoveDepth = 1;

uint64_t YbbMoveRecordSize(uint64_t flags) {
  return (flags & kYbbFlagMoveDepth) ? 6 : 4;
}

/** .ybb の指し手レコードを読む */
void ParseYbbMoves(const uint8_t* p, uint16_t count, uint64_t flags,
                   std::vector<YaneBookMove>* moves) {
  const uint64_t record_size = YbbMoveRecordSize(flags);
  moves->clear();
  for (uint16_t i = 0; i < count; ++i, p += record_size) {
    YaneBookMove bm;
    bm.move = ReadU16(p);
    bm.ponder = 0;
    bm.value = int16_t(ReadU16(p + 2));
    bm.depth = (flags & kYbbFlagMoveDepth) ? ReadU16(p + 4) : 0;
    bm.move_count = 0;
    moves->push_back(bm);
  }
}

std::string MoveToString(Move m) {
  return m == kMoveNone ? std::string() : m.ToSfen();
}

} // namespace

YaneuraOuBook::YaneuraOuBook() {
#ifdef PSEUDO_RANDOM_DEVICE
  random_.seed(static_cast<std::mt19937::result_type>(
      std::chrono::high_resolution_clock::now().time_since_epoch().count()));
#else
  std::random_device rd;
  random_.seed(rd());
#endif
}

YaneuraOuBook::~YaneuraOuBook() {
  Clear();
}

void YaneuraOuBook::Clear() {
  loaded_ = false;
  has_depth_ = false;
  file_name_.clear();
  file_size_ = 0;
  file_mtime_ = 0;
  index_.clear();
  index_.shrink_to_fit();
  moves_.clear();
  moves_.shrink_to_fit();
  ybb_data_.clear();
  ybb_data_.shrink_to_fit();
  if (ybb_file_ != nullptr) {
    std::fclose(ybb_file_);
    ybb_file_ = nullptr;
  }
  ybb_record_count_ = ybb_flags_ = ybb_moves_base_ = 0;
}

YaneuraOuBook::Format YaneuraOuBook::DetectFormat(const std::string& file_name,
                                                  std::string* resolved_file_name) {
  std::string name = file_name;

  // .db が存在せず、同名の .ybb があれば、そちらを使う（やねうら王と同じ）
  if (!FileExists(name) && EndsWith(name, ".db")) {
    const std::string ybb = name.substr(0, name.size() - 3) + ".ybb";
    if (FileExists(ybb)) {
      name = ybb;
    }
  }
  if (resolved_file_name != nullptr) {
    *resolved_file_name = name;
  }

  std::FILE* fp = std::fopen(name.c_str(), "rb");
  if (fp != nullptr) {
    char head[32] = {0};
    const size_t n = std::fread(head, 1, sizeof(head), fp);
    std::fclose(fp);
    if (n >= 16 && std::memcmp(head, kYbbMagic, 16) == 0) {
      return kYbbFormat;
    }
    // UTF-8 の BOM は読み飛ばす
    const char* text = head;
    if (n >= 3 && std::memcmp(head, "\xEF\xBB\xBF", 3) == 0) text += 3;
    if (std::strncmp(text, kDbHeader, std::strlen(kDbHeader)) == 0) {
      return kDbFormat;
    }
  }

  if (EndsWith(name, ".db")) return kDbFormat;
  if (EndsWith(name, ".ybb")) return kYbbFormat;
  return kGikouFormat;
}

bool YaneuraOuBook::Load(const std::string& file_name, Format format, bool on_the_fly) {
  if (format == kDbFormat) {
    on_the_fly = false; // .db は常に全読込
  }

  // 同じファイルを同じモードで読み込み済みなら、読み直さない
  struct stat st;
  const bool exists = ::stat(file_name.c_str(), &st) == 0;
  if (exists && loaded_ && file_name == file_name_ && format == format_
      && on_the_fly == on_the_fly_ && uint64_t(st.st_size) == file_size_
      && int64_t(st.st_mtime) == file_mtime_) {
    return true;
  }

  Clear();
  if (!exists) {
    SYNCED_PRINTF("info string Failed to Open %s.\n", file_name.c_str());
    return false;
  }

  const auto start = std::chrono::steady_clock::now();
  const bool ok = (format == kYbbFormat) ? LoadYbb(file_name, on_the_fly) : LoadDb(file_name);
  if (!ok) {
    Clear();
    SYNCED_PRINTF("info string YaneuraOu book is disabled because %s could not be read.\n",
                  file_name.c_str());
    return false;
  }

  loaded_ = true;
  file_name_ = file_name;
  file_size_ = uint64_t(st.st_size);
  file_mtime_ = int64_t(st.st_mtime);
  format_ = format;
  on_the_fly_ = on_the_fly;

  const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  SYNCED_PRINTF("info string YaneuraOu book loaded: %s (%s%s, %llu positions, %.1f sec)\n",
                file_name.c_str(), format == kYbbFormat ? "ybb" : "db",
                on_the_fly ? ", on the fly" : "", (unsigned long long)num_positions(), elapsed);
  if (!has_depth_) {
    SYNCED_PRINTF("info string This book has no depth information. BookDepthLimit is ignored.\n");
  }
  return true;
}

uint64_t YaneuraOuBook::num_positions() const {
  return format_ == kYbbFormat ? ybb_record_count_ : uint64_t(index_.size());
}

bool YaneuraOuBook::LoadDb(const std::string& file_name) {
  std::ifstream in(file_name.c_str(), std::ios::in | std::ios::binary);
  if (!in) {
    SYNCED_PRINTF("info string Failed to Open %s.\n", file_name.c_str());
    return false;
  }

  // 1. ファイル順に、局面ごとのブロックとして読み込む
  struct Block {
    PackedSfen key;
    uint32_t first, count, seq;
  };
  std::vector<Block> blocks;
  std::vector<YaneBookMove> raw_moves;
  bool in_valid_block = false;
  uint64_t bad_positions = 0, bad_moves = 0, line_number = 0;
  std::string line, token;

  while (std::getline(in, line)) {
    ++line_number;
    // 末尾の改行・空白を除く
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
      line.pop_back();
    }
    if (line_number == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) {
      line.erase(0, 3);
    }
    if (line.empty() || line[0] == '#' || line.compare(0, 2, "//") == 0) {
      continue;
    }

    // "sfen " で始まる行は局面
    if (line.compare(0, 5, "sfen ") == 0) {
      in_valid_block = false;
      RawPosition raw;
      PackedSfen key;
      if (!raw.ParseSfen(line.substr(5)) || !SfenPacker::Pack(raw, &key)) {
        if (bad_positions++ < 5) {
          SYNCED_PRINTF("info string Error! : Invalid SFEN in book (line %llu) : %s\n",
                        (unsigned long long)line_number, line.c_str());
        }
        continue;
      }
      if (raw_moves.size() >= UINT32_MAX - 1 || blocks.size() >= UINT32_MAX - 1) {
        SYNCED_PRINTF("info string Error! : The book is too large.\n");
        return false;
      }
      blocks.push_back(Block{key, uint32_t(raw_moves.size()), 0, uint32_t(blocks.size())});
      in_valid_block = true;
      continue;
    }

    // それ以外は指し手の行: "指し手 予想手 評価値 深さ 採択回数"（評価値以降は省略可）
    if (!in_valid_block) {
      continue; // 局面が不正、または局面より前にある指し手は読み飛ばす
    }
    const char* p = line.data();
    const char* end = p + line.size();
    std::string move_str, ponder_str;
    NextToken(p, end, &move_str);
    NextToken(p, end, &ponder_str);
    YaneBookMove bm;
    bm.move = YaneMove16::FromUsi(move_str);
    bm.ponder = YaneMove16::FromUsi(ponder_str); // "none" などは 0 になる
    bm.value = int32_t(std::max<int64_t>(std::min<int64_t>(NextNumber(p, end, 0), INT32_MAX), INT32_MIN));
    bm.depth = int32_t(std::max<int64_t>(std::min<int64_t>(NextNumber(p, end, 0), INT32_MAX), 0));
    bm.move_count = uint64_t(std::max<int64_t>(NextNumber(p, end, 1), 0));
    if (bm.move == YaneMove16::kNone) {
      ++bad_moves;
      continue;
    }
    raw_moves.push_back(bm);
    blocks.back().count++;
  }

  if (bad_positions > 0 || bad_moves > 0) {
    SYNCED_PRINTF("info string Warning : %llu invalid positions and %llu invalid moves are ignored.\n",
                  (unsigned long long)bad_positions, (unsigned long long)bad_moves);
  }

  // 2. 局面のキーでソートし、同じ局面（手数違いを含む）のブロックをまとめる
  //    同じ指し手が重複している場合は、後に出てきたもので置き換え、採択回数は合算する（やねうら王と同じ）
  std::sort(blocks.begin(), blocks.end(), [](const Block& a, const Block& b) {
    const int c = std::memcmp(a.key.data, b.key.data, sizeof(a.key.data));
    return c != 0 ? c < 0 : a.seq < b.seq;
  });
  index_.clear();
  moves_.clear();
  index_.reserve(blocks.size());
  moves_.reserve(raw_moves.size());
  has_depth_ = false;
  for (size_t i = 0; i < blocks.size();) {
    size_t j = i;
    IndexEntry entry;
    entry.key = blocks[i].key;
    entry.first = uint32_t(moves_.size());
    for (; j < blocks.size() && blocks[j].key == blocks[i].key; ++j) {
      for (uint32_t k = 0; k < blocks[j].count; ++k) {
        const YaneBookMove& bm = raw_moves[blocks[j].first + k];
        auto it = std::find_if(moves_.begin() + entry.first, moves_.end(),
                               [&](const YaneBookMove& m) { return m.move == bm.move; });
        if (it != moves_.end()) {
          const uint64_t count = it->move_count;
          *it = bm;
          it->move_count += count;
        } else {
          moves_.push_back(bm);
        }
        has_depth_ |= bm.depth != 0;
      }
    }
    entry.count = uint32_t(moves_.size() - entry.first);
    if (entry.count > 0) {
      index_.push_back(entry);
    }
    i = j;
  }

  if (index_.empty()) {
    SYNCED_PRINTF("info string Error! : No positions found in %s.\n", file_name.c_str());
    return false;
  }
  return true;
}

bool YaneuraOuBook::LoadYbb(const std::string& file_name, bool on_the_fly) {
  std::FILE* fp = std::fopen(file_name.c_str(), "rb");
  if (fp == nullptr) {
    SYNCED_PRINTF("info string Failed to Open %s.\n", file_name.c_str());
    return false;
  }
  struct stat st;
  if (::stat(file_name.c_str(), &st) != 0) {
    std::fclose(fp);
    return false;
  }
  const uint64_t file_size = uint64_t(st.st_size);

  // 1. ヘッダ
  uint8_t header[kYbbHeaderSize];
  if (std::fread(header, 1, sizeof(header), fp) != sizeof(header)
      || std::memcmp(header, kYbbMagic, 16) != 0) {
    SYNCED_PRINTF("info string Error! : invalid ybb file (bad header) : %s\n", file_name.c_str());
    std::fclose(fp);
    return false;
  }
  const uint64_t record_count = ReadU64(header + 16);
  const uint64_t flags = ReadU64(header + 24);
  if ((flags & ~kYbbFlagMoveDepth) != 0
      || record_count > (file_size - kYbbHeaderSize) / kYbbIndexRecordSize) {
    SYNCED_PRINTF("info string Error! : invalid ybb file (bad flags or record count) : %s\n",
                  file_name.c_str());
    std::fclose(fp);
    return false;
  }
  ybb_record_count_ = record_count;
  ybb_flags_ = flags;
  ybb_moves_base_ = kYbbHeaderSize + record_count * kYbbIndexRecordSize;
  has_depth_ = (flags & kYbbFlagMoveDepth) != 0;

  // 2. on the fly ならファイルを開いたままにする
  if (on_the_fly) {
    ybb_file_ = fp;
    return true;
  }

  // 3. メモリに全部読み込み、各レコードの指し手の範囲がファイル内に収まっていることを確認する
  ybb_data_.resize(size_t(file_size));
  if (SeekFile(fp, 0) != 0
      || std::fread(ybb_data_.data(), 1, ybb_data_.size(), fp) != ybb_data_.size()) {
    SYNCED_PRINTF("info string Error! : failed to read %s\n", file_name.c_str());
    std::fclose(fp);
    return false;
  }
  std::fclose(fp);

  const uint64_t move_record_size = YbbMoveRecordSize(flags);
  const uint64_t moves_area = file_size - ybb_moves_base_;
  for (uint64_t i = 0; i < record_count; ++i) {
    const uint8_t* e = ybb_data_.data() + kYbbHeaderSize + i * kYbbIndexRecordSize;
    const uint64_t offset = ReadU64(e + 32);
    const uint64_t count = ReadU16(e + 42);
    if (offset > moves_area || count * move_record_size > moves_area - offset) {
      SYNCED_PRINTF("info string Error! : invalid ybb file (record %llu is out of range) : %s\n",
                    (unsigned long long)i, file_name.c_str());
      return false;
    }
  }
  return true;
}

bool YaneuraOuBook::FindInYbb(const PackedSfen& key, std::vector<YaneBookMove>* moves) {
  const uint64_t move_record_size = YbbMoveRecordSize(ybb_flags_);
  uint64_t left = 0, right = ybb_record_count_;
  uint8_t entry[kYbbIndexRecordSize];
  while (left < right) {
    const uint64_t middle = left + (right - left) / 2;
    const uint64_t pos = kYbbHeaderSize + middle * kYbbIndexRecordSize;
    const uint8_t* e;
    if (ybb_file_ != nullptr) {
      if (SeekFile(ybb_file_, pos) != 0 || std::fread(entry, 1, sizeof(entry), ybb_file_) != sizeof(entry)) {
        SYNCED_PRINTF("info string Error! : failed to read the ybb book.\n");
        return false;
      }
      e = entry;
    } else {
      e = ybb_data_.data() + pos;
    }

    const int c = std::memcmp(key.data, e, 32);
    if (c < 0) {
      right = middle;
    } else if (c > 0) {
      left = middle + 1;
    } else {
      // 見つかった（手数は無視する）
      const uint64_t offset = ReadU64(e + 32);
      const uint16_t count = ReadU16(e + 42);
      const uint64_t size = count * move_record_size;
      if (ybb_file_ != nullptr) {
        std::vector<uint8_t> buf(size_t(size) + 1);
        if (SeekFile(ybb_file_, ybb_moves_base_ + offset) != 0
            || std::fread(buf.data(), 1, size_t(size), ybb_file_) != size_t(size)) {
          SYNCED_PRINTF("info string Error! : failed to read the moves in the ybb book.\n");
          return false;
        }
        ParseYbbMoves(buf.data(), count, ybb_flags_, moves);
      } else {
        ParseYbbMoves(ybb_data_.data() + ybb_moves_base_ + offset, count, ybb_flags_, moves);
      }
      return true;
    }
  }
  return false;
}

bool YaneuraOuBook::Find(const PackedSfen& key, std::vector<YaneBookMove>* moves) {
  moves->clear();
  if (!loaded_) return false;
  if (format_ == kYbbFormat) {
    return FindInYbb(key, moves);
  }
  auto it = std::lower_bound(index_.begin(), index_.end(), key,
                             [](const IndexEntry& e, const PackedSfen& k) { return e.key < k; });
  if (it == index_.end() || it->key != key) {
    return false;
  }
  moves->assign(moves_.begin() + it->first, moves_.begin() + it->first + it->count);
  return true;
}

void YaneuraOuBook::SortMoves(std::vector<YaneBookMove>* moves) {
  std::stable_sort(moves->begin(), moves->end(), [](const YaneBookMove& a, const YaneBookMove& b) {
    return a.move_count != b.move_count ? a.move_count > b.move_count : a.value > b.value;
  });
}

bool YaneuraOuBook::Probe(const Position& pos, bool flipped_book,
                          std::vector<YaneBookMove>* moves, bool* hit_flipped) {
  if (hit_flipped != nullptr) *hit_flipped = false;
  moves->clear();
  if (!loaded_) return false;

  const RawPosition raw = RawPosition::FromPosition(pos);
  PackedSfen key;
  if (!SfenPacker::Pack(raw, &key)) return false;
  bool found = Find(key, moves);

  // 先後反転した局面にヒットするか調べる（ペタショック定跡など、片方の手番の局面だけを持つ定跡のため）
  if (!found && flipped_book) {
    PackedSfen flipped_key;
    if (SfenPacker::Pack(raw.Flipped(), &flipped_key) && Find(flipped_key, moves)) {
      for (YaneBookMove& bm : *moves) {
        bm.move = YaneMove16::Flip(bm.move);
        bm.ponder = YaneMove16::Flip(bm.ponder);
        // 評価値は手番側から見た値なので、反転しても符号は変わらない
      }
      found = true;
      if (hit_flipped != nullptr) *hit_flipped = true;
    }
  }

  if (found) {
    SortMoves(moves);
  }
  return found && !moves->empty();
}

Move YaneuraOuBook::GetOneBookMove(const Position& root, const UsiOptions& usi_options,
                                   Move* const ponder_move) {
  *ponder_move = kMoveNone;
  if (!loaded_) return kMoveNone;

  // 1. 一定の確率で定跡を無視する（BookIgnoreRate）
  const int ignore_rate = usi_options["BookIgnoreRate"];
  if (ignore_rate > 0 && ignore_rate > int(std::uniform_int_distribution<int>(0, 99)(random_))) {
    return kMoveNone;
  }

  // 2. 定跡を探す
  const bool flipped_book = usi_options["FlippedBook"];
  std::vector<YaneBookMove> book_moves;
  if (!Probe(root, flipped_book, &book_moves)) {
    return kMoveNone;
  }

  // 3. 非合法手を取り除く
  struct Candidate {
    YaneBookMove bm;
    Move move;
    Move ponder;
  };
  std::vector<Candidate> candidates;
  const std::string sfen = RawPosition::FromPosition(root).ToSfen(root.game_ply() + 1);
  for (const YaneBookMove& bm : book_moves) {
    const std::string usi = YaneMove16::ToUsi(bm.move);
    const Move move = usi.empty() ? kMoveNone : FindLegalMoveFromUsi(root, usi);
    if (move == kMoveNone) {
      SYNCED_PRINTF("info string Error! : Illegal Move In Book DB : move = %s , sfen = %s\n",
                    usi.empty() ? "?" : usi.c_str(), sfen.c_str());
      continue;
    }
    Move ponder = kMoveNone;
    const std::string ponder_usi = YaneMove16::ToUsi(bm.ponder);
    if (!ponder_usi.empty()) {
      Position next = root;
      next.MakeMove(move);
      ponder = FindLegalMoveFromUsi(next, ponder_usi);
    }
    candidates.push_back(Candidate{bm, move, ponder});
  }
  if (candidates.empty()) {
    return kMoveNone;
  }

  // 4. 候補手を出力する（multipv の順 = 採択回数の多い順 → 評価値の高い順）
  uint64_t move_count_total = 0;
  for (const Candidate& c : candidates) move_count_total += c.bm.move_count;
  const bool has_move_count = move_count_total != 0;
  for (size_t i = 0; i < candidates.size(); ++i) {
    const Candidate& c = candidates[i];
    SYNCED_PRINTF("info time 0 depth %d nodes 0 score cp %d multipv %d pv %s%s%s\n",
                  int(c.bm.depth), int(c.bm.value), int(i + 1), c.move.ToSfen().c_str(),
                  c.ponder != kMoveNone ? " " : "", MoveToString(c.ponder).c_str());
  }

  const std::vector<Candidate> printed_candidates = candidates;

  // 5. NarrowBook（採択率10%未満の手を除く）
  if (usi_options["NarrowBook"] && has_move_count) {
    const size_t n = candidates.size();
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](const Candidate& c) {
      return double(c.bm.move_count) / double(move_count_total) < 0.1;
    }), candidates.end());
    if (n != candidates.size()) {
      SYNCED_PRINTF("info string NarrowBook : %zu moves to %zu moves.\n", n, candidates.size());
    }
  }
  if (candidates.empty()) {
    return kMoveNone;
  }

  // 6. BookDepthLimit（最善手の depth が下限未満なら、この局面では定跡を使わない）
  const int depth_limit = usi_options["BookDepthLimit"];
  if (has_depth_ && depth_limit != 0 && candidates[0].bm.depth < depth_limit) {
    SYNCED_PRINTF("info string BookDepthLimit is lower than the depth of this node.\n");
    return kMoveNone;
  }

  // 7. BookEvalDiff / BookEvalBlackLimit / BookEvalWhiteLimit
  {
    const int eval_diff = usi_options["BookEvalDiff"];
    const bool black = root.side_to_move() == kBlack;
    const char* limit_name = black ? "BookEvalBlackLimit" : "BookEvalWhiteLimit";
    const int value_limit2 = usi_options[limit_name];
    const int64_t value_limit1 = int64_t(candidates[0].bm.value) - eval_diff;
    const int64_t value_limit = std::max<int64_t>(value_limit1, value_limit2);
    const size_t n = candidates.size();
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](const Candidate& c) {
      return c.bm.value < value_limit;
    }), candidates.end());
    if (n != candidates.size()) {
      SYNCED_PRINTF("info string BookEvalDiff = %d , %s = %d , %zu moves to %zu moves.\n",
                    eval_diff, limit_name, value_limit2, n, candidates.size());
    }
  }
  if (candidates.empty()) {
    return kMoveNone;
  }

  // 8. 抽選（一様に選ぶ。ConsiderBookMoveCount なら採択回数に比例して選ぶ）
  size_t chosen = std::uniform_int_distribution<size_t>(0, candidates.size() - 1)(random_);
  if (usi_options["ConsiderBookMoveCount"]) {
    uint64_t sum = 0;
    for (const Candidate& c : candidates) sum += c.bm.move_count;
    uint64_t sum_move_counts = 0;
    for (size_t i = 0; i < candidates.size(); ++i) {
      const uint64_t move_count = (sum == 0) ? 1 : candidates[i].bm.move_count;
      sum_move_counts += move_count;
      if (sum_move_counts != 0
          && std::uniform_int_distribution<uint64_t>(0, sum_move_counts - 1)(random_) < move_count) {
        chosen = i;
      }
    }
  }
  const Candidate& best = candidates[chosen];

  // 9. 予想手: 登録されていなければ、指したあとの局面の定跡の先頭の手
  Move ponder = best.ponder;
  if (ponder == kMoveNone) {
    Position next = root;
    next.MakeMove(best.move);
    std::vector<YaneBookMove> next_moves;
    if (Probe(next, flipped_book, &next_moves)) {
      const std::string usi = YaneMove16::ToUsi(next_moves[0].move);
      if (!usi.empty()) ponder = FindLegalMoveFromUsi(next, usi);
    }
  }
  *ponder_move = ponder;

  // 10. 選んだ手を、もう一度出力する（GUI がこの手の評価値を表示するように）
  size_t multipv = 1;
  for (size_t i = 0; i < printed_candidates.size(); ++i) {
    if (printed_candidates[i].move == best.move) multipv = i + 1;
  }
  SYNCED_PRINTF("info time 0 depth %d nodes 0 score cp %d multipv %d pv %s%s%s\n",
                int(best.bm.depth), int(best.bm.value), int(multipv), best.move.ToSfen().c_str(),
                ponder != kMoveNone ? " " : "", MoveToString(ponder).c_str());

  return best.move;
}
