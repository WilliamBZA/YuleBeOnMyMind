// Two-layer compositor: a base layer (the playing pattern or a status indication) and a transient overlay
// (feedback cues). The base timeline keeps advancing underneath an overlay. The output stage applies the
// 1 s fade-out and the owner's brightness. Hardware independent; the LED driver only copies the result.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "animation.h"
#include "yule_limits.h"

namespace yule {

class Compositor {
 public:
  // Brightness in percent, clamped to 5..100. It scales every channel equally, so colours keep their hue.
  void setBrightness(uint8_t percent);
  uint8_t brightness() const { return brightness_; }

  // Replaces the base layer. nullptr means idle (all LEDs off). Cancels any fade-out in progress.
  void setBase(std::shared_ptr<Animation> animation, uint32_t nowMs);
  bool hasBase() const { return base_ != nullptr; }

  // Fades the base layer to black over limits::kFadeOutMs, then clears it (hasBase() becomes false).
  void startFadeOut(uint32_t nowMs);
  bool fading() const { return fading_; }

  // Shows a cue over everything. It replaces any overlay already showing and expires after its
  // durationMs(); a duration of 0 stays until clearOverlay().
  void setOverlay(std::shared_ptr<Animation> animation, uint32_t nowMs);
  void clearOverlay();
  bool hasOverlay() const { return overlay_ != nullptr; }

  // Renders one frame of n pixels at nowMs.
  void render(uint32_t nowMs, Rgb* out, size_t n);

 private:
  std::shared_ptr<Animation> base_;
  uint32_t baseStartMs_ = 0;
  bool fading_ = false;
  uint32_t fadeStartMs_ = 0;

  std::shared_ptr<Animation> overlay_;
  uint32_t overlayStartMs_ = 0;

  uint8_t brightness_ = limits::kDefaultBrightnessPercent;
};

}  // namespace yule
