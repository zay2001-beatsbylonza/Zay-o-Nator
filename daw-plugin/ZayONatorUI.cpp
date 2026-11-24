// ZaY-o-Nator for your DAW (VST3 and AU): the screen. By BeatsbyLonza. MIT licence.
//
// The same look as the ZaY-o-Nator Display and the MPC plugin: a cream (Classic) and
// a black (Noir) backlit VU meter with LED peak strips beside them, 32 spectrum bars
// colored by height, and five buttons along the bottom (click to step through):
// NAME (what the display calls this track), 0 VU, LEFT METER, RIGHT METER and SEND.
// Everything is drawn in a 1000 x 620 design space and scaled to the window, so the
// window can be any size (it keeps its shape).

#include "DistrhoUI.hpp"
#include "ZayONatorShared.hpp"
#include "FontSerifItalic.hpp"

#include <cmath>
#include <cstdio>

START_NAMESPACE_DISTRHO

using namespace zay;
using DGL_NAMESPACE::Color;

const Snapshot* zayGetSnapshot(void* plugin);

static constexpr float W = 1000.0f, H = 620.0f;
static constexpr float kPi = 3.14159265358979f;
static constexpr int kLeds = 24;

struct Look { Color f0, f1, f2, f3, glow, ink, red, needle; };
static Look look(int i)
{
    if (i == 1)
        return { Color(0x2a, 0x22, 0x16), Color(0x17, 0x13, 0x0d), Color(0x0d, 0x0b, 0x08), Color(0x05, 0x04, 0x03),
                 Color(0xff, 0xb5, 0x47), Color(0xff, 0xc3, 0x6b), Color(0xff, 0x4b, 0x3e), Color(0xff, 0x7a, 0x3d) };
    return { Color(0xff, 0xf4, 0xcf), Color(0xf1, 0xd9, 0x95), Color(0xc9, 0xa2, 0x59), Color(0x8a, 0x6a, 0x32),
             Color(0xf1, 0xd9, 0x95), Color(0x2b, 0x24, 0x18), Color(0xb3, 0x26, 0x1e), Color(0x15, 0x15, 0x15) };
}
static Color withAlpha(Color c, float a) { c.alpha = a; return c; }
static const Color kGold(0xe8, 0xc7, 0x7a), kGray(0x8d, 0x94, 0x9e);
static Color ledOn(int i) { return i < 16 ? Color(0x2f, 0xd3, 0x6b) : i < 21 ? Color(0xff, 0xd2, 0x3f) : Color(0xff, 0x3b, 0x30); }
static Color ledOff(int i) { return i < 16 ? Color(0x0f, 0x3a, 0x1f) : i < 21 ? Color(0x3d, 0x33, 0x10) : Color(0x3d, 0x12, 0x10); }

class ZayONatorUI : public UI
{
public:
    ZayONatorUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT, true)
    {
        loadSharedResources();
        sans_ = findFont(NANOVG_DEJAVU_SANS_TTF);
        serif_ = createFontFromMemory("zay-serif", kFontSerifItalic, kFontSerifItalicSize, false);
        if (serif_ < 0) serif_ = sans_;
        values_[kName] = 0; values_[kSend] = 1; values_[kRef] = 2; values_[kLookL] = 0; values_[kLookR] = 1;
        for (float& b : bars_) b = 0.0f;
        snap_ = zayGetSnapshot(getPluginInstancePointer());
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index < kParamCount) { values_[index] = (int)std::lround(value); repaint(); }
    }

    // ~30 times a second: move the needles, LEDs and bars toward the newest reading
    void uiIdle() override
    {
        if (!snap_) return;
        const double dt = 1.0 / 30.0;
        const float k = 1.0f - (float)std::exp(-dt / 0.3);   // VU swing, ~300 ms like a real meter
        for (int c = 0; c < 2; ++c)
        {
            vu_[c] += (snap_->rmsDb[c].load() - vu_[c]) * k;
            const float p = snap_->peakDb[c].load();
            peak_[c] = p > peak_[c] ? p : std::fmax(p, peak_[c] - 26.0f * (float)dt);
        }
        for (int b = 0; b < kBands; ++b)
        {
            const float h = std::fmin(1.0f, std::fmax(0.0f, (snap_->bandDb[b].load() + 90.0f) / 80.0f));
            bars_[b] = h > bars_[b] ? h : std::fmax(h, bars_[b] - 1.8f * (float)dt);
        }
        repaint();
    }

    void onNanoDisplay() override
    {
        const float s = std::fmin(getWidth() / W, getHeight() / H);
        save();
        translate((getWidth() - W * s) / 2.0f, (getHeight() - H * s) / 2.0f);
        scale(s, s);
        drawBackground();
        drawHeader();
        drawCell(20, 76, 0);
        drawCell(510, 76, 1);
        drawSpectrum(20, 392, 960, 128);
        drawButtons(20, 536);
        restore();
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (!ev.press || ev.button != 1) return false;
        const float s = std::fmin(getWidth() / W, getHeight() / H);
        const float x = ((float)ev.pos.getX() - (getWidth() - W * s) / 2.0f) / s;
        const float y = ((float)ev.pos.getY() - (getHeight() - H * s) / 2.0f) / s;
        if (y < 536 || y > 604) return false;
        const int i = (int)((x - 20) / 192.0f);
        if (x < 20 || i < 0 || i > 4) return false;
        static const uint32_t order[5] = { kName, kRef, kLookL, kLookR, kSend };
        static const int counts[5] = { kNames, 3, 2, 2, 2 };
        const uint32_t p = order[i];
        const int v = (values_[p] + 1) % counts[i];
        values_[p] = v;
        editParameter(p, true);
        setParameterValue(p, (float)v);
        editParameter(p, false);
        repaint();
        return true;
    }

private:
    void drawBackground()
    {
        beginPath();
        rect(0, 0, W, H);
        fillPaint(radialGradient(W / 2, 200, 40, 720, Color(0x1c, 0x1f, 0x25), Color(0x05, 0x05, 0x06)));
        fill();
    }

    void drawHeader()
    {
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fontFaceId(serif_);
        fontSize(44);
        fontBlur(10);
        fillColor(Color(0xe8, 0xc7, 0x7a, 0.55f));
        text(W / 2, 38, "ZaY-o-Nator", nullptr);
        fontBlur(0);
        fillColor(kGold);
        text(W / 2, 38, "ZaY-o-Nator", nullptr);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        fontSize(20);
        fillColor(Color(0xd7, 0xdb, 0xe1));
        text(W - 24, 40, "by BeatsbyLonza", nullptr);

        char status[96];
        const int st = snap_ ? snap_->sendState.load() : 0;
        const char* name = kNameLabels[values_[kName] % kNames];
        if (values_[kSend] == 0) std::snprintf(status, sizeof status, "Send is off: meters here only");
        else if (st == 2) std::snprintf(status, sizeof status, "Can't send to the display");
        else std::snprintf(status, sizeof status, "Sending to display as DAW %s", name);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fontFaceId(sans_);
        fontSize(13);
        fillColor(kGray);
        text(24, 40, status, nullptr);
    }

    // one meter cell: LED strip on the outside, VU meter inside
    void drawCell(float x, float y, int c)
    {
        beginPath();
        roundedRect(x, y, 470, 300, 14);
        fillColor(Color(0, 0, 0));
        fill();
        strokeColor(Color(0x2a, 0x2e, 0x34));
        strokeWidth(2);
        stroke();
        const float ledX = c == 0 ? x + 14 : x + 470 - 14 - 26;
        drawLeds(ledX, y + 24, 26, 252, peak_[c]);
        drawVu(c == 0 ? x + 50 : x + 20, y + 24, 400.0f / 300.0f, c);
    }

    void drawLeds(float x, float y, float w, float h, float peakDb)
    {
        const int lit = (int)std::lround(std::fmin(1.0f, std::fmax(0.0f, (peakDb + 48.0f) / 48.0f)) * kLeds);
        const float seg = h / kLeds;
        for (int i = 0; i < kLeds; ++i)
        {
            const float yy = y + h - (i + 1) * seg;
            beginPath();
            roundedRect(x + 3, yy + 1.5f, w - 6, seg - 3, 2);
            fillColor(i < lit ? ledOn(i) : ledOff(i));
            fill();
        }
    }

    // the VU meter, drawn in its 300 x 190 design units
    void drawVu(float x, float y, float k, int c)
    {
        const Look L = look(values_[c ? kLookR : kLookL]);
        save();
        translate(x, y);
        scale(k, k);
        // bezel
        beginPath();
        roundedRect(2, 2, 296, 186, 10);
        fillPaint(linearGradient(0, 2, 0, 188, Color(0x4a, 0x4f, 0x57), Color(0x24, 0x27, 0x2c)));
        fill();
        // backlight glow and face
        beginPath();
        roundedRect(14, 14, 272, 162, 5);
        fillPaint(boxGradient(14, 14, 272, 162, 5, 18, withAlpha(L.glow, 0.55f), withAlpha(L.glow, 0.0f)));
        fill();
        beginPath();
        roundedRect(14, 14, 272, 162, 5);
        fillPaint(radialGradient(150, 140, 10, 200, L.f0, L.f2));
        fill();
        beginPath();
        roundedRect(14, 14, 272, 162, 5);
        fillPaint(radialGradient(150, 140, 150, 260, withAlpha(L.f2, 0.0f), L.f3));
        fill();
        // scale
        const float cx = 150, cy = 182;
        auto pt = [&](float a, float r, float& px, float& py) {
            const float t = a * kPi / 180.0f; px = cx + r * std::sin(t); py = cy - r * std::cos(t);
        };
        arcPath(cx, cy, 118, -48, 18); strokeColor(L.ink); strokeWidth(1.6f); stroke();
        arcPath(cx, cy, 121, 18, 47); strokeColor(L.red); strokeWidth(7); stroke();
        fontFaceId(sans_);
        fontSize(11);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        static const char* labels[11] = { "20", "10", "7", "5", "3", "2", "1", "0", "1", "2", "3" };
        for (int i = 0; i < 11; ++i)
        {
            const Color col = kMarks[i][0] > 0 ? L.red : L.ink;
            float x1, y1, x2, y2, tx, ty;
            pt(kMarks[i][1], 118, x1, y1); pt(kMarks[i][1], 130, x2, y2); pt(kMarks[i][1], 142, tx, ty);
            beginPath(); moveTo(x1, y1); lineTo(x2, y2); strokeColor(col); strokeWidth(2); stroke();
            fillColor(col); text(tx, ty, labels[i], nullptr);
        }
        for (int a = -48; a <= 18; a += 4)
        {
            float x1, y1, x2, y2;
            pt((float)a, 118, x1, y1); pt((float)a, 124, x2, y2);
            beginPath(); moveTo(x1, y1); lineTo(x2, y2); strokeColor(L.ink); strokeWidth(1); stroke();
        }
        fontSize(14); fillColor(L.ink); text(34, 108, "-", nullptr);
        fillColor(L.red); text(262, 108, "+", nullptr);
        fontSize(12); fillColor(L.ink); text(262, 160, "VU", nullptr); text(34, 160, c ? "R" : "L", nullptr);
        fontFaceId(serif_); fontSize(17); text(150, 122, "ZaY-o-Nator", nullptr);
        // needle
        const float a = vuAngle(vu_[c] + kRefDb[values_[kRef] % 3]);
        float x0, y0, x1, y1;
        pt(a, 16, x0, y0); pt(a, 124, x1, y1);
        beginPath(); moveTo(x0, y0); lineTo(x1, y1);
        strokeColor(L.needle); strokeWidth(1.8f); lineCap(ROUND); stroke();
        // pivot cap, glass
        beginPath(); ellipse(150, 176, 42, 30); fillColor(Color(0x14, 0x14, 0x14)); fill();
        beginPath(); rect(14, 176, 272, 14); fillPaint(linearGradient(0, 176, 0, 188, Color(0x2c, 0x30, 0x36), Color(0x24, 0x27, 0x2c))); fill();
        beginPath(); moveTo(14, 14); lineTo(200, 14); lineTo(90, 176); lineTo(14, 176); closePath();
        fillColor(Color(255, 255, 255, 0.10f)); fill();
        beginPath(); roundedRect(14, 14, 272, 162, 5); strokeColor(Color(0, 0, 0, 0.45f)); strokeWidth(3); stroke();
        restore();
    }

    void arcPath(float cx, float cy, float r, float a0, float a1)
    {
        beginPath();
        for (int i = 0; i <= 40; ++i)
        {
            const float t = (a0 + (a1 - a0) * i / 40.0f) * kPi / 180.0f;
            const float px = cx + r * std::sin(t), py = cy - r * std::cos(t);
            if (i == 0) moveTo(px, py); else lineTo(px, py);
        }
    }

    void drawSpectrum(float x, float y, float w, float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 12);
        fillPaint(linearGradient(0, y, 0, y + h, Color(0x0b, 0x1a, 0x26), Color(0x06, 0x11, 0x1a)));
        fill();
        strokeColor(Color(0x1f, 0x3a, 0x4d)); strokeWidth(2); stroke();
        strokeColor(Color(0x1d, 0x3b, 0x50)); strokeWidth(1);
        for (int i = 1; i < 4; ++i) { beginPath(); moveTo(x, y + h * i / 4); lineTo(x + w, y + h * i / 4); stroke(); }
        for (int i = 1; i < 10; ++i) { beginPath(); moveTo(x + w * i / 10, y); lineTo(x + w * i / 10, y + h); stroke(); }
        const float step = (w - 24) / kBands, bw = step - 5, bh = h - 16, seg = bh / 16.0f;
        for (int b = 0; b < kBands; ++b)
        {
            const int lit = (int)std::lround(bars_[b] * 16.0f);
            for (int i = 0; i < lit; ++i)
            {
                beginPath();
                rect(x + 12 + b * step, y + 8 + bh - (i + 1) * seg + 1, bw, seg - 2);
                fillColor(i < 10 ? Color(0x2f, 0xd3, 0x6b) : i < 13 ? Color(0xff, 0xd2, 0x3f) : Color(0xff, 0x3b, 0x30));
                fill();
            }
        }
    }

    void drawButtons(float x, float y)
    {
        static const char* titles[5] = { "NAME", "0 VU", "LEFT METER", "RIGHT METER", "SEND" };
        const char* vals[5] = { kNameLabels[values_[kName] % kNames], kRefLabels[values_[kRef] % 3],
                                kLookLabels[values_[kLookL] % 2], kLookLabels[values_[kLookR] % 2], values_[kSend] ? "On" : "Off" };
        for (int i = 0; i < 5; ++i)
        {
            const float bx = x + i * 192;
            beginPath();
            roundedRect(bx, y, 184, 68, 10);
            fillColor(Color(0x16, 0x18, 0x1c));
            fill();
            strokeColor(i == 4 && values_[kSend] ? kGold : Color(0x3a, 0x3f, 0x47));
            strokeWidth(1.5f);
            stroke();
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fontFaceId(sans_);
            fontSize(12); fillColor(kGray); text(bx + 16, y + 22, titles[i], nullptr);
            fontSize(19); fillColor(kGold); text(bx + 16, y + 46, vals[i], nullptr);
        }
    }

    const Snapshot* snap_ = nullptr;
    FontId sans_ = -1, serif_ = -1;
    int values_[kParamCount] = {};
    float vu_[2] = { -60.0f, -60.0f }, peak_[2] = { -120.0f, -120.0f }, bars_[kBands];

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZayONatorUI)
};

UI* createUI() { return new ZayONatorUI(); }

END_NAMESPACE_DISTRHO
