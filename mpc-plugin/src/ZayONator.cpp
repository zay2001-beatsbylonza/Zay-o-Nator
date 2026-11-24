// ZaY-o-Nator for MPC - backlit VU meters, LED peak meters and a spectrum on the
// MPC's own screen, sent over Wi-Fi to the ZaY-o-Nator Display on your Mac.
// By BeatsbyLonza. Built on Akai's MPC Plugin SDK (MIT).
//
// Signal flow: input -> Output gain -> output. The sound passes straight through
// (Output is 0 dB by default), so it can sit on any track or on the master.
//
// The meters:
//   process() (audio thread) copies each block into two small ring buffers and
//   does nothing else. A worker thread wakes ~25 times a second, reads the newest
//   samples and works out:
//     - the VU needles (RMS, with a real VU meter's ~300 ms swing; 0 VU = -10 dBFS
//       by default, so loud beats swing around 0 instead of pinning the needle),
//     - the LED peak meters (instant up, falling ~26 dB a second),
//     - 32 spectrum bars (1024-point FFT, bass on the left).
//   It moves the screen's needles, LEDs and bars (screen-only parameters that pick
//   a frame of a film strip in the skin), and, when "Send" is on, sends one small
//   UDP packet to the display:  ZAYO1|<this MPC>|peakL|peakR|rmsL|rmsR|b1,...,b32
//   It broadcasts on port 47911, or sends to the Mac's address written in
//   "ZaY-o-Nator display.txt" at the top of the drive when broadcasts are blocked.
//
// Parameter order is FROZEN once released (saved projects store it).
// Append new parameters at the end only.

#include "mpc_plugin.h"
#include "mpc_resources.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

namespace {

enum ParamIndex
{
    kOutput = 0,   // dB. Param 0 is nudged by the host on load: a plain gain is harmless.
    kSend,         // send the meters to the ZaY-o-Nator Display over Wi-Fi
    kLookL,        // left meter face: Classic (cream) or Noir (black)
    kLookR,        // right meter face
    kRef,          // the level that reads 0 VU: -18, -14 or -10 dBFS (loud beats swing best at -10)
    kVuL,          // screen only: film strip frame of the left needle (+ kVuFrames for Noir)
    kVuR,
    kPeakL,        // screen only: how many LEDs are lit (0..24)
    kPeakR,
    kStatus,       // readout: what the plugin is doing (a change counter; the text is the display)
    kBand0,        // screen only: first of kBands spectrum bars (height 0..16)
    kNumParams = kBand0 + 32
};

constexpr int kBands     = 32;
constexpr int kBandSteps = 16;
constexpr int kVuFrames  = 49;     // needle positions per face
constexpr int kLeds      = 24;
constexpr int kFft       = 1024;
constexpr int kRing      = 8192;   // per channel, a power of two
constexpr int kPort      = 47911;
constexpr float kPi      = 3.14159265358979f;

// VU scale (VU value, needle angle in degrees), the same as the display and the skin
const float kMarks[][2] = { {-20,-48}, {-10,-30}, {-7,-20}, {-5,-12}, {-3,-2}, {-2,5}, {-1,11}, {0,18}, {1,28}, {2,37}, {3,47} };

float vuAngle(float vu)
{
    if (vu <= kMarks[0][0]) return std::max(-52.0f, kMarks[0][1] + (vu - kMarks[0][0]) * 0.6f);
    for (size_t i = 1; i < sizeof kMarks / sizeof kMarks[0]; ++i)
        if (vu <= kMarks[i][0])
            return kMarks[i - 1][1] + (kMarks[i][1] - kMarks[i - 1][1]) * (vu - kMarks[i - 1][0]) / (kMarks[i][0] - kMarks[i - 1][0]);
    return std::min(52.0f, 47.0f + (vu - 3.0f) * 4.0f);
}

// This MPC's name for the display: its hostname, cleaned.
std::string deviceName()
{
    char host[128] = {0};
    gethostname(host, sizeof host - 1);
    std::string out;
    for (const char* p = host; *p && out.size() < 30; ++p)
        if (std::isalnum((unsigned char)*p) || *p == ' ' || *p == '-' || *p == '_' || *p == '.') out += *p;
    return out.empty() ? std::string("MPC") : out;
}

std::string readFirstLine(const std::string& path)
{
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return std::string();
    char buf[128] = {0};
    if (!std::fgets(buf, sizeof buf, f)) buf[0] = 0;
    std::fclose(f);
    std::string s = buf;
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
    while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(0, 1);
    return s;
}

std::string parentOf(const std::string& p)
{
    const size_t s = p.rfind('/');
    return (s == std::string::npos || s == 0) ? std::string() : p.substr(0, s);
}

} // namespace

class ZayONator : public mpc::Plugin
{
public:
    ZayONator() : mpc::Plugin(makeInfo())
    {
        addParam(mpc::floatParam("output", "Output", -24.0f, 12.0f, 0.0f, "dB", 1));
        addParam(mpc::boolParam("send", "Send", true));
        addParam(mpc::choiceParam("lookl", "Left Meter", { "Classic", "Noir" }, 0));
        addParam(mpc::choiceParam("lookr", "Right Meter", { "Classic", "Noir" }, 1));
        addParam(mpc::choiceParam("ref", "0 VU", { "-18 dB", "-14 dB", "-10 dB" }, 2));
        screenParam("vul", "VU L", 2 * kVuFrames - 1);
        screenParam("vur", "VU R", 2 * kVuFrames - 1);
        screenParam("peakl", "Peak L", kLeds);
        screenParam("peakr", "Peak R", kLeds);
        screenParam("status", "Status", 1000);
        for (int b = 0; b < kBands; ++b)
        {
            char id[16], name[16];
            std::snprintf(id, sizeof id, "band%02d", b);
            std::snprintf(name, sizeof name, "Band %02d", b + 1);
            screenParam(id, name, kBandSteps);
        }
        name_ = deviceName();
        status_ = "Starting...";
    }

    ~ZayONator() override
    {
        {
            std::lock_guard<std::mutex> lk(wakeMu_);
            stop_ = true;
        }
        wake_.notify_all();
        if (worker_.joinable()) worker_.join();
        if (sock_ >= 0) close(sock_);
    }

    void prepare(double sampleRate, int) override
    {
        sampleRate_.store(sampleRate > 1000.0 ? sampleRate : 44100.0);
        gain_ = std::pow(10.0f, get(kOutput) / 20.0f);
        if (!worker_.joinable()) worker_ = std::thread([this] { run(); });
    }

    // Real-time safe: a gain, and a copy into the meters' ring buffers.
    void process(mpc::ProcessContext& ctx) override
    {
        const float target = std::pow(10.0f, get(kOutput) / 20.0f);
        const int nOut = mpc::clamp(ctx.numOutputs, 0, 2);
        uint32_t w = write_.load(std::memory_order_relaxed);
        for (int i = 0; i < ctx.numFrames; ++i)
        {
            gain_ += 0.002f * (target - gain_);
            float s[2] = { 0.0f, 0.0f };
            for (int ch = 0; ch < 2; ++ch)
            {
                const int src = (ctx.numInputs > 1) ? ch : 0;
                if (ctx.inputs && src < ctx.numInputs && ctx.inputs[src]) s[ch] = ctx.inputs[src][i] * gain_;
            }
            for (int ch = 0; ch < nOut; ++ch) ctx.outputs[ch][i] = s[ch];
            ringL_[w & (kRing - 1)] = s[0];
            ringR_[w & (kRing - 1)] = s[1];
            ++w;
        }
        write_.store(w, std::memory_order_release);
    }

    void parameterChanged(int index, float) override
    {
        if (selfSet_) return;
        if (index == kSend) bump(kStatus);
    }

    bool formatValue(int index, float, std::string& out) override
    {
        if (index != kStatus) return false;
        std::lock_guard<std::mutex> lk(textMu_);
        out = status_;
        return true;
    }

private:
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo info;
        info.name         = "ZaY-o-Nator";
        info.vendor       = "BeatsbyLonza";   // must match plugin.json "manufacturer"
        info.uniqueId     = MPC_FOURCC('Z', 'y', 'N', '1');
        info.version      = 1000;
        info.isInstrument = false;
        info.numInputs    = 2;
        info.numOutputs   = 2;
        info.tailSamples  = 0;
        return info;
    }

    void screenParam(const char* id, const char* name, int max)
    {
        mpc::Param p = mpc::intParam(id, name, 0, max, 0);
        p.automatable = false;
        addParam(p);
    }

    // ---------------- worker thread ----------------

    void run()
    {
        const std::string folder = mpc::detail::computePluginFolder();
        std::string drive = parentOf(folder);
        const std::string top = drive.empty() ? std::string() : drive.substr(drive.rfind('/') + 1);
        if (top == "VST2 Plugins" || top == "Synths") drive = parentOf(drive);
        addrFiles_[0] = folder + "/ZaY-o-Nator display.txt";
        addrFiles_[1] = drive.empty() ? std::string() : drive + "/ZaY-o-Nator display.txt";

        openSocket();
        uint32_t read = write_.load(std::memory_order_acquire);
        auto last = std::chrono::steady_clock::now(), lastAddr = last - std::chrono::seconds(10);
        float vu[2] = { -60.0f, -60.0f }, pk[2] = { -120.0f, -120.0f };
        float bars[kBands] = {};

        while (!sleepFor(40))
        {
            const auto now = std::chrono::steady_clock::now();
            const float dt = std::min(0.2f, std::chrono::duration<float>(now - last).count());
            last = now;
            if (now - lastAddr > std::chrono::seconds(5)) { lastAddr = now; findDisplay(); }

            // the samples that came in since last time (at most one ring)
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
            float rmsDb[2], peakDb[2];
            for (int c = 0; c < 2; ++c)
            {
                rmsDb[c] = n ? toDb(std::sqrt(sum[c] / (float)n)) : -120.0f;
                peakDb[c] = toDb(peak[c]);
                vu[c] += (rmsDb[c] - vu[c]) * (1.0f - std::exp(-dt / 0.3f));          // VU ballistics
                pk[c] = peakDb[c] > pk[c] ? peakDb[c] : std::max(peakDb[c], pk[c] - 26.0f * dt);
            }
            float bandDb[kBands];
            spectrum(w, bandDb);
            for (int b = 0; b < kBands; ++b)
            {
                const float h = mpc::clamp((bandDb[b] + 90.0f) / 80.0f, 0.0f, 1.0f) * kBandSteps;
                bars[b] = h > bars[b] ? h : std::max(h, bars[b] - 30.0f * dt);
            }

            // the MPC screen: only values that changed are sent
            for (int c = 0; c < 2; ++c)
            {
                const float ref = getInt(kRef) == 0 ? 18.0f : getInt(kRef) == 1 ? 14.0f : 10.0f;
                const float a = vuAngle(vu[c] + ref);                               // -52..52 degrees
                int f = (int)std::lround((a + 52.0f) / 104.0f * (kVuFrames - 1));
                f = mpc::clamp(f, 0, kVuFrames - 1) + (getInt(c ? kLookR : kLookL) ? kVuFrames : 0);
                show(c ? kVuR : kVuL, f);
                const int lit = mpc::clamp((int)std::lround((pk[c] + 48.0f) / 48.0f * kLeds), 0, kLeds);
                show(c ? kPeakR : kPeakL, lit);
            }
            for (int b = 0; b < kBands; ++b) show(kBand0 + b, (int)std::lround(bars[b]));

            if (getBool(kSend)) sendFrame(peakDb, rmsDb, bandDb);
            else setStatus("Send is off: MPC screen only");
        }
    }

    // 32 bands from 30 Hz to 18 kHz, in dB (0 = full scale), tilted so the highs show
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
        const float sr = (float)sampleRate_.load(), binHz = sr / kFft, norm = 4.0f / kFft;
        for (int b = 0; b < kBands; ++b)
        {
            const float f0 = 30.0f * std::pow(600.0f, (float)b / kBands), f1 = 30.0f * std::pow(600.0f, (float)(b + 1) / kBands);
            int k0 = std::max(1, (int)(f0 / binHz)), k1 = std::min(kFft / 2, std::max(k0 + 1, (int)(f1 / binHz) + 1));
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

    // ---------------- network ----------------

    void openSocket()
    {
        sock_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock_ < 0) { setStatus("No network"); return; }
        int yes = 1;
        setsockopt(sock_, SOL_SOCKET, SO_BROADCAST, &yes, sizeof yes);
    }

    // The display's address: from "ZaY-o-Nator display.txt" if there is one, else broadcast.
    void findDisplay()
    {
        std::string manual;
        for (const std::string& f : addrFiles_)
            if (!f.empty() && manual.empty()) manual = readFirstLine(f);
        std::memset(&dest_, 0, sizeof dest_);
        dest_.sin_family = AF_INET;
        dest_.sin_port = htons(kPort);
        const std::string host = manual.substr(0, manual.find(':'));
        if (!host.empty() && inet_pton(AF_INET, host.c_str(), &dest_.sin_addr) == 1) { target_ = host; return; }
        dest_.sin_addr.s_addr = htonl(INADDR_BROADCAST);
        target_.clear();
    }

    void sendFrame(const float* peakDb, const float* rmsDb, const float* bandDb)
    {
        if (sock_ < 0) return;
        char buf[1024];
        int len = std::snprintf(buf, sizeof buf, "ZAYO1|%s|%.1f|%.1f|%.1f|%.1f|", name_.c_str(),
                                peakDb[0], peakDb[1], rmsDb[0], rmsDb[1]);
        for (int b = 0; b < kBands && len < (int)sizeof buf - 16; ++b)
            len += std::snprintf(buf + len, sizeof buf - len, b ? ",%.1f" : "%.1f", std::max(-120.0f, bandDb[b]));
        const bool ok = sendto(sock_, buf, (size_t)len, 0, (const sockaddr*)&dest_, sizeof dest_) == len;
        if (!ok) setStatus("Can't send. Wi-Fi on?");
        else setStatus(target_.empty() ? "Sending to display as " + name_ : "Sending to " + target_ + " as " + name_);
    }

    // ---------------- screen ----------------

    void show(int index, int frame)
    {
        if (getInt(index) != frame) setSelf(index, (float)frame);
    }

    void setSelf(int index, float plain)
    {
        selfSet_ = true;
        set(index, plain);
        selfSet_ = false;
        notifyHostParameterChanged(index);
    }

    void setStatus(const std::string& s)
    {
        {
            std::lock_guard<std::mutex> lk(textMu_);
            if (status_ == s) return;
            status_ = s;
        }
        bump(kStatus);
    }

    // The MPC only re-reads a readout's text when its value changes, so step a counter.
    void bump(int index) { setSelf(index, (float)((getInt(index) + 1) % 1000)); }

    bool sleepFor(int ms)
    {
        std::unique_lock<std::mutex> lk(wakeMu_);
        wake_.wait_for(lk, std::chrono::milliseconds(ms), [this] { return stop_.load(); });
        return stop_.load();
    }

    std::thread worker_;
    std::mutex wakeMu_;
    std::condition_variable wake_;
    std::atomic<bool> stop_{false}, selfSet_{false};
    std::atomic<double> sampleRate_{44100.0};
    float gain_ = 1.0f;

    float ringL_[kRing] = {}, ringR_[kRing] = {};
    std::atomic<uint32_t> write_{0};
    float re_[kFft] = {}, im_[kFft] = {};

    int sock_ = -1;
    sockaddr_in dest_{};
    std::string addrFiles_[2], target_, name_;

    std::mutex textMu_;
    std::string status_;
};

MPC_PLUGIN_ENTRY(ZayONator)
