#include "fly_camera.h"

#include <algorithm>
#include <cmath>

namespace rec {

Vec3 FlyCamera::forward() const {
  return {std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_), -std::cos(pitch_) * std::cos(yaw_)};
}

void FlyCamera::Update(const InputState& input, bool allow_mouse, bool allow_keyboard, f32 dt) {
  looking_ = allow_mouse && input.button(MouseButton::kRight);

  if (looking_) {
    yaw_ += input.mouse_dx * sensitivity;
    pitch_ -= input.mouse_dy * sensitivity;
    pitch_ = std::clamp(pitch_, -1.55f, 1.55f);
    if (input.wheel != 0) {
      speed *= std::pow(1.2f, input.wheel);
      speed = std::clamp(speed, 0.1f, 200.0f);
    }
  }

  if (!allow_keyboard) return;

  Vec3 fwd = forward();
  Vec3 right = Normalize(Cross(fwd, {0, 1, 0}));
  Vec3 move{};
  if (input.key(Key::kW)) move += fwd;
  if (input.key(Key::kS)) move += fwd * -1.0f;
  if (input.key(Key::kD)) move += right;
  if (input.key(Key::kA)) move += right * -1.0f;
  if (input.key(Key::kE) || input.key(Key::kSpace)) move += Vec3{0, 1, 0};
  if (input.key(Key::kQ) || input.key(Key::kLeftCtrl)) move += Vec3{0, -1, 0};

  f32 length = std::sqrt(Dot(move, move));
  if (length > 0) {
    f32 boost = input.key(Key::kLeftShift) ? 4.0f : 1.0f;
    position_ += move * (speed * boost * dt / length);
  }
}

}  // namespace rec
