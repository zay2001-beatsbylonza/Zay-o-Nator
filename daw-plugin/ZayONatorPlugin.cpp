// ZaY-o-Nator for your DAW (VST3 and AU): the sound side. By BeatsbyLonza. MIT licence.
//
// Audio passes straight through. run() (the audio thread) copies each block into two
// small ring buffers and does nothing else. A worker thread, started when the host
// activates the plugin, wakes ~25 times a second and works out the levels (peak, RMS)
// and 32 spectrum bands for the screen, and - when Send is on - sends one small UDP
// packet to the ZaY-o-Nator Display on this computer (and the network):
//     ZAYO1|DAW <NAME>|peakL|peakR|rmsL|rmsR|b1,...,b32|kick,snare,perc,hats (hit counts)
// The display shows it like an MPC: a button with the name, and a pair in RACK.
//
// Parameter order is FROZEN once released (saved sessions store it).

// Sockets first: on Windows winsock2.h has to come before anything that pulls in windows.h
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET zay_socket;
static const zay_socket kNoSocket = INVALID_SOCKET;
static void zayCloseSocket(zay_socket s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int zay_socket;
static const zay_socket kNoSocket = -1;
static void zayCloseSocket(zay_socket s) { close(s); }
#endif

#include "DistrhoPlugin.hpp"
#include "ZayONatorShared.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

START_NAMESPACE_DISTRHO

using namespace zay;

static constexpr int kRing = 8192;   // per channel, a power of two
static constexpr int kFft = 2048;
static constexpr int kPort = 47911;
static constexpr float kPi = 3.14159265358979f;

class ZayONatorPlugin : public Plugin
{
public:
    ZayONatorPlugin() : Plugin(kParamCount, 0, 0)
    {
        values_[kName] = 0.0f;
        values_[kSend] = 0.0f;   // off until the user turns it on
        values_[kRef] = 2.0f;
        values_[kLookL] = 0.0f;
        values_[kLookR] = 1.0f;
        values_[kFace] = 0.0f;
    }

    ~ZayONatorPlugin() override { stopWorker(); }

    // The screen reads this (DISTRHO_PLUGIN_WANT_DIRECT_ACCESS).
    const Snapshot& snapshot() const noexcept { return snap_; }

protected:
    const char* getLabel() const override { return "ZaY-o-Nator"; }
    const char* getDescription() const override
    {
        return "Backlit VU meters, LED peak meters and a spectrum, sent to the ZaY-o-Nator Display for your extra monitor.";
    }
    const char* getMaker() const override { return "BeatsbyLonza"; }
    const char* getHomePage() const override { return "https://github.com/zay2001-beatsbylonza/Zay-o-Nator"; }
    const char* getLicense() const override { return "MIT"; }
    uint32_t getVersion() const override { return d_version(1, 1, 3); }

    void initAudioPort(bool input, uint32_t index, AudioPort& port) override
    {
        port.groupId = kPortGroupStereo;
        Plugin::initAudioPort(input, index, port);
    }

    static void enumerate(Parameter& p, const char* const* labels, int n)
    {
        p.enumValues.count = (uint8_t)n;
        p.enumValues.restrictedMode = true;
        ParameterEnumerationValue* v = new ParameterEnumerationValue[n];
        for (int i = 0; i < n; ++i) { v[i].label = labels[i]; v[i].value = (float)i; }
        p.enumValues.values = v;
    }

    void initParameter(uint32_t index, Parameter& p) override
    {
        p.hints = kParameterIsAutomatable | kParameterIsInteger;
        p.ranges.min = 0.0f;
        switch (index)
        {
        case kName:
            p.name = "Name"; p.symbol = "name"; p.ranges.max = kNames - 1; p.ranges.def = 0.0f;
            enumerate(p, kNameLabels, kNames);
            break;
        case kSend:
            p.hints |= kParameterIsBoolean;
            p.name = "Send"; p.symbol = "send"; p.ranges.max = 1.0f; p.ranges.def = 0.0f;
            break;
        case kRef:
            p.name = "0 VU"; p.symbol = "ref"; p.ranges.max = 2.0f; p.ranges.def = 2.0f;
            enumerate(p, kRefLabels, 3);
            break;
        case kLookL:
        case kLookR:
            p.name = index == kLookL ? "Left Meter" : "Right Meter";
            p.symbol = index == kLookL ? "lookl" : "lookr";
            p.ranges.max = 1.0f; p.ranges.def = index == kLookL ? 0.0f : 1.0f;
            enumerate(p, kLookLabels, 2);
            break;
        case kFace:
            p.name = "Face"; p.symbol = "face"; p.ranges.max = kFaces - 1; p.ranges.def = 0.0f;
            enumerate(p, kFaceLabels, kFaces);
            break;
        }
    }

    float getParameterValue(uint32_t index) const override { return index < kParamCount ? values_[index] : 0.0f; }
    void setParameterValue(uint32_t index, float value) override { if (index < kParamCount) values_[index] = value; }

    void activate() override
    {
        drums_.prepare((float)getSampleRate());
        startWorker();
    }
    void deactivate() override { stopWorker(); }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        uint32_t w = write_.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < frames; ++i)
        {
            const float l = inputs[0][i], r = inputs[1][i];
            outputs[0][i] = l;
            outputs[1][i] = r;
            drums_.run(0.5f * (l + r));
            ringL_[w & (kRing - 1)] = l;
            ringR_[w & (kRing - 1)] = r;
            ++w;
        }
        write_.store(w, std::memory_order_release);
    }

private:
    // ---------------- worker thread ----------------

    void startWorker()
    {
        std::lock_guard<std::mutex> lk(threadMu_);
        if (worker_.joinable()) return;
        stop_ = false;
        worker_ = std::thread([this] { work(); });
    }

    void stopWorker()
    {
        std::lock_guard<std::mutex> lk(threadMu_);
        {
            std::lock_guard<std::mutex> wl(wakeMu_);
            stop_ = true;
        }
        wake_.notify_all();
        if (worker_.joinable()) worker_.join();
        if (sock_ != kNoSocket) { zayCloseSocket(sock_); sock_ = kNoSocket; }
#ifdef _WIN32
        if (wsa_) { WSACleanup(); wsa_ = false; }
#endif
    }

    void work()
    {
#ifdef _WIN32
        WSADATA wd;
        wsa_ = WSAStartup(MAKEWORD(2, 2), &wd) == 0;
#endif
        sock_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock_ != kNoSocket) { int yes = 1; setsockopt(sock_, SOL_SOCKET, SO_BROADCAST, (const char*)&yes, sizeof yes); }
        uint32_t read = write_.load(std::memory_order_acquire);
        float bands[kBands];
        while (true)
        {
            {
                std::unique_lock<std::mutex> lk(wakeMu_);
                if (wake_.wait_for(lk, std::chrono::milliseconds(40), [this] { return stop_; })) break;
            }
            const uint32_t w = write_.load(std::memory_order_acquire);
            uint32_t n = w - read;
            if (n > (uint32_t)kRing) { read = w - kRing; n = kRing; }
            float peak[2] = { 0.0f, 0.0f }, sum[2] = { 0.0f, 0.0f };
            for (uint32_t i = 0; i < n; ++i)
            {
                const float l = ringL_[(read + i) & (kRing - 1)], r = ringR_[(read + i) & (kRing - 1)];
                peak[0] = std::max(peak[0], std::fabs(l)); peak[1] = std::max(peak[1], std::fabs(r));
                sum[0] += l * l; sum[1] += r * r;
            }
            read = w;
            float peakDb[2], rmsDb[2];
            for (int c = 0; c < 2; ++c)
            {
                peakDb[c] = toDb(peak[c]);
                rmsDb[c] = n ? toDb(std::sqrt(sum[c] / (float)n)) : -120.0f;
                snap_.peakDb[c].store(peakDb[c]);
                snap_.rmsDb[c].store(rmsDb[c]);
            }
            for (int i = 0; i < kWave; ++i)
            {
                const uint32_t k = (w - kWave + (uint32_t)i) & (kRing - 1);
                snap_.wave[0][i].store(ringL_[k]);
                snap_.wave[1][i].store(ringR_[k]);
            }
            for (int l = 0; l < kLanes; ++l) snap_.hits[l].store(drums_.count[l].load(std::memory_order_relaxed));
            spectrum(w, bands);
            for (int b = 0; b < kBands; ++b) snap_.bandDb[b].store(bands[b]);

            if (values_[kSend] > 0.5f) snap_.sendState.store(send(peakDb, rmsDb, bands) ? 1 : 2);
            else snap_.sendState.store(0);
            snap_.counter.fetch_add(1);
        }
    }

    // One packet to this computer (the display is usually here) and one broadcast (a display on another computer).
    bool send(const float* peakDb, const float* rmsDb, const float* bands)
    {
        if (sock_ == kNoSocket) return false;
        const int name = std::min(kNames - 1, std::max(0, (int)std::lround(values_[kName])));
        char buf[1024];
        int len = std::snprintf(buf, sizeof buf, "ZAYO1|DAW %s|%.1f|%.1f|%.1f|%.1f|", kNameLabels[name],
                                peakDb[0], peakDb[1], rmsDb[0], rmsDb[1]);
        for (int b = 0; b < kBands && len < (int)sizeof buf - 16; ++b)
            len += std::snprintf(buf + len, sizeof buf - len, b ? ",%.1f" : "%.1f", std::max(-120.0f, bands[b]));
        sockaddr_in to{};
        len += std::snprintf(buf + len, sizeof buf - len, "|%u,%u,%u,%u", snap_.hits[0].load(), snap_.hits[1].load(),
                             snap_.hits[2].load(), snap_.hits[3].load());
        to.sin_family = AF_INET;
        to.sin_port = htons(kPort);
        to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        const bool local = sendto(sock_, buf, len, 0, (const sockaddr*)&to, sizeof to) == len;
        to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
        sendto(sock_, buf, len, 0, (const sockaddr*)&to, sizeof to);   // fine if the network says no
        return local;
    }

    // 32 bands from 30 Hz to 18 kHz, in dB (0 = full scale), tilted 3 dB an octave so the highs show
    void spectrum(uint32_t w, float* out)
    {
        for (int i = 0; i < kFft; ++i)
        {
            const uint32_t k = (w - kFft + (uint32_t)i) & (kRing - 1);
            const float win = 0.5f - 0.5f * std::cos(2.0f * kPi * (float)i / (kFft - 1));
            re_[i] = win * 0.5f * (ringL_[k] + ringR_[k]);
            im_[i] = 0.0f;
        }
        fft(re_, im_);
        const float sr = (float)getSampleRate(), binHz = (sr > 1000.0f ? sr : 44100.0f) / kFft, norm = 4.0f / kFft;
        for (int b = 0; b < kBands; ++b)
        {
            const float f0 = 30.0f * std::pow(600.0f, (float)b / kBands), f1 = 30.0f * std::pow(600.0f, (float)(b + 1) / kBands);
            const int k0 = std::max(1, (int)(f0 / binHz)), k1 = std::min(kFft / 2, std::max(k0 + 1, (int)(f1 / binHz) + 1));
            float m = 0.0f;
            for (int k = k0; k < k1; ++k) m = std::max(m, re_[k] * re_[k] + im_[k] * im_[k]);
            out[b] = 10.0f * std::log10(m * norm * norm + 1e-14f) + 3.0f * std::log2(std::max(f0, 50.0f) / 1000.0f);
        }
    }

    static void fft(float* re, float* im)
    {
        const int n = kFft;
        for (int i = 1, j = 0; i < n; ++i)
        {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const float ang = -2.0f * kPi / len, wr = std::cos(ang), wi = std::sin(ang);
            for (int i = 0; i < n; i += len)
            {
                float cr = 1.0f, ci = 0.0f;
                for (int k = 0; k < len / 2; ++k)
                {
                    const int a = i + k, b = a + len / 2;
                    const float tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
                    re[b] = re[a] - tr; im[b] = im[a] - ti;
                    re[a] += tr; im[a] += ti;
                    const float nr = cr * wr - ci * wi;
                    ci = cr * wi + ci * wr; cr = nr;
                }
            }
        }
    }

    static float toDb(float x) { return 20.0f * std::log10(std::max(x, 1e-6f)); }

    float values_[kParamCount];
    DrumHits drums_;
    Snapshot snap_;

    float ringL_[kRing] = {}, ringR_[kRing] = {};
    std::atomic<uint32_t> write_ { 0 };
    float re_[kFft] = {}, im_[kFft] = {};

    std::mutex threadMu_, wakeMu_;
    std::condition_variable wake_;
    bool stop_ = false;
    std::thread worker_;
    zay_socket sock_ = kNoSocket;
#ifdef _WIN32
    bool wsa_ = false;
#endif

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZayONatorPlugin)
};

Plugin* createPlugin() { return new ZayONatorPlugin(); }

// for the screen
const Snapshot* zayGetSnapshot(void* plugin) { return &static_cast<ZayONatorPlugin*>(plugin)->snapshot(); }

END_NAMESPACE_DISTRHO
