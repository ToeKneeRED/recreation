#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>

#include "bethesda/load_order.h"
#include "bethesda/plugin.h"

namespace {

using namespace rec::bethesda;

constexpr rec::u32 kEdid = rec::FourCc('E', 'D', 'I', 'D');
constexpr rec::u32 kName = rec::FourCc('N', 'A', 'M', 'E');
constexpr rec::u32 kData = rec::FourCc('D', 'A', 'T', 'A');
constexpr rec::u32 kModl = rec::FourCc('M', 'O', 'D', 'L');

// Lists the refs of one exterior cell with their base record info, for
// chasing down "what is that thing on screen" questions.
int DumpCellRefs(const std::string& data_dir, int x, int y) {
  const auto& profile = GameProfile::For(GameProfile::DetectFromDataDir(data_dir));
  auto order = LoadOrder::FromPluginsTxt(data_dir + "/../plugins.txt", profile);
  RecordStore records;
  if (!records.LoadAll(data_dir, order, profile)) return 1;

  GlobalFormId world = records.FindWorldspace(profile.exterior_worldspace);
  const RecordStore::ExteriorGrid* grid = records.ExteriorCells(world);
  if (!grid) return 1;
  const RecordStore::ExteriorCell* cell =
      grid->find(RecordStore::GridKey(static_cast<rec::i16>(x), static_cast<rec::i16>(y)));
  if (!cell) {
    std::printf("no cell at %d,%d\n", x, y);
    return 1;
  }
  std::printf("cell %d,%d: %zu refs\n", x, y, static_cast<size_t>(cell->refs.size()));
  for (rec::u64 packed : cell->refs) {
    GlobalFormId id{static_cast<rec::u16>(packed >> 32), static_cast<rec::u32>(packed)};
    Record refr;
    if (!records.Parse(id, &refr)) continue;
    const Subrecord* name = refr.Find(kName);
    const Subrecord* data = refr.Find(kData);
    if (!name || name->data.size() < 4) continue;
    rec::u32 base_raw;
    std::memcpy(&base_raw, name->data.data(), 4);
    GlobalFormId base_id = records.ResolveFrom(RawFormId{base_raw}, records.Find(id)->winning_plugin);
    const RecordStore::StoredRecord* base_stored = records.Find(base_id);
    char type[5] = {};
    if (base_stored) std::memcpy(type, &base_stored->header.type, 4);
    Record base;
    std::string edid, model;
    if (base_stored && records.Parse(base_id, &base)) {
      edid = base.GetString(kEdid);
      model = base.GetString(kModl);
    }
    float pos[3] = {};
    if (data && data->data.size() >= 12) std::memcpy(pos, data->data.data(), 12);
    std::printf("  %04x:%06x %s %-32s (%.0f,%.0f,%.0f) %s\n", id.plugin, id.local_id, type,
                edid.c_str(), pos[0], pos[1], pos[2], model.c_str());
  }
  return 0;
}

}  // namespace

// Dumps header info and record counts of a plugin. Handy for checking the
// parser against real game files. With a data directory and a cell
// coordinate it lists that exterior cell's refs instead.
int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("usage: esminfo <plugin.esm> [game: skyrimse|fo4|fo76]\n");
    std::printf("       esminfo <data-dir> cell <x,y>\n");
    return 1;
  }

  if (argc >= 4 && std::string(argv[2]) == "cell") {
    std::string coords = argv[3];
    size_t comma = coords.find(',');
    if (comma == std::string::npos) return 1;
    return DumpCellRefs(argv[1], std::stoi(coords.substr(0, comma)),
                        std::stoi(coords.substr(comma + 1)));
  }

  Game game = Game::kSkyrimSe;
  if (argc > 2) {
    std::string id = argv[2];
    if (id == "fo4") game = Game::kFallout4;
    if (id == "fo76") game = Game::kFallout76;
  }

  auto plugin = PluginFile::Open(argv[1], GameProfile::For(game));
  if (!plugin) return 1;

  std::printf("%s\n", plugin->file_name().c_str());
  std::printf("  version: %.2f\n", plugin->version());
  std::printf("  master:  %s\n", plugin->is_master() ? "yes" : "no");
  std::printf("  light:   %s\n", plugin->is_light() ? "yes" : "no");
  std::printf("  records: %u (header)\n", plugin->record_count());
  for (const auto& master : plugin->masters()) {
    std::printf("  requires %s\n", master.c_str());
  }

  std::map<std::string, int> counts;
  plugin->VisitRecords([&](Record& record) {
    char type[5] = {};
    std::memcpy(type, &record.header.type, 4);
    ++counts[type];
  });
  for (const auto& [type, count] : counts) {
    std::printf("  %s x%d\n", type.c_str(), count);
  }
  return 0;
}
