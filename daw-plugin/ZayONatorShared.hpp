// What the plugin and its screen share. By BeatsbyLonza. MIT licence.
#ifndef ZAYONATOR_SHARED_HPP
#define ZAYONATOR_SHARED_HPP

#include <atomic>
#include <cstdint>

namespace zay {

enum Param : uint32_t {
    kName = 0,   // what this track is called on the display: MASTER, DRUMS, SYNTHS, ...
    kSend,       // send the meters to the ZaY-o-Nator Display (on by default)
    kRef,        // the level that reads 0 VU: -18, -14 or -10 dBFS
    kLookL,      // left meter face: Classic (cream) or Noir (black)
    kLookR,      // right meter face
    kParamCount
};

constexpr int kNames = 8;
static const char* const kNameLabels[kNames] = { "MASTER", "DRUMS", "SYNTHS", "BASS", "KEYS", "VOCALS", "FX", "TRACK" };
static const char* const kRefLabels[3] = { "-18 dB", "-14 dB", "-10 dB" };
static const float kRefDb[3] = { 18.0f, 14.0f, 10.0f };
static const char* const kLookLabels[2] = { "Classic", "Noir" };

constexpr int kBands = 32;

// The newest meter reading, written ~25 times a second by the plugin's worker thread
// and read by the screen. Each value is a separate atomic: a torn frame only shows for 40 ms.
struct Snapshot {
    std::atomic<float> peakDb[2] { {-120.0f}, {-120.0f} };
    std::atomic<float> rmsDb[2] { {-120.0f}, {-120.0f} };
    std::atomic<float> bandDb[kBands];
    std::atomic<int> sendState { 0 };   // 0 = off, 1 = sending, 2 = can't send
    std::atomic<uint32_t> counter { 0 };
    Snapshot() { for (auto& b : bandDb) b.store(-120.0f); }
};

// VU scale (VU value, needle angle in degrees), the same as the MPC plugin and the display
static const float kMarks[11][2] = { {-20,-48}, {-10,-30}, {-7,-20}, {-5,-12}, {-3,-2}, {-2,5}, {-1,11}, {0,18}, {1,28}, {2,37}, {3,47} };

inline float vuAngle(float vu)
{
    if (vu <= kMarks[0][0]) { const float a = kMarks[0][1] + (vu - kMarks[0][0]) * 0.6f; return a < -52.0f ? -52.0f : a; }
    for (int i = 1; i < 11; ++i)
        if (vu <= kMarks[i][0])
            return kMarks[i - 1][1] + (kMarks[i][1] - kMarks[i - 1][1]) * (vu - kMarks[i - 1][0]) / (kMarks[i][0] - kMarks[i - 1][0]);
    const float a = 47.0f + (vu - 3.0f) * 4.0f;
    return a > 52.0f ? 52.0f : a;
}

} // namespace zay

#endif
