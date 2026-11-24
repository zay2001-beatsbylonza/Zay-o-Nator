# ZaY-o-Nator (MPC plugin)

Backlit VU meters, LED peak meters and a spectrum on your MPC's screen. It also sends them over Wi-Fi to the **ZaY-o-Nator Display** on your Mac, so every MPC shows up on your extra monitor. By BeatsbyLonza.

The sound passes straight through, so you can put it on any track or on the master.

## Install

1. The MPC must run **MPC OS 3.10.1 or later**.
2. On the MPC, go to **Menu → Preferences → Enhancements** and turn on **Enable Enhancements** and **External Plugin Support**. Wait a few seconds, then restart.
3. Unzip the download. Copy the `BeatsbyLonza - VST - ZaY-o-Nator` folder into a folder named `VST2 Plugins` (create it if needed):
   - **MPC XL and Live III:** connect USB and switch the MPC to **Controller Mode**. The internal drive appears on the Mac as **MPC User**. Copy the folder there, eject, and go back to Standalone.
   - **Gen 1 MPCs (MPC One, One+, Key 37, Live II, MPC X SE) and Force:** put a `VST2 Plugins` folder at the top of an **SD card or USB stick** (formatted exFAT), copy the folder into it, and put it in the MPC.
   - Or use the **Open MPC Plugin Manager**.
4. Restart the MPC.

## Use

On any track or the master, add an insert: **External VST Plugins → BeatsbyLonza → ZaY-o-Nator**.

| Control | What it does |
| --- | --- |
| **0 VU** | Sets which level reads 0 on the VU needles: -18, -14 or -10 dBFS. Use -10 for loud, finished beats so the needles swing around 0 instead of sticking at the top. |
| **Left Meter / Right Meter** | Sets each meter's face: **Classic** (cream) or **Noir** (black and amber). |
| **Send** | Sends the meters to the ZaY-o-Nator Display on your Mac. It's on by default. |

The line at the top left says what it's doing, for example `Sending to display as MPC-Live-III`.

## On your extra monitor

1. Turn on **Wi-Fi** on the MPC (**Menu → Preferences → Wi-Fi**) and join the same network as your Mac.
2. Start the **ZaY-o-Nator Display** on the Mac (`Start ZaY-o-Nator.command`).
3. The MPC shows up as a button at the bottom of the display, and in the **RACK** face with every other MPC. Click its button to show it full size.

**If the display doesn't see the MPC:** some routers block broadcasts. Make a text file named `ZaY-o-Nator display.txt` at the top of the drive the plugin is on (MPC User, or the SD card or USB stick). Put just your Mac's address in it, for example `192.168.1.50`. The display's terminal window shows that address. The plugin checks for the file every few seconds.

## Notes

- Light on CPU: the audio thread only copies samples. The meters are worked out about 25 times a second on a background thread.
- It sends small meter packets (levels and 32 spectrum bands) on UDP port 47911, on your own network only. No audio leaves the MPC.
- Built on Akai's MPC Plugin SDK (MIT). Source: github.com/zay2001-beatsbylonza/Zay-o-Nator
