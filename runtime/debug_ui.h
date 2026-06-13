#ifndef RECREATION_RUNTIME_DEBUG_UI_H_
#define RECREATION_RUNTIME_DEBUG_UI_H_

#include "core/types.h"
#include "core/window.h"
#include "render/renderer.h"

namespace rec {

class FlyCamera;

// Dear ImGui overlay: frame stats plus live toggles for every render
// feature. Rendered through the renderer's ui pass straight onto the
// backbuffer. Compiles to a stub when imgui/SDL3 are unavailable.
class DebugUi {
 public:
  DebugUi();
  ~DebugUi();

  DebugUi(const DebugUi&) = delete;
  DebugUi& operator=(const DebugUi&) = delete;

  bool Initialize(Window& window, render::Renderer& renderer);
  // Call between renderer WaitIdle and renderer Shutdown.
  void Shutdown();

  // Starts the imgui frame; call once per frame after PumpEvents.
  void BeginFrame();
  // Builds the panels and fills view->ui_draw. Always pairs with a
  // BeginFrame, even while hidden.
  void Build(render::Renderer& renderer, FlyCamera& camera, f32 frame_delta,
             render::FrameView* view);

  void ToggleVisible() { visible_ = !visible_; }
  bool wants_mouse() const;
  bool wants_keyboard() const;

 private:
  bool initialized_ = false;
  bool visible_ = true;
  bool show_demo_ = false;
  VkFormat swapchain_format_ = VK_FORMAT_UNDEFINED;  // outlives imgui init info
  f32 frame_times_[150] = {};
  u32 frame_time_cursor_ = 0;
};

}  // namespace rec

#endif  // RECREATION_RUNTIME_DEBUG_UI_H_
