// Checks the DRUMS face's hit detection on a made-up beat with known hits:
// kick on 1 and 3, snare on 2 and 4, hats on every 8th, a clap (perc) now and then,
// plus a quiet pad underneath. Every real hit must be caught on its own lane within
// 30 ms, and a lane may only rarely light for a drum that didn't play (a clap can
// read as a snare: claps and snares share the same crack).
//
// Build: c++ -std=c++17 -O2 -Idaw-plugin tests/drum_hits_test.cpp -o drum_hits_test
#include "DrumHits.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

using namespace zay;

int main()
{
    const float sr = 48000.0f;
    const double bpm = 92.0, beat = 60.0 / bpm;
    const int bars = 8;
    const size_t n = (size_t)(sr * beat * 4 * bars) + 48000;
    std::vector<float> x(n, 0.0f);
    std::mt19937 rng(7);
    std::normal_distribution<float> noise(0.0f, 1.0f);
    struct Hit { int lane; size_t at; };
    std::vector<Hit> truth;

    auto add = [&](size_t at, int lane) {
        truth.push_back({ lane, at });
        float hp1 = 0, hp2 = 0, lpA = 0;
        for (size_t i = 0; i < (size_t)(sr * 0.4f) && at + i < n; ++i)
        {
            const float t = i / sr;
            float v = 0;
            if (lane == kKick)   // pitch-dropping sine plus a short click
            {
                const float f = 50 + 90 * std::exp(-t * 30), ph = 2 * 3.14159f * (50 * t + 90 / 30.0f * (1 - std::exp(-t * 30)));
                v = 0.9f * std::sin(ph) * std::exp(-t * 7) + 0.15f * noise(rng) * std::exp(-t * 400);
                (void)f;
            }
            else if (lane == kSnare)   // 190 Hz body plus a noise crack (snare wires: mid-heavy, softer up top)
            {
                lpA += 0.55f * (noise(rng) - lpA);
                v = 0.35f * std::sin(2 * 3.14159f * 190 * t) * std::exp(-t * 25) + 0.6f * lpA * std::exp(-t * 22);
            }
            else if (lane == kHats)   // high-passed noise, short
            {
                const float w = noise(rng);
                hp1 = 0.2f * hp1 + 0.2f * (w - lpA); lpA = w;   // crude high-pass
                hp2 = w - hp1;
                v = 0.22f * (w - 0.9f * hp2 * 0.0f) * std::exp(-t * 60);
                v = 0.2f * (noise(rng) - noise(rng)) * std::exp(-t * 70);
            }
            else   // perc: a clap, three quick mid bursts
            {
                const float b = std::exp(-std::fmod(t, 0.011f) * 300) * (t < 0.033f ? 1.0f : std::exp(-(t - 0.033f) * 25));
                lpA += 0.35f * (noise(rng) - lpA);
                v = 0.5f * lpA * b;
            }
            x[at + i] += v;
        }
    };
    // a hat needs to really be high: run the hat lane through a simple 6 kHz high-pass after
    for (int bar = 0; bar < bars; ++bar)
        for (int e = 0; e < 8; ++e)
        {
            const size_t at = (size_t)((bar * 4 + e * 0.5) * beat * sr) + 4800;
            if (e == 0 || e == 4) add(at, kKick);
            if (e == 2 || e == 6) add(at, kSnare);
            if (e == 3 && bar % 2 == 1) add(at + (size_t)(0.25 * beat * sr), kPerc);
        }
    // hats on every 8th, as real high-passed noise
    {
        Biquad hp; hp.set(Biquad::HP, sr, 7000, 0.7f);
        Biquad hp2; hp2.set(Biquad::HP, sr, 7000, 0.7f);
        std::vector<float> h(n, 0.0f);
        for (int bar = 0; bar < bars; ++bar)
            for (int e = 0; e < 8; ++e)
            {
                const size_t at = (size_t)((bar * 4 + e * 0.5) * beat * sr) + 4800;
                truth.push_back({ kHats, at });
                for (size_t i = 0; i < (size_t)(sr * 0.08f) && at + i < n; ++i) h[at + i] += 0.6f * noise(rng) * std::exp(-(i / sr) * 55);
            }
        for (size_t i = 0; i < n; ++i) x[i] += hp2.run(hp.run(h[i]));
    }
    // a quiet pad chord underneath
    for (size_t i = 0; i < n; ++i)
    {
        const float t = i / sr;
        x[i] += 0.04f * (std::sin(2 * 3.14159f * 220 * t) + std::sin(2 * 3.14159f * 277 * t) + std::sin(2 * 3.14159f * 330 * t));
    }

    const float gain = std::getenv("GAIN") ? (float)std::atof(std::getenv("GAIN")) : 1.0f;
    for (float& v : x) v *= gain;
    DrumHits d;
    d.prepare(sr);
    std::vector<Hit> found;
    uint32_t last[kLanes] = {};
    for (size_t i = 0; i < n; ++i)
    {
        d.run(x[i]);
        for (int l = 0; l < kLanes; ++l)
        {
            const uint32_t c = d.count[l].load();
            if (c != last[l]) { last[l] = c; found.push_back({ l, i }); }
        }
    }

    static const char* names[kLanes] = { "KICK", "SNARE", "PERC", "HATS" };
    const size_t tol = (size_t)(0.030f * sr);   // a hit shows within 30 ms (one frame at 30 fps)
    int failures = 0;
    for (int l = 0; l < kLanes; ++l)
    {
        int real = 0, caught = 0, extra = 0;
        for (const Hit& t : truth) if (t.lane == l)
        {
            ++real;
            for (const Hit& f : found) if (f.lane == l && f.at + tol / 3 >= t.at && f.at <= t.at + tol) { ++caught; break; }
        }
        for (const Hit& f : found) if (f.lane == l)
        {
            bool ok = false;
            for (const Hit& t : truth) if (t.lane == l && f.at + tol / 3 >= t.at && f.at <= t.at + tol) ok = true;
            if (!ok)
            {
                ++extra;
                if (std::getenv("DEBUG"))
                {
                    long best = 1L << 40; int bl = -1;
                    for (const Hit& t : truth) { long d = (long)f.at - (long)t.at; if (std::labs(d) < std::labs(best)) { best = d; bl = t.lane; } }
                    std::printf("   false %s at %.3fs, nearest %s %+.1f ms\n", names[l], f.at / sr, bl >= 0 ? names[bl] : "-", best / sr * 1000);
                }
            }
        }
        // at least 9 in 10 real hits caught, and at most 1 false flash for every 4 real hits
        const bool pass = caught >= real * 9 / 10 && extra <= real / 4;
        std::printf("%s  %-5s caught %d of %d, %d false\n", pass ? "PASS" : "FAIL", names[l], caught, real, extra);
        if (!pass) ++failures;
    }
    std::printf(failures ? "%d lanes FAILED\n" : "drum hit detection: all lanes pass\n", failures);
    return failures ? 1 : 0;
}
