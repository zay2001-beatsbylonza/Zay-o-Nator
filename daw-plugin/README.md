# ZaY-o-Nator for your DAW (VST3 and AU, Mac and Windows)

The ZaY-o-Nator VU meters, LED peak meters and spectrum inside Logic, Ableton, Studio One, the MPC software FL Studio or any DAW that loads VST3 plugins on a Mac or Windows PC (and AU on the Mac). Everything runs in the plugin window. By BeatsbyLonza.

The sound passes straight through, so you can put it on any track or on the master.

## Install on a Mac

1. Download **ZaY-o-Nator-DAW-Plugins-Mac.zip** from the [latest release](../../../releases/latest) and unzip it.
2. Double-click **Install ZaY-o-Nator Plugins.command**. The first time, right-click it and choose **Open**. It copies the plugins into your user's `Library/Audio/Plug-Ins/VST3` and `Library/Audio/Plug-Ins/Components` folders.
3. Restart your DAW, or rescan plugins. ZaY-o-Nator shows up under **BeatsbyLonza**.

It runs natively on Apple Silicon and Intel Macs.

## Install on Windows

1. Download **ZaY-o-Nator-DAW-Plugin-Windows.zip** from the [latest release](../../../releases/latest) and unzip it (right-click, **Extract All**).
2. Double-click **Install ZaY-o-Nator (Windows).bat** and say **Yes** when Windows asks for permission. If Windows shows "Windows protected your PC", click **More info**, then **Run anyway**. It copies **ZaY-o-Nator.vst3** into `C:\Program Files\Common Files\VST3`.
3. Restart your DAW, or rescan plugins. ZaY-o-Nator shows up under **BeatsbyLonza**.

Or copy the **ZaY-o-Nator.vst3** folder into `C:\Program Files\Common Files\VST3` yourself. Windows 10 and 11, 64-bit.

## Use

Put ZaY-o-Nator on a track or the master. Click the buttons along the bottom to step through:

| Button | What it does |
| --- | --- |
| **FACE** | Seven looks: **DUO** (two VUs over the spectrum), **SPECTRUM**, **NEEDLES** (big VUs, black meter first), **CONSOLE** (LED towers and bars), **DRUMS** (pads that flash on kicks, snares, percussion and hats), **MASTER** (vectorscope, peak, RMS, stereo correlation) and **SYNTHS** (a scope inside a ring of spectrum). |
| **NAME** | What the ZaY-o-Nator Display calls this track (if you have it): MASTER, DRUMS, SYNTHS, BASS, KEYS, VOCALS, FX or TRACK. Put one on your drums bus set to DRUMS, one on your synths set to SYNTHS, and one on the master. |
| **0 VU** | Which level reads 0 on the needles: -18, -14 or -10 dB. Use -10 for loud, finished beats. |
| **LEFT METER / RIGHT METER** | Classic (cream) or Noir (black) for each meter. |
| **SEND** | Sends this track's meters to the ZaY-o-Nator Display, a separate app sold by BeatsbyLonza. Off until you turn it on; not needed for anything in the plugin. |

With the ZaY-o-Nator Display, each one shows up as its own button (for example **DAW DRUMS**).

The window can be resized: drag its corner.

## Notes

- With SEND on, it sends small meter packets (levels, 32 spectrum bands and drum hits, never audio) on UDP port 47911, to this computer and your local network. SEND is off until you turn it on.
- Built with DPF (DISTRHO Plugin Framework, ISC licence). The title font is DejaVu Serif Italic (see `fonts/DejaVu-LICENSE.txt`).
- Not code-signed yet: on the Mac the installer lifts the download quarantine for these two plugins only; on Windows, SmartScreen may ask before the installer runs.
