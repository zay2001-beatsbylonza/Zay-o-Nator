# ZaY-o-Nator

**Premium VU meters and spectrum for your extra monitor. By BeatsbyLonza.** Free and open source (MIT).

Backlit vintage VU needles, green/yellow/red LED peak meters and a glowing spectrum, made to look good on a live stream. It scales to any screen, from a laptop to a 4K TV.

## Five faces

Click the face buttons at the bottom, press **1–5**, or use the **arrow keys** to switch between them. You can also have it change face every 30 seconds.

| | Face | What's on screen |
| --- | --- | --- |
| 1 | **DUO** | Two big VU meters (classic cream and noir amber) with LED peaks, over the spectrum |
| 2 | **SPECTRUM** | A giant spectrum, with the VUs in a strip on top |
| 3 | **NEEDLES** | Just the VUs and LED peaks, as big as they go, with the black meter first |
| 4 | **RACK** | A pair of meters for every MPC sending to the display, all at once |
| 5 | **CONSOLE** | Tall LED towers and green, yellow and red spectrum bars |

## Get it

1. Download **ZaY-o-Nator-Mac.zip** (or **-Windows.zip**) from the [latest release](../../releases/latest) and unzip it.
2. Double-click **Start ZaY-o-Nator.command** (on Windows, **Start ZaY-o-Nator.bat**). The first time on a Mac, right-click it and choose **Open**.
3. The display opens in your browser. Drag that window to your extra monitor and press **F** for full screen.
4. Press **S** for settings. Under **Listen to**, pick **Audio input on this computer**, then pick the input (Loopback, your audio interface, a mic) and the channels. Allow microphone access when your browser asks.

Keep the small terminal window open while you use the display. The mouse and buttons hide after 3 seconds, so the screen stays clean on camera.

### Your MPCs

- **Through an audio interface or Loopback:** pick that input and the channel pair the MPC is on (for example 3–4). Use **Name shown on screen** to label it (MPC LIVE III).
- **Over Wi-Fi:** put the **ZaY-o-Nator MPC plugin** on a track or the master of each MPC. They send their meters to the display by themselves, and each one shows up as a button and in the RACK face.

### On your stream

In StreamYard, share the display's window or screen, or put it full screen on your extra monitor and point a camera at it.

## The MPC plugin

The same VU meters, LED peaks and spectrum also run on the MPC's own screen (MPC OS 3.10.1 or later). Each release includes `ZaY-o-Nator-<version>-mpc-sdk.zip`: copy the `BeatsbyLonza - VST - ZaY-o-Nator` folder into `VST2 Plugins`. See [mpc-plugin/README.md](mpc-plugin/README.md) for the install steps, the controls (**0 VU**, **Left / Right Meter**, **Send**) and what to do if the display doesn't see the MPC.

## Settings

- **Left / right meter:** classic cream or noir amber for each VU.
- **0 VU level:** -18, -14 or -10 dBFS (use -10 for loud beats).
- **Accent:** gold, blue or red.
- **Show LIVE badge**, **Show source buttons**, **Show face buttons**.
- **Change face every 30 seconds.**

Settings are saved in your browser.

## For developers

- `mpc-plugin/` is the MPC plugin (Akai's MPC Plugin SDK); `tools/make_skin.py` draws its screen into `mpc-plugin/skins/`.
- `display/` is a small Go app that serves the page (`display/web/index.html`) on `http://localhost:47910`, on this computer only.
- MPCs send meters as UDP packets to port **47911**: `ZAYO1|<name>|peakL|peakR|rmsL|rmsR|band1,band2,...` (all in dBFS, bands bass first).
- GitHub Actions builds the Mac and Windows downloads on every push and attaches them to each release.

MIT licence. ZaY-o-Nator and BeatsbyLonza are names of BeatsbyLonza.
