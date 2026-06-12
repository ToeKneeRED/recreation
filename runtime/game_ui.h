#ifndef RECREATION_RUNTIME_GAME_UI_H_
#define RECREATION_RUNTIME_GAME_UI_H_

#include <memory>

#include "core/types.h"

namespace rec {

class Window;
class FlyCamera;
namespace render {
class Renderer;
struct FrameView;
}  // namespace render

// libultragui-driven HUD and pause menu. Runs ultragui in draw-data mode and
// records its draw list into the renderer's ui pass, alongside the debug ImGui
// overlay. Compiles to a stub when ultragui is unavailable (RECREATION_HAS_UGUI
// off, e.g. the dedicated server build).
class GameUi {
 public:
  GameUi();
  ~GameUi();

  GameUi(const GameUi&) = delete;
  GameUi& operator=(const GameUi&) = delete;

  bool Initialize(Window& window, render::Renderer& renderer);
  // Call between renderer WaitIdle and renderer Shutdown.
  void Shutdown();

  // Feed input, drive HUD values from engine state, produce the draw list and
  // fill view->hud_draw. Call once per frame after PumpEvents.
  void Build(Window& window, render::Renderer& renderer, FlyCamera& camera, f32 frame_delta,
             render::FrameView* view);

  void ToggleMenu();
  bool menu_open() const;
  bool quit_requested() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace rec

#endif  // RECREATION_RUNTIME_GAME_UI_H_
