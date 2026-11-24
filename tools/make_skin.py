#!/usr/bin/env python3
"""Build ZaY-o-Nator's MPC screen ("Plugin Skins"), matching the ZaY-o-Nator Display.

Runs Akai's gen_ui.py with mpc-plugin/ui.json (the four controls along the bottom),
then draws the meters above them:
  - the page background: title, "by BeatsbyLonza", the meter cells and the spectrum panel,
  - two VU meters, each in three layers: the face (a 2-frame film strip: Classic cream /
    Noir black, following "Left Meter" / "Right Meter"), the needle (a film strip
    following "VU L" / "VU R": frames 0-40 black needle, 41-81 orange needle for Noir)
    and the glass and pivot cap on top,
  - two LED peak meters (green, yellow, red; a film strip following "Peak L" / "Peak R"),
  - 32 spectrum bars colored by height (film strips following "Band 01".."Band 32"),
  - a status line (what the plugin is doing).

The result goes to mpc-plugin/skins/, which the SDK's build.py copies verbatim.
Re-run this after changing parameter names, ui.json or the art:

    python3 tools/make_skin.py <path to mpc-plugin-builder-sdk>

Needs Pillow and a native build first (it reads mpc-plugin/build/params.json).
"""
import copy
import json
import math
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
PLUGIN = os.path.join(os.path.dirname(HERE), "mpc-plugin")

PW, PH = 1280, 628
# VU meters: 300 x 190 design units, drawn at this size
VW, VH = 426, 270
S = VW / 300.0
LED_W, GAP, MID = 30, 10, 40
X0 = (PW - (2 * (LED_W + GAP + VW) + MID)) // 2
VU_Y = 84
LED_X = [X0, X0 + LED_W + GAP + VW + MID + VW + GAP]
VU_X = [X0 + LED_W + GAP, X0 + LED_W + GAP + VW + MID]
# needle: pivot (150, 182), 124 long, swinging -52..52 degrees
PIV = (150 * S, 182 * S)
NLEN = 124 * S
NX0, NY0 = int(PIV[0] - NLEN * math.sin(math.radians(52)) - 6), int(PIV[1] - NLEN - 6)
NW, NH = int(2 * (PIV[0] - NX0)), int(PIV[1] - NY0 + 4)
VU_FRAMES = 41
LEDS = 24
BANDS, BAND_STEPS = 32, 16
SPEC = (X0, 368, PW - 2 * X0, 116)            # spectrum panel x, y, w, h
BAR_H = SPEC[3] - 16
BAR_STEP = (SPEC[2] - 24) / BANDS
BAR_W = int(BAR_STEP - 5)
STATUS = (X0, 22, 320, 40)

MARKS = [(-20, -48, "20"), (-10, -30, "10"), (-7, -20, "7"), (-5, -12, "5"), (-3, -2, "3"), (-2, 5, "2"),
         (-1, 11, "1"), (0, 18, "0"), (1, 28, "1"), (2, 37, "2"), (3, 47, "3")]
LOOKS = {
    "classic": dict(f=["#fff4cf", "#f1d995", "#c9a259", "#8a6a32"], glow="#f1d995", ink="#2b2418", red="#b3261e", needle="#151515"),
    "noir": dict(f=["#2a2216", "#17130d", "#0d0b08", "#050403"], glow="#ffb547", ink="#ffc36b", red="#ff4b3e", needle="#ff7a3d"),
}
GOLD = "#e8c77a"
FONT_DIRS = ["/usr/share/fonts/truetype/dejavu", "/System/Library/Fonts/Supplemental", "/Library/Fonts"]


def font(kind, size):
    names = {"sans": ["DejaVuSans.ttf", "Arial.ttf"], "bold": ["DejaVuSans-Bold.ttf", "Arial Bold.ttf"],
             "serif": ["DejaVuSerif-Italic.ttf", "Georgia Italic.ttf", "Times New Roman Italic.ttf"]}[kind]
    for d in FONT_DIRS:
        for n in names:
            p = os.path.join(d, n)
            if os.path.exists(p):
                return ImageFont.truetype(p, size)
    sys.exit("make_skin: no DejaVu or Arial font found")


def rgb(h, a=255):
    h = h.lstrip("#")
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), a)


def lerp(c1, c2, t):
    return tuple(int(a + (b - a) * t) for a, b in zip(c1, c2))


def pt(a, r, scale=S):
    t = math.radians(a)
    return (PIV[0] + r * scale * math.sin(t), PIV[1] - r * scale * math.cos(t))


def centered(d, xy, text, f, fill):
    w = d.textlength(text, font=f)
    d.text((xy[0] - w / 2, xy[1]), text, font=f, fill=fill)


# ---------------- the page background ----------------

def background():
    img = Image.new("RGBA", (PW, PH), (5, 5, 6, 255))
    # soft radial light behind the meters
    glow = Image.new("RGBA", (PW, PH), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    for i in range(30, 0, -1):
        t = i / 30
        gd.ellipse([PW / 2 - 700 * t, 200 - 420 * t, PW / 2 + 700 * t, 200 + 420 * t], fill=lerp((28, 31, 37, 255), (6, 6, 8, 255), t))
    img = Image.alpha_composite(img, glow)
    d = ImageDraw.Draw(img)
    # title, gold with a soft glow
    title = Image.new("RGBA", (PW, 90), (0, 0, 0, 0))
    td = ImageDraw.Draw(title)
    centered(td, (PW / 2, 8), "ZaY-o-Nator", font("serif", 50), rgb(GOLD, 120))
    title = title.filter(ImageFilter.GaussianBlur(9))
    img.alpha_composite(title, (0, 0))
    centered(d, (PW / 2, 8), "ZaY-o-Nator", font("serif", 50), rgb(GOLD))
    by = "by BeatsbyLonza"
    f = font("serif", 24)
    d.text((PW - X0 - d.textlength(by, font=f), 26), by, font=f, fill=rgb("#d7dbe1"))
    # meter cells
    for x in (LED_X[0] - 12, VU_X[1] - 12):
        d.rounded_rectangle([x, VU_Y - 12, x + LED_W + GAP + VW + 24, VU_Y + VH + 12], 14, fill=(0, 0, 0, 255), outline=(42, 46, 52, 255), width=2)
    # spectrum panel
    x, y, w, h = SPEC
    panel = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    pd = ImageDraw.Draw(panel)
    for yy in range(h):
        pd.line([0, yy, w, yy], fill=lerp(rgb("#0b1a26"), rgb("#06111a"), yy / h))
    for i in range(1, 4):
        pd.line([0, i * h / 4, w, i * h / 4], fill=rgb("#1d3b50"))
    for i in range(1, 10):
        pd.line([i * w / 10, 0, i * w / 10, h], fill=rgb("#1d3b50"))
    mask = Image.new("L", (w, h), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, w - 1, h - 1], 12, fill=255)
    img.paste(panel, (x, y), mask)
    d.rounded_rectangle([x, y, x + w - 1, y + h - 1], 12, outline=rgb("#1f3a4d"), width=2)
    return img


# ---------------- VU meter layers ----------------

def face(look):
    L = LOOKS[look]
    big = 2   # drawn at 2x, then smoothed down
    W2, H2 = VW * big, VH * big
    img = Image.new("RGBA", (W2, H2), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    k = S * big
    # bezel
    for yy in range(H2):
        t = yy / H2
        c = lerp(rgb("#4a4f57"), rgb("#1b1d21"), t * 2) if t < 0.5 else lerp(rgb("#1b1d21"), rgb("#2c3036"), (t - 0.5) * 2)
        d.line([0, yy, W2, yy], fill=c)
    bez = Image.new("L", (W2, H2), 0)
    ImageDraw.Draw(bez).rounded_rectangle([2 * k, 2 * k, 298 * k, 188 * k], int(10 * k), fill=255)
    out = Image.new("RGBA", (W2, H2), (0, 0, 0, 0))
    out.paste(img, (0, 0), bez)
    # backlit face: radial gradient from the bottom centre
    fx0, fy0, fx1, fy1 = 14 * k, 14 * k, 286 * k, 176 * k
    fw, fh = int(fx1 - fx0), int(fy1 - fy0)
    fimg = Image.new("RGBA", (fw, fh))
    px = fimg.load()
    stops = [(0, rgb(L["f"][0])), (0.45, rgb(L["f"][1])), (0.85, rgb(L["f"][2])), (1.0, rgb(L["f"][3]))]
    cx, cy, r = fw * 0.5, fh * 0.78, max(fw, fh) * 0.8
    for yy in range(fh):
        for xx in range(fw):
            t = min(1.0, math.hypot(xx - cx, (yy - cy)) / r)
            for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
                if t <= t1:
                    px[xx, yy] = lerp(c0, c1, (t - t0) / (t1 - t0))
                    break
    fm = Image.new("L", (fw, fh), 0)
    ImageDraw.Draw(fm).rounded_rectangle([0, 0, fw - 1, fh - 1], int(5 * k), fill=255)
    out.paste(fimg, (int(fx0), int(fy0)), fm)
    d = ImageDraw.Draw(out)
    piv = (150 * k, 182 * k)

    def p(a, rr):
        t = math.radians(a)
        return (piv[0] + rr * k * math.sin(t), piv[1] - rr * k * math.cos(t))

    def arc(a0, a1, rr, col, width):
        pts = [p(a0 + (a1 - a0) * i / 40, rr) for i in range(41)]
        d.line(pts, fill=col, width=int(width), joint="curve")

    arc(-48, 18, 118, rgb(L["ink"]), 1.6 * k)
    arc(18, 47, 121, rgb(L["red"]), 7 * k)
    f = font("sans", int(11 * k))
    for v, a, label in MARKS:
        c = rgb(L["red"] if v > 0 else L["ink"])
        d.line([p(a, 118), p(a, 130)], fill=c, width=int(2 * k))
        tx, ty = p(a, 142)
        centered(d, (tx, ty - 7 * k), label, f, c)
    for a in range(-48, 19, 4):
        d.line([p(a, 118), p(a, 124)], fill=rgb(L["ink"]), width=int(k))
    centered(d, (34 * k, 101 * k), "-", font("sans", int(14 * k)), rgb(L["ink"]))
    centered(d, (262 * k, 101 * k), "+", font("sans", int(14 * k)), rgb(L["red"]))
    centered(d, (262 * k, 152 * k), "VU", font("sans", int(12 * k)), rgb(L["ink"]))
    centered(d, (150 * k, 113 * k), "ZaY-o-Nator", font("serif", int(17 * k)), rgb(L["ink"]))
    return out.resize((VW, VH), Image.LANCZOS)


def face_strip():
    strip = Image.new("RGBA", (VW, VH * 2), (0, 0, 0, 0))
    strip.paste(face("classic"), (0, 0))
    strip.paste(face("noir"), (0, VH))
    return strip


def side_labels(side):
    """The L / R letter, for both faces (a 2-frame strip like the faces)."""
    strip = Image.new("RGBA", (30, 30 * 2), (0, 0, 0, 0))
    d = ImageDraw.Draw(strip)
    for i, look in enumerate(("classic", "noir")):
        centered(d, (15, i * 30 + 6), side, font("sans", 16), rgb(LOOKS[look]["ink"]))
    return strip


def needle_strip():
    big = 2
    strip = Image.new("RGBA", (NW, NH * VU_FRAMES * 2), (0, 0, 0, 0))
    for half, look in enumerate(("classic", "noir")):
        col = rgb(LOOKS[look]["needle"])
        for fidx in range(VU_FRAMES):
            a = -52 + 104 * fidx / (VU_FRAMES - 1)
            fr = Image.new("RGBA", (NW * big, NH * big), (0, 0, 0, 0))
            d = ImageDraw.Draw(fr)
            px, py = (PIV[0] - NX0) * big, (PIV[1] - NY0) * big
            t = math.radians(a)
            r0 = 16 * S * big   # starts under the pivot cap, so no tail shows below the face
            d.line([px + r0 * math.sin(t), py - r0 * math.cos(t), px + NLEN * big * math.sin(t), py - NLEN * big * math.cos(t)],
                   fill=col, width=int(1.8 * S * big))
            strip.paste(fr.resize((NW, NH), Image.LANCZOS), (0, (half * VU_FRAMES + fidx) * NH))
    return strip


def glass():
    """The pivot cap and the glass reflection, over the needle."""
    big = 2
    k = S * big
    img = Image.new("RGBA", (VW * big, VH * big), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.pieslice([108 * k, 146 * k, 192 * k, 206 * k], 180, 360, fill=rgb("#141414"))
    refl = Image.new("RGBA", img.size, (0, 0, 0, 0))
    rd = ImageDraw.Draw(refl)
    rd.polygon([(14 * k, 14 * k), (200 * k, 14 * k), (90 * k, 176 * k), (14 * k, 176 * k)], fill=(255, 255, 255, 34))
    img = Image.alpha_composite(img, refl)
    ImageDraw.Draw(img).rounded_rectangle([14 * k, 14 * k, 286 * k, 176 * k], int(5 * k), outline=(0, 0, 0, 110), width=int(3 * k))
    return img.resize((VW, VH), Image.LANCZOS)


# ---------------- LEDs and spectrum bars ----------------

def led_colour(i, lit):
    if i < 16:
        return rgb("#2fd36b") if lit else rgb("#0f3a1f")
    if i < 21:
        return rgb("#ffd23f") if lit else rgb("#3d3310")
    return rgb("#ff3b30") if lit else rgb("#3d1210")


def led_strip():
    seg = VH / LEDS
    strip = Image.new("RGBA", (LED_W, VH * (LEDS + 1)), (0, 0, 0, 0))
    d = ImageDraw.Draw(strip)
    for f in range(LEDS + 1):
        for i in range(LEDS):
            y1 = f * VH + VH - i * seg - 2
            d.rounded_rectangle([3, y1 - seg + 4, LED_W - 4, y1], 2, fill=led_colour(i, i < f))
    return strip


def bar_strip():
    seg = BAR_H / BAND_STEPS
    strip = Image.new("RGBA", (BAR_W, BAR_H * (BAND_STEPS + 1)), (0, 0, 0, 0))
    d = ImageDraw.Draw(strip)
    for f in range(BAND_STEPS + 1):
        for i in range(f):
            colour = rgb("#2fd36b") if i < 10 else rgb("#ffd23f") if i < 13 else rgb("#ff3b30")
            y1 = f * BAR_H + BAR_H - i * seg - 1
            d.rectangle([0, y1 - seg + 2, BAR_W - 1, y1], fill=colour)
    return strip


# ---------------- TUI entries ----------------

def bounds(x, y, w, h):
    return {"version": 1, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Show", "whenVisible": "Always",
            "boundsType": "Absolute", "bounds": "%d %d %d %d" % (x, y, w, h)}


def image_entry(name, image, x, y, w, h, frames=None, param=None):
    if frames:
        data = {"version": 2, "imageType": "FilmStrip", "colour": "0", "image": image, "numFrames": frames, "handleName": "Data"}
    else:
        data = {"version": 2, "imageType": "Regular", "colour": "0", "image": image}
    remap = [{"key": "Data", "value": "Parameter %d" % param}] if param is not None else []
    return {"version": 2, "componentData": {"version": 1, "name": name, "type": "Image", "data": data},
            "handle remapping": {"version": 1, "map": remap}, "bounds": bounds(x, y, w, h)}


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    sdk = sys.argv[1]
    params = os.path.join(PLUGIN, "build", "params.json")
    if not os.path.exists(params):
        sys.exit("make_skin: build the plugin first (mpc-plugin/build/params.json is missing)")
    out = os.path.join(PLUGIN, "skins")
    index = {p["name"]: p.get("index", i) for i, p in enumerate(json.load(open(params))["params"])}
    with tempfile.TemporaryDirectory() as tmp:
        gen = os.path.join(tmp, "Plugin Skins")
        subprocess.run([sys.executable, os.path.join(sdk, "tools", "gen_ui.py"), "--params", params,
                        "--ui", os.path.join(PLUGIN, "ui.json"), "--out", gen, "--kit-cache", tmp, "--quiet"], check=True)
        tui_path = os.path.join(gen, "TUI.json")
        tui = json.load(open(tui_path))
        page = tui["pageData"]
        locals_ = page["componentDefinitions"]["localComponentDefinitions"]
        toggle = next(lc["value"] for lc in locals_ if lc["key"] == "toggle")
        value_label = next(c for c in toggle["componentsData"] if c["componentData"]["name"] == "value")
        tab = page["tabs"][0]["componentDefinition"]["componentsData"]

        background().save(os.path.join(gen, "bg.png"))
        face_strip().save(os.path.join(gen, "vu_face.png"))
        needle_strip().save(os.path.join(gen, "vu_needle.png"))
        glass().save(os.path.join(gen, "vu_glass.png"))
        side_labels("L").save(os.path.join(gen, "vu_l.png"))
        side_labels("R").save(os.path.join(gen, "vu_r.png"))
        led_strip().save(os.path.join(gen, "led.png"))
        bar_strip().save(os.path.join(gen, "bar.png"))

        art = [image_entry("background", "bg.png", 0, 0, PW, PH)]
        for c, (side, look_p, vu_p, peak_p) in enumerate((("L", "Left Meter", "VU L", "Peak L"), ("R", "Right Meter", "VU R", "Peak R"))):
            x = VU_X[c]
            art.append(image_entry("face " + side, "vu_face.png", x, VU_Y, VW, VH, 2, index[look_p]))
            art.append(image_entry("side " + side, "vu_%s.png" % side.lower(), x + int(20 * S), VU_Y + int(146 * S), 30, 30, 2, index[look_p]))
            art.append(image_entry("needle " + side, "vu_needle.png", x + NX0, VU_Y + NY0, NW, NH, 2 * VU_FRAMES, index[vu_p]))
            art.append(image_entry("glass " + side, "vu_glass.png", x, VU_Y, VW, VH))
            art.append(image_entry("leds " + side, "led.png", LED_X[c], VU_Y, LED_W, VH, LEDS + 1, index[peak_p]))
        for b in range(BANDS):
            art.append(image_entry("band %02d" % (b + 1), "bar.png", int(SPEC[0] + 12 + b * BAR_STEP), SPEC[1] + 8, BAR_W, BAR_H,
                                   BAND_STEPS + 1, index["Band %02d" % (b + 1)]))
        status = copy.deepcopy(value_label)
        status["componentData"]["name"] = "status"
        status["componentData"]["data"]["textStyle"]["colour"] = "ff8d949e"
        status["componentData"]["data"]["textStyle"]["font"]["height"] = 22.0
        if "justification" in status["componentData"]["data"]["textStyle"]:
            status["componentData"]["data"]["textStyle"]["justification"] = "centredLeft"
        status["handle remapping"] = {"version": 1, "map": [{"key": "Data", "value": "Parameter %d" % index["Status"]}]}
        status["bounds"] = bounds(*STATUS)
        art.append(status)
        tab[0:0] = art   # behind the controls, background first

        json.dump(tui, open(tui_path, "w"), indent=1)
        if os.path.isdir(out):
            shutil.rmtree(out)
        shutil.copytree(gen, out)
    print("make_skin: wrote %s" % out)


if __name__ == "__main__":
    main()
