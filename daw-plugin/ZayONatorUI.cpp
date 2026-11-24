// ZaY-o-Nator for your DAW (VST3 and AU): the screen. By BeatsbyLonza. MIT licence.
//
// The same look as the ZaY-o-Nator Display and the MPC plugin, with seven faces (FACE
// button, bottom left): DUO (two VUs over the spectrum), SPECTRUM, NEEDLES (big VUs,
// black meter first), CONSOLE (LED towers and bars), DRUMS (pads that flash on hits),
// MASTER (vectorscope, peak, RMS, correlation) and SYNTHS (scope in a spectrum ring).
// The other buttons along the bottom (click to step through): NAME (what the display
// calls this track), 0 VU, LEFT METER, RIGHT METER and SEND.
// Everything is drawn in a 1000 x 620 design space and scaled to the window, so the
// window can be any size (it keeps its shape).

#include "DistrhoUI.hpp"
#include "ZayONatorShared.hpp"
#include "FontSerifItalic.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

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
        values_[kName] = 0; values_[kSend] = 1; values_[kRef] = 2; values_[kLookL] = 0; values_[kLookR] = 1; values_[kFace] = 0;
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
        // DRUMS: a hit is a lane louder than its own recent average
        for (Lane& l : lanes_)
        {
            float e = -140.0f;
            for (int b = l.b0; b <= l.b1; ++b) e = std::fmax(e, snap_->bandDb[b].load());
            l.cool -= (float)dt;
            if (e - l.avg > 5.0f && e > -75.0f && l.cool <= 0.0f)
            {
                l.env = 1.0f; l.cool = 0.09f;
                Pad& pad = pads_[l.row * 4 + std::rand() % 4];
                pad.v = 1.0f; pad.colour = l.colour;
            }
            l.avg += (e - l.avg) * std::fmin(1.0f, (float)dt * 4.0f);
            l.env = std::fmax(0.0f, l.env - 3.5f * (float)dt);
        }
        for (Pad& pad : pads_) pad.v = std::fmax(0.0f, pad.v - 2.6f * (float)dt);
        // MASTER: smoothed numbers and stereo correlation
        float sLR = 0, sLL = 0, sRR = 0;
        for (int i = 0; i < kWave; ++i)
        {
            const float l = snap_->wave[0][i].load(), r = snap_->wave[1][i].load();
            sLR += l * r; sLL += l * l; sRR += r * r;
        }
        const float c = (sLL > 1e-9f && sRR > 1e-9f) ? sLR / std::sqrt(sLL * sRR) : 1.0f;
        corr_ += (c - corr_) * std::fmin(1.0f, (float)dt * 4.0f);
        mPeak_ = std::fmax(std::fmax(snap_->peakDb[0].load(), snap_->peakDb[1].load()), mPeak_ - 12.0f * (float)dt);
        mRms_ += ((snap_->rmsDb[0].load() + snap_->rmsDb[1].load()) / 2.0f - mRms_) * std::fmin(1.0f, (float)dt * 1.5f);
        ringRot_ += 0.15f * (float)dt;
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
        switch (values_[kFace] % kFaces)
        {
        case 1:   // SPECTRUM: the spectrum fills the screen, LED strips on the sides
            drawLeds(20, 76, 30, 444, peak_[0]);
            drawLeds(950, 76, 30, 444, peak_[1]);
            drawSpectrum(62, 76, 876, 444, 32);
            break;
        case 2:   // NEEDLES: big VUs only, the black meter first
            drawBigCell(20, 0, values_[kLookR]);
            drawBigCell(510, 1, values_[kLookL]);
            break;
        case 3:   // CONSOLE: tall LED towers and bars
            drawTower(20, peak_[0]);
            drawTower(924, peak_[1]);
            drawSpectrum(92, 76, 816, 444, 48);
            break;
        case 4: drawDrums(); break;
        case 5: drawMaster(); break;
        case 6: drawSynths(); break;
        default:  // DUO
            drawCell(20, 76, 0);
            drawCell(510, 76, 1);
            drawSpectrum(20, 392, 960, 128, 32);
            break;
        }
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
        const int i = (int)((x - 20) / 160.0f);
        if (x < 20 || i < 0 || i > 5) return false;
        static const uint32_t order[6] = { kFace, kName, kRef, kLookL, kLookR, kSend };
        static const int counts[6] = { kFaces, kNames, 3, 2, 2, 2 };
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
        drawVu(c == 0 ? x + 50 : x + 20, y + 24, 400.0f / 300.0f, c, values_[c ? kLookR : kLookL]);
    }

    // NEEDLES: a full-height cell with a bigger VU
    void drawBigCell(float x, int c, int lookIdx)
    {
        beginPath();
        roundedRect(x, 76, 470, 444, 14);
        fillColor(Color(0, 0, 0));
        fill();
        strokeColor(Color(0x2a, 0x2e, 0x34)); strokeWidth(2); stroke();
        drawLeds(c == 0 ? x + 14 : x + 470 - 14 - 26, 100, 26, 396, peak_[c]);
        const float k = 400.0f / 300.0f;
        drawVu(c == 0 ? x + 50 : x + 20, 76 + (444 - 190 * k) / 2, k, c, lookIdx);
    }

    // CONSOLE: a wide LED tower
    void drawTower(float x, float peakDb)
    {
        beginPath();
        roundedRect(x, 76, 56, 444, 10);
        fillColor(Color(0, 0, 0)); fill();
        strokeColor(Color(0x2a, 0x2e, 0x34)); strokeWidth(2); stroke();
        drawLeds(x + 8, 88, 40, 420, peakDb);
    }

    // DRUMS: a kick ring that pumps, and MPC-style pads that flash on kicks, snares, percussion and hats
    void drawDrums()
    {
        panel(20, 76, 960, 444);
        const float cx = 250, cy = 298, R = 170, e = lanes_[0].env;
        beginPath(); circle(cx, cy, R); strokeColor(Color(0x2a, 0x0b, 0x09)); strokeWidth(14); stroke();
        beginPath(); circle(cx, cy, R * (0.82f + 0.18f * e));
        strokeColor(Color(0xff, 0x3b, 0x30, 0.35f + 0.65f * e)); strokeWidth(14 + 16 * e); stroke();
        beginPath(); circle(cx, cy, R * 0.62f * (0.85f + 0.15f * e));
        fillColor(Color(0xff, 0x3b, 0x30, 0.08f + 0.3f * e)); fill();
        fontFaceId(sans_); fontSize(30); textAlign(ALIGN_CENTER | ALIGN_MIDDLE); fillColor(kGold);
        text(cx, cy, "KICK", nullptr);
        const float gx = 560, gy = 96, pw = 88, gap = 16;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
            {
                const Pad& pad = pads_[r * 4 + c];
                const float x = gx + c * (pw + gap), y = gy + r * (pw + gap);
                beginPath(); roundedRect(x, y, pw, pw, 8); fillColor(Color(0x0d, 0x0e, 0x10)); fill();
                if (pad.v > 0.01f)
                {
                    beginPath(); roundedRect(x - 6, y - 6, pw + 12, pw + 12, 12);
                    fillPaint(boxGradient(x, y, pw, pw, 8, 14, withAlpha(pad.colour, 0.6f * pad.v), withAlpha(pad.colour, 0.0f))); fill();
                    beginPath(); roundedRect(x, y, pw, pw, 8); fillColor(withAlpha(pad.colour, 0.25f + 0.75f * pad.v)); fill();
                }
                beginPath(); roundedRect(x, y, pw, pw, 8);
                strokeColor(pad.v > 0.01f ? pad.colour : Color(0x2c, 0x30, 0x36)); strokeWidth(2); stroke();
            }
        fontSize(14); textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        for (const Lane& l : lanes_) { fillColor(l.colour); text(gx - 14, gy + l.row * (pw + gap) + pw / 2, l.name, nullptr); }
    }

    // MASTER: a stereo vectorscope, big peak and RMS, correlation and L/R bars
    void drawMaster()
    {
        panel(20, 76, 960, 444);
        const float cx = 250, cy = 298, k = 190;
        strokeColor(Color(0x1d, 0x3b, 0x50)); strokeWidth(1);
        beginPath(); circle(cx, cy, k); stroke();
        beginPath(); moveTo(cx - k, cy); lineTo(cx + k, cy); moveTo(cx, cy - k); lineTo(cx, cy + k);
        moveTo(cx - k * .7f, cy - k * .7f); lineTo(cx + k * .7f, cy + k * .7f); moveTo(cx + k * .7f, cy - k * .7f); lineTo(cx - k * .7f, cy + k * .7f); stroke();
        fillColor(withAlpha(kGold, 0.85f));
        for (int i = 0; i < kWave; ++i)
        {
            const float l = snap_ ? snap_->wave[0][i].load() : 0.0f, r = snap_ ? snap_->wave[1][i].load() : 0.0f;
            const float x = (r - l) * 0.707f * k * 1.4f, y = (l + r) * 0.707f * k * 1.4f;
            if (std::fabs(x) > k || std::fabs(y) > k) continue;
            beginPath(); rect(cx + x - 1, cy - y - 1, 2.4f, 2.4f); fill();
        }
        const float x0 = 520, w = 420;
        char buf[32];
        fontFaceId(sans_); textAlign(ALIGN_LEFT | ALIGN_BASELINE);
        fontSize(14); fillColor(kGray); text(x0, 118, "PEAK", nullptr);
        std::snprintf(buf, sizeof buf, mPeak_ > -100 ? "%.1f dB" : "-", mPeak_);
        fontSize(54); fillColor(mPeak_ > -1 ? Color(0xff, 0x3b, 0x30) : mPeak_ > -6 ? Color(0xff, 0xd2, 0x3f) : Color(0x2f, 0xd3, 0x6b));
        text(x0, 172, buf, nullptr);
        fontSize(14); fillColor(kGray); text(x0, 222, "RMS", nullptr);
        std::snprintf(buf, sizeof buf, mRms_ > -100 ? "%.1f dB" : "-", mRms_);
        fontSize(54); fillColor(Color(0xf1, 0xf3, 0xf6)); text(x0, 276, buf, nullptr);
        fontSize(14); fillColor(kGray); text(x0, 330, "STEREO CORRELATION", nullptr);
        beginPath(); roundedRect(x0, 342, w, 22, 5); fillColor(Color(0x16, 0x18, 0x1c)); fill();
        const float mid = x0 + w / 2, at = x0 + w * (corr_ + 1) / 2;
        beginPath(); rect(std::fmin(mid, at), 342, std::fabs(at - mid), 22);
        fillColor(corr_ < 0 ? Color(0xff, 0x3b, 0x30) : corr_ < 0.3f ? Color(0xff, 0xd2, 0x3f) : Color(0x2f, 0xd3, 0x6b)); fill();
        fontSize(12); fillColor(Color(0x6b, 0x72, 0x7c));
        text(x0, 382, "-1", nullptr); textAlign(ALIGN_CENTER | ALIGN_BASELINE); text(mid, 382, "0", nullptr);
        textAlign(ALIGN_RIGHT | ALIGN_BASELINE); text(x0 + w, 382, "+1", nullptr);
        for (int c = 0; c < 2; ++c)
        {
            const float y = 410 + c * 44, lv = std::fmin(1.0f, std::fmax(0.0f, (vu_[c] + 60.0f) / 60.0f));
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE); fontSize(14); fillColor(kGray); text(x0, y + 12, c ? "R" : "L", nullptr);
            beginPath(); roundedRect(x0 + 24, y, w - 24, 24, 5); fillColor(Color(0x16, 0x18, 0x1c)); fill();
            beginPath(); rect(x0 + 24, y, (w - 24) * lv, 24);
            fillColor(lv > 0.8f ? Color(0xff, 0x3b, 0x30) : lv > 0.6f ? Color(0xff, 0xd2, 0x3f) : Color(0x2f, 0xd3, 0x6b)); fill();
        }
    }

    // SYNTHS: a glowing waveform scope inside a turning ring of spectrum
    void drawSynths()
    {
        panel(20, 76, 960, 444);
        const float cx = 500, cy = 298, R = 120;
        lineCap(ROUND);
        for (int i = 0; i < 64; ++i)
        {
            const float h = barAt(i / 63.0f);
            const Color col = h > 0.8f ? Color(0xff, 0x3b, 0x30) : h > 0.6f ? Color(0xff, 0xd2, 0x3f) : Color(0x2f, 0xd3, 0x6b);
            for (int side = -1; side <= 1; side += 2)
            {
                const float a = ringRot_ + side * (i / 64.0f) * kPi - kPi / 2;
                const float r1 = R * 1.04f, r2 = R * (1.06f + 0.62f * h);
                beginPath(); moveTo(cx + r1 * std::cos(a), cy + r1 * std::sin(a)); lineTo(cx + r2 * std::cos(a), cy + r2 * std::sin(a));
                strokeColor(col); strokeWidth(5); stroke();
            }
        }
        for (int pass = 0; pass < 2; ++pass)
        {
            beginPath();
            for (int i = 0; i < kWave; ++i)
            {
                const float v = snap_ ? snap_->wave[0][i].load() : 0.0f;
                const float x = cx - R * 0.92f + (i / (float)(kWave - 1)) * R * 1.84f;
                const float y = cy - std::fmax(-1.0f, std::fmin(1.0f, v * 1.6f)) * R * 0.75f;
                if (i == 0) moveTo(x, y); else lineTo(x, y);
            }
            strokeColor(pass ? Color(0xee, 0xf5, 0xfb) : Color(0x7c, 0xc4, 0xef, 0.45f));
            strokeWidth(pass ? 1.6f : 7.0f);
            stroke();
        }
    }

    void panel(float x, float y, float w, float h)
    {
        beginPath(); roundedRect(x, y, w, h, 14); fillColor(Color(0, 0, 0)); fill();
        strokeColor(Color(0x2a, 0x2e, 0x34)); strokeWidth(2); stroke();
    }

    // the spectrum height at t (0 = bass, 1 = highs), between the 32 bands
    float barAt(float t) const
    {
        const float x = t * (kBands - 1);
        const int j = (int)x;
        const float f = x - j;
        return bars_[j] * (1 - f) + bars_[j < kBands - 1 ? j + 1 : j] * f;
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
    void drawVu(float x, float y, float k, int c, int lookIdx)
    {
        const Look L = look(lookIdx);
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

    void drawSpectrum(float x, float y, float w, float h, int nBars)
    {
        beginPath();
        roundedRect(x, y, w, h, 12);
        fillPaint(linearGradient(0, y, 0, y + h, Color(0x0b, 0x1a, 0x26), Color(0x06, 0x11, 0x1a)));
        fill();
        strokeColor(Color(0x1f, 0x3a, 0x4d)); strokeWidth(2); stroke();
        strokeColor(Color(0x1d, 0x3b, 0x50)); strokeWidth(1);
        for (int i = 1; i < 4; ++i) { beginPath(); moveTo(x, y + h * i / 4); lineTo(x + w, y + h * i / 4); stroke(); }
        for (int i = 1; i < 10; ++i) { beginPath(); moveTo(x + w * i / 10, y); lineTo(x + w * i / 10, y + h); stroke(); }
        const int steps = h > 200 ? 28 : 16;
        const float step = (w - 24) / nBars, bw = step - (nBars > 32 ? 3 : 5), bh = h - 16, seg = bh / steps;
        for (int b = 0; b < nBars; ++b)
        {
            const int lit = (int)std::lround(barAt(b / (float)(nBars - 1)) * steps);
            for (int i = 0; i < lit; ++i)
            {
                beginPath();
                rect(x + 12 + b * step, y + 8 + bh - (i + 1) * seg + 1, bw, seg - 2);
                fillColor(i < steps * 10 / 16 ? Color(0x2f, 0xd3, 0x6b) : i < steps * 13 / 16 ? Color(0xff, 0xd2, 0x3f) : Color(0xff, 0x3b, 0x30));
                fill();
            }
        }
    }

    void drawButtons(float x, float y)
    {
        static const char* titles[6] = { "FACE", "NAME", "0 VU", "LEFT METER", "RIGHT METER", "SEND" };
        const char* vals[6] = { kFaceLabels[values_[kFace] % kFaces], kNameLabels[values_[kName] % kNames], kRefLabels[values_[kRef] % 3],
                                kLookLabels[values_[kLookL] % 2], kLookLabels[values_[kLookR] % 2], values_[kSend] ? "On" : "Off" };
        for (int i = 0; i < 6; ++i)
        {
            const float bx = x + i * 160;
            beginPath();
            roundedRect(bx, y, 152, 68, 10);
            fillColor(Color(0x16, 0x18, 0x1c));
            fill();
            strokeColor((i == 0 || (i == 5 && values_[kSend])) ? kGold : Color(0x3a, 0x3f, 0x47));
            strokeWidth(1.5f);
            stroke();
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fontFaceId(sans_);
            fontSize(12); fillColor(kGray); text(bx + 16, y + 22, titles[i], nullptr);
            fontSize(19); fillColor(kGold); text(bx + 16, y + 46, vals[i], nullptr);
        }
    }

    struct Lane { const char* name; int b0, b1, row; Color colour; float avg, env, cool; };
    struct Pad { float v; Color colour; };
    Lane lanes_[4] = {
        { "KICK", 1, 6, 3, Color(0xff, 0x3b, 0x30), -90, 0, 0 },
        { "SNARE", 8, 13, 2, Color(0xff, 0xd2, 0x3f), -90, 0, 0 },
        { "PERC", 15, 23, 1, Color(0xff, 0x8a, 0x3d), -90, 0, 0 },
        { "HATS", 27, 31, 0, Color(0x2f, 0xd3, 0x6b), -90, 0, 0 },
    };
    Pad pads_[16] = {};
    float corr_ = 1.0f, mPeak_ = -120.0f, mRms_ = -120.0f, ringRot_ = 0.0f;

    const Snapshot* snap_ = nullptr;
    FontId sans_ = -1, serif_ = -1;
    int values_[kParamCount] = {};
    float vu_[2] = { -60.0f, -60.0f }, peak_[2] = { -120.0f, -120.0f }, bars_[kBands];

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZayONatorUI)
};

UI* createUI() { return new ZayONatorUI(); }

END_NAMESPACE_DISTRHO
