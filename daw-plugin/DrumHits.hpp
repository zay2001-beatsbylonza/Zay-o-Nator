// Drum hit detection for the DRUMS face. By BeatsbyLonza. MIT licence.
//
// Runs on the audio thread, sample by sample, so a hit is caught the moment it
// happens instead of from a blurry 40 ms spectrum. Each lane has its own filter:
//   KICK   low-pass ~110 Hz           (the thump)
//   SNARE  band-pass ~3 kHz (crack)   and ~200 Hz (body)
//   PERC   band-pass ~800 Hz          (claps, rims, toms, shakers in the mids)
//   HATS   high-pass ~9 kHz           (the sizzle)
// A lane "hits" when its fast envelope jumps well above its own slow average
// (an onset, not just loudness), is above a floor, and it isn't still ringing
// from its last hit. Then a few cross-checks stop one drum lighting another:
// a kick's click doesn't count as a snare, a snare's top end doesn't count as a hat,
// and the mids only count as PERC when the kick and snare are quiet.
#ifndef ZAYONATOR_DRUMHITS_HPP
#define ZAYONATOR_DRUMHITS_HPP

#include <atomic>
#include <cmath>
#include <cstdint>

namespace zay {

enum Lane { kKick = 0, kSnare, kPerc, kHats, kLanes };

struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    enum Type { LP, HP, BP };
    void set(Type t, float sr, float f, float q)
    {
        const float w = 2.0f * 3.14159265f * f / sr, cw = std::cos(w), alpha = std::sin(w) / (2.0f * q), a0 = 1.0f + alpha;
        if (t == LP) { b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2; }
        else if (t == HP) { b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; }
        else { b0 = alpha; b1 = 0; b2 = -alpha; }
        a1 = -2 * cw; a2 = 1 - alpha;
        b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
        z1 = z2 = 0;
    }
    float run(float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

struct Env
{
    float v = 0, att = 0, rel = 0;
    void set(float sr, float attackMs, float releaseMs)
    {
        att = 1.0f - std::exp(-1.0f / (sr * attackMs * 0.001f));
        rel = 1.0f - std::exp(-1.0f / (sr * releaseMs * 0.001f));
    }
    float run(float x) { x = std::fabs(x); v += (x > v ? att : rel) * (x - v); return v; }
};

class DrumHits
{
public:
    // How many hits each lane has had (the screen flashes when a count goes up).
    std::atomic<uint32_t> count[kLanes] {};

    void prepare(float sampleRate)
    {
        sr_ = sampleRate > 1000.0f ? sampleRate : 44100.0f;
        kickLp_[0].set(Biquad::LP, sr_, 110, 0.7f);  kickLp_[1].set(Biquad::LP, sr_, 110, 0.7f);
        snareHi_.set(Biquad::BP, sr_, 3000, 0.8f);
        snareLo_.set(Biquad::BP, sr_, 200, 1.0f);
        perc_.set(Biquad::BP, sr_, 800, 1.2f);
        hatHp_[0].set(Biquad::HP, sr_, 9000, 0.7f);  hatHp_[1].set(Biquad::HP, sr_, 9000, 0.7f);
        for (int i = 0; i < 6; ++i) { fast_[i].set(sr_, 0.5f, 12.0f); slow_[i].set(sr_, 60.0f, 250.0f); }
        for (int l = 0; l < kLanes; ++l) since_[l] = 1 << 30;
    }

    void run(float mono)
    {
        // 0 kick, 1 snare crack, 2 perc, 3 hats, 4 snare body
        const float x[5] = { kickLp_[1].run(kickLp_[0].run(mono)), snareHi_.run(mono), perc_.run(mono),
                             hatHp_[1].run(hatHp_[0].run(mono)), snareLo_.run(mono) };
        float f[5], s[5];
        for (int i = 0; i < 5; ++i) { f[i] = fast_[i].run(x[i]); s[i] = slow_[i].run(x[i]); }
        for (int l = 0; l < kLanes; ++l) { ++since_[l]; if (pending_[l] > 0) --pending_[l]; }
        const int ms = (int)(sr_ / 1000.0f);

        // KICK: the low end jumps (x2.5 over its average). Counted at once.
        // and is stronger than the mids (a clap has lows too, but more mids)
        if (onset(f[0], s[0], 2.5f, 0.03f) && f[0] > f[2] * 1.2f && since_[kKick] > 90 * ms) hit(kKick);

        // The others wait 10 ms before they count, so a hit that turns out to be part of
        // a kick (its click) or a snare (its top end) can be thrown out.
        // SNARE: the crack jumps, with the body (~200 Hz) jumping under it
        if (!pending_[kSnare] && since_[kSnare] > 80 * ms && onset(f[1], s[1], 2.5f, 0.008f) && f[4] > s[4] * 1.8f && f[4] > 0.01f)
        { pending_[kSnare] = 10 * ms; crackMax_ = 0.0f; lowMax_ = 0.0f; topMax_ = 0.0f; kickAt_ = since_[kKick]; }
        if (pending_[kSnare] > 0) { crackMax_ = std::fmax(crackMax_, f[1]); lowMax_ = std::fmax(lowMax_, f[0]); topMax_ = std::fmax(topMax_, f[3]); }
        // HATS: the top jumps and is strong next to the snare band
        if (!pending_[kHats] && since_[kHats] > 45 * ms && onset(f[3], s[3], 2.2f, 0.003f) && f[3] > f[1] * 0.35f)
        { pending_[kHats] = 10 * ms; hatTop_ = f[3] / (f[1] + 1e-9f); }
        // PERC: the mids jump
        if (!pending_[kPerc] && since_[kPerc] > 70 * ms && onset(f[2], s[2], 2.8f, 0.008f))
        { pending_[kPerc] = 10 * ms; percNear_ = since_[kKick] < 40 * ms || since_[kSnare] < 40 * ms; }

        // decide the ones that have waited
        if (pending_[kSnare] == 1)
        {
            // with a kick at the same moment, the crack has to be loud next to the kick's lows
            // and the hat's top (a real snare on the kick), not just the kick's click or a hat
            const bool kickNow = kickAt_ < 45 * ms || since_[kKick] < 11 * ms;   // the kick was on it, still ringing, or just landed
            if (!kickNow || (crackMax_ > 0.2f * lowMax_ && crackMax_ > 0.8f * topMax_)) hit(kSnare);
        }
        // HATS: a clearly top-heavy hit is a hat even on a snare beat; a duller one only away from a snare
        if (pending_[kHats] == 1 && (hatTop_ > 1.6f || (since_[kSnare] > 26 * ms && pending_[kSnare] == 0))) hit(kHats);
        // PERC waits for a snare that's still being decided; it doesn't count next to a kick or a snare
        if (pending_[kPerc] == 1 && pending_[kSnare] > 1) pending_[kPerc] = pending_[kSnare] + 1;
        if (pending_[kPerc] == 1 && !percNear_ && since_[kKick] > 26 * ms && since_[kSnare] > 26 * ms) hit(kPerc);
    }

private:
    static bool onset(float fast, float slow, float ratio, float floor) { return fast > floor && fast > slow * ratio; }
    void hit(int lane)
    {
        since_[lane] = 0;
        if ((lane == kKick || lane == kSnare) && pending_[kPerc] > 0) percNear_ = true;
        count[lane].fetch_add(1, std::memory_order_relaxed);
    }

    float sr_ = 44100.0f;
    Biquad kickLp_[2], snareHi_, snareLo_, perc_, hatHp_[2];
    Env fast_[6], slow_[6];
    int since_[kLanes] = {}, pending_[kLanes] = {};
    int kickAt_ = 0;
    bool percNear_ = false;
    float crackMax_ = 0.0f, lowMax_ = 0.0f, topMax_ = 0.0f, hatTop_ = 0.0f;
};

} // namespace zay

#endif
