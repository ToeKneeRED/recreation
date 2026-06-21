#ifndef RECREATION_RUNTIME_EDITOR_H_
#define RECREATION_RUNTIME_EDITOR_H_

#include <string>
#include <vector>

#include "bethesda/form_id.h"
#include "core/math.h"
#include "core/types.h"
#include "ecs/entity.h"
#include "game_ui.h"
#include "world/components.h"

namespace rec {

struct EngineContext;
struct InputState;

// Asset browser category tabs. Index 0 ("All") aggregates the rest; each catalog
// entry stores its specific bucket (1..N). Shared by the catalog builder and the
// view so the tab order never drifts.
inline constexpr const char* kEditorCategories[] = {
    "All",   "Statics", "Furniture", "Doors",      "Containers",
    "Flora", "Lights",  "Clutter",   "Activators",
};
inline constexpr int kEditorCategoryCount =
    sizeof(kEditorCategories) / sizeof(kEditorCategories[0]);

// A live, in-game map editor: a Creation Kit that runs inside the streamed
// world. Toggle it on (F4) and the camera becomes a free-fly builder; an asset
// browser lists every placeable base form pulled from the loaded game files,
// and a click drops the chosen asset onto the terrain. Selected objects can be
// moved, rotated, scaled and deleted, with an undo stack. All the logic lives
// here; the UI is mirrored through GameUi (EditorView) and drives back through
// a single event sink, so the renderer and ECS need no editor-specific code.
//
// The editor operates on the primary content domain (ctx.streamer / ctx.records
// / ctx.assets). It is a windowed-client feature; a headless build never
// constructs one.
class MapEditor {
 public:
  explicit MapEditor(EngineContext& ctx);

  bool active() const { return active_; }

  // True while the asset search box is focused, so the engine routes the
  // keyboard to text entry instead of flying the camera (mirrors the debug
  // overlay's wants_keyboard()).
  bool wants_keyboard() const { return active_ && search_focused_; }

  // Receives a click/scroll on an editor widget (registered with GameUi as the
  // editor event sink). Public so the GameUi callback can reach it.
  void HandleUiEvent(const EditorUiEvent& e);

  // Enter or leave editor mode. Entering drops walk mode, frees the cursor and
  // arms the overlay; leaving clears the selection and the armed brush.
  void Toggle();

  // Per-frame editor tick (only meaningful while active). `allow_input` is false
  // when the pause menu owns input. Reads the frame's input, applies edits, and
  // pushes the rebuilt EditorView into the HUD.
  void Update(const InputState& input, f32 dt, bool allow_input);

 private:
  // One placeable base form discovered in the load order.
  struct CatalogEntry {
    bethesda::GlobalFormId base;
    std::string name;       // displayed FULL name, falling back to the editor id
    std::string editor_id;  // EDID
    u32 type = 0;           // record fourcc (STAT, TREE, ...)
    int category = 0;       // index into the category tabs
  };

  // An object the editor placed (so it can be counted, re-selected and, later,
  // serialized). Streamed refs the editor edits are tracked only transiently.
  struct PlacedObject {
    ecs::Entity entity;
    bethesda::GlobalFormId base;
    std::string name;
  };

  // The inverse of one edit, for the undo stack.
  enum class UndoKind { kPlace, kDelete, kTransform };
  struct UndoOp {
    UndoKind kind;
    ecs::Entity entity;           // kPlace/kTransform target
    bethesda::GlobalFormId base;  // kDelete/kPlace base form
    world::Transform transform;   // kTransform: prior transform; kDelete: where it was
    std::string name;
  };

  // --- catalog (editor_catalog.cc) ---
  void BuildCatalog();
  void RefreshFilter();  // recompute filtered_ from search_ + category_

  // --- input / ops (editor.cc) ---
  void UpdateSearchInput(const InputState& input);  // text entry while focused
  void ApplyKeyboard(const InputState& input);
  void ArmBrush(int catalog_index);
  void PlaceBrush(const InputState& input);
  void SelectUnderCursor(const InputState& input);
  void DeleteSelection();
  void DuplicateSelection();
  void RotateSelection(f32 radians);
  void ScaleSelection(f32 factor);
  void Undo();
  void RecordTransform(ecs::Entity entity);  // push a kTransform undo snapshot
  void SetStatus(std::string message);
  void PushView();

  // --- picking / placement geometry ---
  bool PointerOverUi(const InputState& input) const;
  Vec3 CursorRayDir(const InputState& input) const;
  bool AimPoint(const InputState& input, Vec3* out) const;            // ray vs ground
  ecs::Entity PickEntity(const InputState& input, f32* out_t) const;  // ray vs spheres
  world::Transform* SelectedTransform();

  EngineContext& ctx_;
  bool active_ = false;

  // Catalog + browser filter state.
  std::vector<CatalogEntry> catalog_;
  std::vector<int> filtered_;  // indices into catalog_ matching search_/category_
  bool catalog_built_ = false;
  int category_ = 0;
  std::string search_;
  int page_first_ = 0;           // scroll offset into filtered_
  int brush_ = -1;               // catalog_ index of the armed asset, or -1
  bool search_focused_ = false;  // search box has keyboard focus

  // Selection + active tool.
  ecs::Entity selection_ = ecs::kInvalidEntity;
  int tool_ = 0;                  // 0 select, 1 move, 2 rotate, 3 scale
  bool moving_ = false;           // G-grab gesture: selection follows the aim point
  world::Transform move_origin_;  // restore target if the move is cancelled
  bool prev_lmb_ = false;         // left-button edge detection (clicks, not holds)

  std::vector<PlacedObject> placed_;
  std::vector<UndoOp> undo_;

  std::string status_;
  f32 status_age_ = 0;
  u32 next_synth_id_ = 1;  // local id for placed objects' synthetic form links
};

}  // namespace rec

#endif  // RECREATION_RUNTIME_EDITOR_H_
