// A pretend MPC: loads the real ZaY-o-Nator plugin (.so), plays a test tone through
// it on an audio thread like the MPC does, and checks that the sound passes through,
// that the screen's needles, LEDs and bars move, and that it says it is sending.
// (tests/plugin_test.sh also checks that the ZaY-o-Nator Display receives the meters.)
//
// Build: c++ -std=c++17 -I<sdk>/framework tests/plugin_host_test.cpp -ldl -lpthread -o plugin_host_test
// Run:   ./plugin_host_test <plugin.so> [seconds]
#include "mpc_vst2_abi.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <string>
#include <thread>
#include <vector>

enum { kOutput, kSend, kLookL, kLookR, kRef, kVuL, kVuR, kPeakL, kPeakR, kStatus, kBand0 };

static std::atomic<bool> running{true};
static std::atomic<float> worst{0.0f};
static int failures = 0;

static VstIntPtr host(AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float)
{
    return opcode == audioMasterVersion ? 2400 : 0;
}

static std::string display(AEffect* e, int i)
{
    char buf[256] = {0};
    e->dispatcher(e, effGetParamDisplay, i, 0, buf, 0.0f);
    return buf;
}

static void check(bool ok, const char* what, const std::string& seen)
{
    std::printf("%s  %s   [%s]\n", ok ? "PASS" : "FAIL", what, seen.c_str());
    if (!ok) failures++;
}

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: %s plugin.so [seconds]\n", argv[0]); return 2; }
    const int seconds = argc > 2 ? std::atoi(argv[2]) : 3;
    void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib) { std::fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    auto entry = (AEffect* (*)(audioMasterCallback))dlsym(lib, "VSTPluginMain");
    AEffect* e = entry(host);
    if (!e || e->magic != kEffectMagic) { std::fprintf(stderr, "no effect\n"); return 1; }

    e->dispatcher(e, effOpen, 0, 0, nullptr, 0.0f);
    e->dispatcher(e, effSetSampleRate, 0, 0, nullptr, 44100.0f);
    e->dispatcher(e, effSetBlockSize, 0, 128, nullptr, 0.0f);
    e->dispatcher(e, effMainsChanged, 0, 1, nullptr, 0.0f);

    // a 100 Hz tone with a little 3 kHz (about -16 dBFS RMS), in real time
    std::thread audio([e] {
        std::vector<float> l(128), r(128), ol(128), orr(128);
        float* in[2] = { l.data(), r.data() };
        float* out[2] = { ol.data(), orr.data() };
        double t = 0.0;
        while (running)
        {
            for (int i = 0; i < 128; ++i, t += 1.0 / 44100.0)
                l[i] = r[i] = (float)(0.2 * std::sin(2 * M_PI * 100 * t) + 0.03 * std::sin(2 * M_PI * 3000 * t));
            e->processReplacing(e, in, out, 128);
            for (int i = 0; i < 128; ++i)
                worst.store(std::fmax(worst.load(), std::fabs(ol[i] - l[i]) + std::fabs(orr[i] - r[i])));
            std::this_thread::sleep_for(std::chrono::microseconds(2900));
        }
    });

    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    // -17 dBFS RMS with 0 VU = -10 dBFS reads about -7 VU: off the rest, not pinned
    const int vuL = std::atoi(display(e, kVuL).c_str()), vuR = std::atoi(display(e, kVuR).c_str());
    check(vuL >= 6 && vuL <= 25, "left needle swings (classic face, frames 0-40)", display(e, kVuL));
    check(vuR >= 41 + 6 && vuR <= 41 + 25, "right needle swings (noir face, frames 41-81)", display(e, kVuR));
    check(e->getParameter(e, kPeakL) > 0.2f, "left LEDs are lit", display(e, kPeakL));
    int lowBar = 0, topBar = 0;
    for (int b = 0; b < 32; ++b)
    {
        const int v = std::atoi(display(e, kBand0 + b).c_str());
        if (b < 8) lowBar = std::max(lowBar, v);
        if (b > 28) topBar = std::max(topBar, v);
    }
    check(lowBar >= 8, "bass bars are tall for a 100 Hz tone", std::to_string(lowBar));
    check(topBar < lowBar, "top bars are lower than the bass", std::to_string(topBar));
    check(display(e, kStatus).find("Sending") != std::string::npos, "says it is sending to the display", display(e, kStatus));
    check(worst.load() < 1e-3f, "sound passes straight through at 0 dB", std::to_string(worst.load()));

    e->setParameter(e, kSend, 0.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    check(display(e, kStatus).find("Send is off") != std::string::npos, "Send off says so", display(e, kStatus));

    running = false;
    audio.join();
    e->dispatcher(e, effMainsChanged, 0, 0, nullptr, 0.0f);
    e->dispatcher(e, effClose, 0, 0, nullptr, 0.0f);
    std::printf(failures ? "%d FAILED\n" : "all plugin checks passed\n", failures);
    return failures ? 1 : 0;
}
