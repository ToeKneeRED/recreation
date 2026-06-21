#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "core/log.h"
#include "ecs/world.h"
#include "editor.h"
#include "engine_context.h"
#include "world/cell_streaming.h"

namespace rec {
namespace {

constexpr f32 kUnitsToMeters = 0.01428f;  // mirrors CellStreamer (unit -> metre)

// Where the layout lives: REC_EDITOR_LAYOUT, else a file in the working dir.
std::string DefaultLayoutPath() {
  if (const char* env = std::getenv("REC_EDITOR_LAYOUT")) return env;
  return "editor_layout.reclayout";
}

}  // namespace

int MapEditor::SaveLayout() {
  if (layout_path_.empty()) layout_path_ = DefaultLayoutPath();
  std::ofstream out(layout_path_, std::ios::trunc);
  if (!out) {
    SetStatus("Save failed: " + layout_path_);
    REC_WARN("editor: cannot open {} for writing", layout_path_);
    return 0;
  }
  out << "# recreation map layout v1\n";
  out << "# place <plugin> <local_id> <px py pz> <qx qy qz qw> <scale>\n";
  int n = 0;
  for (const PlacedObject& p : placed_) {
    if (!ctx_.world->IsAlive(p.entity)) continue;
    const world::Transform* t = ctx_.world->Get<world::Transform>(p.entity);
    if (!t) continue;
    const f32 user_scale = t->scale / kUnitsToMeters;
    out << "place " << p.base.plugin << ' ' << p.base.local_id << ' ' << t->position[0] << ' '
        << t->position[1] << ' ' << t->position[2] << ' ' << t->rotation[0] << ' ' << t->rotation[1]
        << ' ' << t->rotation[2] << ' ' << t->rotation[3] << ' ' << user_scale << '\n';
    ++n;
  }
  SetStatus("Saved " + std::to_string(n) + " objects to " + layout_path_);
  REC_INFO("editor: saved {} objects to {}", n, layout_path_);
  return n;
}

int MapEditor::LoadLayout() {
  if (layout_path_.empty()) layout_path_ = DefaultLayoutPath();
  std::ifstream in(layout_path_);
  if (!in) return 0;  // no layout saved yet: a silent no-op
  if (!ctx_.streamer || !ctx_.world) return 0;

  std::string line;
  int n = 0;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ss(line);
    std::string tag;
    ss >> tag;
    if (tag != "place") continue;
    unsigned plugin = 0, local = 0;
    f32 px, py, pz, qx, qy, qz, qw, scale;
    if (!(ss >> plugin >> local >> px >> py >> pz >> qx >> qy >> qz >> qw >> scale)) continue;
    const bethesda::GlobalFormId base{static_cast<u16>(plugin), static_cast<u32>(local)};
    const f32 rot[4] = {qx, qy, qz, qw};
    ecs::Entity e = ctx_.streamer->PlaceObject(*ctx_.world, base, Vec3{px, py, pz}, rot, scale);
    if (e == ecs::kInvalidEntity) continue;
    // Recover a display name from the catalog when it is built.
    std::string name = "object";
    for (const CatalogEntry& c : catalog_) {
      if (c.base.plugin == base.plugin && c.base.local_id == base.local_id) {
        name = c.name;
        break;
      }
    }
    placed_.push_back({e, base, std::move(name)});
    ++n;
  }
  if (n > 0) {
    SetStatus("Loaded " + std::to_string(n) + " saved objects");
    REC_INFO("editor: loaded {} objects from {}", n, layout_path_);
  }
  return n;
}

}  // namespace rec
