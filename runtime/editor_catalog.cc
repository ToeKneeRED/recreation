#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

#include "bethesda/load_order.h"
#include "bethesda/record.h"
#include "bethesda/strings.h"
#include "core/log.h"
#include "editor.h"
#include "engine_context.h"

namespace rec {
namespace {

constexpr int kCatalogCap = 9000;  // total entries (keeps the first toggle snappy)
constexpr int kPerTypeCap = 2500;  // per record type, so "All" stays varied

// The placeable base record types, with the browser category each maps to. Only
// types CellStreamer::PlaceObject can resolve a world model for are listed, so
// every catalog entry is actually droppable.
struct TypeBucket {
  u32 type;
  int category;
};
const TypeBucket kPlaceableTypes[] = {
    {FourCc('S', 'T', 'A', 'T'), 1}, {FourCc('M', 'S', 'T', 'T'), 1},
    {FourCc('F', 'U', 'R', 'N'), 2}, {FourCc('D', 'O', 'O', 'R'), 3},
    {FourCc('C', 'O', 'N', 'T'), 4}, {FourCc('F', 'L', 'O', 'R'), 5},
    {FourCc('T', 'R', 'E', 'E'), 5}, {FourCc('L', 'I', 'G', 'H'), 6},
    {FourCc('M', 'I', 'S', 'C'), 7}, {FourCc('B', 'O', 'O', 'K'), 7},
    {FourCc('I', 'N', 'G', 'R'), 7}, {FourCc('A', 'L', 'C', 'H'), 7},
    {FourCc('S', 'L', 'G', 'M'), 7}, {FourCc('K', 'E', 'Y', 'M'), 7},
    {FourCc('W', 'E', 'A', 'P'), 7}, {FourCc('A', 'M', 'M', 'O'), 7},
    {FourCc('A', 'C', 'T', 'I'), 8},
};

// The displayed name: the localized FULL string, falling back to the editor id.
std::string DisplayName(const bethesda::Record& record, const bethesda::StringTable& strings,
                        const std::string& editor_id) {
  const bethesda::Subrecord* full = record.Find(FourCc('F', 'U', 'L', 'L'));
  if (full) {
    if (full->data.size() >= 4) {
      u32 string_id;
      std::memcpy(&string_id, full->data.data(), 4);
      if (const base::String* s = strings.Find(string_id))
        if (s->size() > 0) return std::string(s->c_str());
    }
    std::string literal = record.GetString(FourCc('F', 'U', 'L', 'L'));
    if (!literal.empty()) return literal;
  }
  return editor_id;
}

std::string Lower(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

}  // namespace

void MapEditor::BuildCatalog() {
  catalog_built_ = true;
  catalog_.clear();
  if (!ctx_.records || !ctx_.strings) return;
  const bethesda::RecordStore& records = *ctx_.records;
  const bethesda::StringTable& strings = *ctx_.strings;

  const u32 kEdid = FourCc('E', 'D', 'I', 'D');
  const u32 kModl = FourCc('M', 'O', 'D', 'L');
  for (const TypeBucket& tb : kPlaceableTypes) {
    if (static_cast<int>(catalog_.size()) >= kCatalogCap) break;
    int taken = 0;
    records.EachOfType(
        tb.type, [&](bethesda::GlobalFormId id, const bethesda::RecordStore::StoredRecord&) {
          if (taken >= kPerTypeCap || static_cast<int>(catalog_.size()) >= kCatalogCap) return;
          bethesda::Record record;
          if (!records.Parse(id, &record)) return;
          // Only forms with a world model are droppable.
          const bethesda::Subrecord* modl = record.Find(kModl);
          if (!modl || modl->data.empty()) return;
          CatalogEntry e;
          e.base = id;
          e.type = tb.type;
          e.category = tb.category;
          e.editor_id = record.GetString(kEdid);
          e.name = DisplayName(record, strings, e.editor_id);
          if (e.name.empty()) return;  // nameless and idless: not useful to browse
          catalog_.push_back(std::move(e));
          ++taken;
        });
  }

  // Group by category then name so the browser reads tidily; the filter keeps
  // these in order.
  std::sort(catalog_.begin(), catalog_.end(), [](const CatalogEntry& a, const CatalogEntry& b) {
    if (a.category != b.category) return a.category < b.category;
    return Lower(a.name) < Lower(b.name);
  });
  REC_INFO("editor catalog: {} placeable forms across {} types", catalog_.size(),
           sizeof(kPlaceableTypes) / sizeof(kPlaceableTypes[0]));
  RefreshFilter();
}

void MapEditor::RefreshFilter() {
  filtered_.clear();
  const std::string needle = Lower(search_);
  for (int i = 0; i < static_cast<int>(catalog_.size()); ++i) {
    const CatalogEntry& e = catalog_[i];
    if (category_ != 0 && e.category != category_) continue;
    if (!needle.empty()) {
      if (Lower(e.name).find(needle) == std::string::npos &&
          Lower(e.editor_id).find(needle) == std::string::npos) {
        continue;
      }
    }
    filtered_.push_back(i);
  }
  if (page_first_ >= static_cast<int>(filtered_.size())) page_first_ = 0;
}

}  // namespace rec
