# ZaY-o-Nator

**Premium VU meters, LED peaks and a glowing spectrum for your MPC and your DAW. By BeatsbyLonza.** Free and open source (MIT).

Backlit vintage VU needles (classic cream or noir black), green/yellow/red LED peak meters and a spectrum that lights up with your beat.

## The MPC plugin

The meters run right on your MPC's screen (MPC OS 3.10.1 or later): Live III, XL, Live II, One, Key 37, X SE and Force. Each release includes `ZaY-o-Nator-<version>-mpc-sdk.zip`: copy the `BeatsbyLonza - VST - ZaY-o-Nator` folder into `VST2 Plugins`, or install it from the **Open MPC Plugin Manager**. See [mpc-plugin/README.md](mpc-plugin/README.md) for the install steps and the controls (**0 VU**, **Left / Right Meter**, **Send**).

## In your DAW (VST3 and AU, Mac and Windows)

**ZaY-o-Nator-DAW-Plugins-Mac.zip** has a VST3 and an AU for Logic, Ableton, Studio One, the MPC software and more, for Apple Silicon and Intel Macs. **ZaY-o-Nator-DAW-Plugin-Windows.zip** has the VST3 for FL Studio, Ableton, Studio One, the MPC software and more on Windows 10 and 11 (64-bit). It has seven faces:

| Face | What's on screen |
| --- | --- |
| **DUO** | Two VU meters with LED peaks, over the spectrum |
| **SPECTRUM** | The spectrum fills the window, with LED strips on the sides |
| **NEEDLES** | Big VUs only, the black meter first |
| **CONSOLE** | Tall LED towers and green, yellow and red bars |
| **DRUMS** | MPC-style pads that flash on every kick, snare, perc and hat, the moment they hit, and a pumping kick ring |
| **MASTER** | A stereo vectorscope, big peak and RMS numbers, stereo correlation and L/R bars |
| **SYNTHS** | A glowing waveform scope inside a ring of spectrum |

See [daw-plugin/README.md](daw-plugin/README.md) for the install steps and controls.

## ZaY-o-Nator Display (sold separately)

Both plugins work fully on their own. The **ZaY-o-Nator Display** is a separate, paid app by BeatsbyLonza for your extra monitor and live stream: full screen on any monitor or TV, every MPC and DAW track at once, and the showcase faces. Turn on **Send** in the plugins to feed it. Ask BeatsbyLonza (BeatsbyLonza.com) for details.

## For developers

- `daw-plugin/` is the VST3/AU plugin for Mac and Windows DAWs, built with DPF. `daw-plugin/DrumHits.hpp` is the drum hit detector (`tests/drum_hits_test.cpp` checks it).
- `mpc-plugin/` is the MPC plugin (Akai's MPC Plugin SDK); `tools/make_skin.py` draws its screen into `mpc-plugin/skins/`.
- With **Send** on, the plugins send small meter packets (never audio) on UDP port **47911**: `ZAYO1|<name>|peakL|peakR|rmsL|rmsR|band1,...,band32[|kick,snare,perc,hats]` (levels in dBFS, bands bass first; the DAW plugin adds drum hit counts).
- GitHub Actions builds both plugins on every push and attaches them to each release.

MIT licence (the two plugins in this repository). ZaY-o-Nator and BeatsbyLonza are names of BeatsbyLonza. The ZaY-o-Nator Display is not part of this repository or its licence.
