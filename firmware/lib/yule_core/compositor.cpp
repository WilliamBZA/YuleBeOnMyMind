#include "compositor.h"

#include <utility>

namespace yule {

void Compositor::setBrightness(uint8_t percent) {
  if (percent < limits::kMinBrightnessPercent) percent = limits::kMinBrightnessPercent;
  if (percent > limits::kMaxBrightnessPercent) percent = limits::kMaxBrightnessPercent;
  brightness_ = percent;
}

void Compositor::setBase(std::shared_ptr<Animation> animation, uint32_t nowMs) {
  base_ = std::move(animation);
  baseStartMs_ = nowMs;
  fading_ = false;
}

void Compositor::startFadeOut(uint32_t nowMs) {
  if (!base_) return;
  fading_ = true;
  fadeStartMs_ = nowMs;
}

void Compositor::setOverlay(std::shared_ptr<Animation> animation, uint32_t nowMs) {
  overlay_ = std::move(animation);
  overlayStartMs_ = nowMs;
}

void Compositor::clearOverlay() { overlay_.reset(); }

void Compositor::render(uint32_t nowMs, Rgb* out, size_t n) {
  for (size_t i = 0; i < n; ++i) out[i] = kBlack;

  if (base_) {
    base_->render(nowMs - baseStartMs_, out, n);  // the base keeps its own timeline even under an overlay
    if (fading_) {
      const uint32_t fadeElapsed = nowMs - fadeStartMs_;
      if (fadeElapsed >= limits::kFadeOutMs) {
        base_.reset();
        fading_ = false;
        for (size_t i = 0; i < n; ++i) out[i] = kBlack;
      } else {
        const uint32_t remaining = limits::kFadeOutMs - fadeElapsed;
        for (size_t i = 0; i < n; ++i) out[i] = scaleRgb(out[i], remaining, limits::kFadeOutMs);
      }
    }
  }

  if (overlay_) {
    const uint32_t elapsed = nowMs - overlayStartMs_;
    const uint32_t duration = overlay_->durationMs();
    if (duration != 0 && elapsed >= duration) {
      overlay_.reset();
    } else {
      for (size_t i = 0; i < n; ++i) out[i] = kBlack;
      overlay_->render(elapsed, out, n);
    }
  }

  if (brightness_ < limits::kMaxBrightnessPercent) {
    for (size_t i = 0; i < n; ++i) out[i] = scaleRgb(out[i], brightness_, 100);
  }
}

}  // namespace yule
