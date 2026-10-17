#include "cues.h"

#include <cmath>

#include "effects.h"

namespace yule {

namespace {

constexpr float kPi = 3.14159265358979f;

constexpr uint32_t kSentMs = 900;        // two pulses of 450 ms
constexpr uint32_t kFailedMs = 900;      // three blinks of 300 ms
constexpr uint32_t kPleaseWaitMs = 600;  // one pulse
constexpr uint32_t kMutedMs = 800;
constexpr uint32_t kUnmutedMs = 800;

constexpr Rgb kGreen{0, 220, 60};
constexpr Rgb kRed{255, 0, 0};
constexpr Rgb kAmber{255, 110, 0};
constexpr Rgb kBlue{0, 0, 255};
constexpr Rgb kPurple{170, 0, 255};
constexpr Rgb kDimRed{60, 0, 0};
constexpr Rgb kDimWhite{60, 60, 60};

void fill(Rgb* px, size_t n, Rgb c) {
  for (size_t i = 0; i < n; ++i) px[i] = c;
}

Rgb scaledBy(Rgb c, float f) {
  if (f <= 0.0f) return kBlack;
  if (f >= 1.0f) return c;
  return scaleRgb(c, static_cast<uint32_t>(f * 1024.0f + 0.5f), 1024);
}

// One smooth hump per period: 0 -> 1 -> 0.
float hump(uint32_t tMs, uint32_t periodMs) {
  const float phase = static_cast<float>(tMs % periodMs) / static_cast<float>(periodMs);
  const float s = std::sin(kPi * phase);
  return s * s;
}

// A continuous gentle breathing level in [0, 1] with the given period.
float breathing(uint32_t tMs, uint32_t periodMs) {
  const float phase = static_cast<float>(tMs % periodMs) / static_cast<float>(periodMs);
  const float level = 0.5f - 0.5f * std::cos(2.0f * kPi * phase);
  return level * level;
}

class PulseCue : public Animation {
 public:
  PulseCue(Rgb colour, uint32_t pulseMs, uint32_t pulses, float peak)
      : colour_(colour), pulseMs_(pulseMs), pulses_(pulses), peak_(peak) {}
  uint32_t durationMs() const override { return pulseMs_ * pulses_; }
  void render(uint32_t tMs, Rgb* px, size_t n) override {
    fill(px, n, tMs >= durationMs() ? kBlack : scaledBy(colour_, peak_ * hump(tMs, pulseMs_)));
  }

 private:
  Rgb colour_;
  uint32_t pulseMs_;
  uint32_t pulses_;
  float peak_;
};

class BlinkCue : public Animation {
 public:
  BlinkCue(Rgb colour, uint32_t blinkMs, uint32_t blinks) : colour_(colour), blinkMs_(blinkMs), blinks_(blinks) {}
  uint32_t durationMs() const override { return blinkMs_ * blinks_; }
  void render(uint32_t tMs, Rgb* px, size_t n) override {
    const bool on = tMs < durationMs() && (tMs % blinkMs_) < blinkMs_ / 2;
    fill(px, n, on ? colour_ : kBlack);
  }

 private:
  Rgb colour_;
  uint32_t blinkMs_;
  uint32_t blinks_;
};

class SolidCue : public Animation {
 public:
  explicit SolidCue(Rgb colour) : colour_(colour) {}
  void render(uint32_t, Rgb* px, size_t n) override { fill(px, n, colour_); }

 private:
  Rgb colour_;
};

class MutedCue : public Animation {
 public:
  uint32_t durationMs() const override { return kMutedMs; }
  void render(uint32_t tMs, Rgb* px, size_t n) override {
    const float f = tMs >= kMutedMs ? 0.0f : 1.0f - static_cast<float>(tMs) / static_cast<float>(kMutedMs);
    fill(px, n, scaledBy(kBlue, f));
  }
};

class UnmutedCue : public Animation {
 public:
  uint32_t durationMs() const override { return kUnmutedMs; }
  void render(uint32_t tMs, Rgb* px, size_t n) override {
    constexpr uint32_t kRiseMs = 600;
    float f = 0.0f;
    if (tMs < kRiseMs) {
      f = static_cast<float>(tMs) / static_cast<float>(kRiseMs);
    } else if (tMs < kUnmutedMs) {
      f = static_cast<float>(kUnmutedMs - tMs) / static_cast<float>(kUnmutedMs - kRiseMs);
    }
    fill(px, n, scaledBy(kBlue, f));
  }
};

class BreathingIndication : public Animation {
 public:
  BreathingIndication(Rgb colour, uint32_t periodMs, float peak) : colour_(colour), periodMs_(periodMs), peak_(peak) {}
  void render(uint32_t tMs, Rgb* px, size_t n) override { fill(px, n, scaledBy(colour_, peak_ * breathing(tMs, periodMs_))); }

 private:
  Rgb colour_;
  uint32_t periodMs_;
  float peak_;
};

class ConnectionLostIndication : public Animation {
 public:
  void render(uint32_t tMs, Rgb* px, size_t n) override {
    fill(px, n, kBlack);
    // A single dim red LED, lit briefly once every 5 seconds.
    if (n > 0 && (tMs % 5000) < 150) px[0] = kDimRed;
  }
};

}  // namespace

uint32_t cueDurationMs(Cue cue) {
  switch (cue) {
    case Cue::Sent: return kSentMs;
    case Cue::SendFailed: return kFailedMs;
    case Cue::PleaseWait: return kPleaseWaitMs;
    case Cue::Muted: return kMutedMs;
    case Cue::Unmuted: return kUnmutedMs;
    case Cue::LongPressReached:
    case Cue::VeryLongPressReached:
    default: return 0;
  }
}

std::shared_ptr<Animation> makeCue(Cue cue) {
  switch (cue) {
    case Cue::Sent: return std::make_shared<PulseCue>(kGreen, kSentMs / 2, 2, 1.0f);
    case Cue::SendFailed: return std::make_shared<BlinkCue>(kRed, kFailedMs / 3, 3);
    case Cue::PleaseWait: return std::make_shared<PulseCue>(kAmber, kPleaseWaitMs, 1, 0.35f);
    case Cue::LongPressReached: return std::make_shared<SolidCue>(kBlue);
    case Cue::VeryLongPressReached: {
      Pattern p;
      p.effect = Effect::Chase;
      p.colors[0] = kPurple;
      p.colorCount = 1;
      p.speed = 3;
      return std::shared_ptr<Animation>(makeEffect(p, 1));
    }
    case Cue::Muted: return std::make_shared<MutedCue>();
    case Cue::Unmuted: return std::make_shared<UnmutedCue>();
  }
  return std::make_shared<SolidCue>(kBlack);
}

std::shared_ptr<Animation> makeSetupIndication() { return std::make_shared<BreathingIndication>(kBlue, 4000, 1.0f); }

std::shared_ptr<Animation> makeConnectingIndication() { return std::make_shared<BreathingIndication>(kAmber, 2400, 0.6f); }

std::shared_ptr<Animation> makeConnectionLostIndication() { return std::make_shared<ConnectionLostIndication>(); }

void UpdateProgressIndication::setPercent(int percent) {
  percent_ = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
}

void UpdateProgressIndication::render(uint32_t, Rgb* px, size_t n) {
  const size_t lit = (static_cast<size_t>(percent_) * n + 50) / 100;
  for (size_t i = 0; i < n; ++i) px[i] = i < lit ? kDimWhite : kBlack;
}

}  // namespace yule
