#!/bin/bash
# Installs the ZaY-o-Nator VST3 and AU for your user, so Logic, Ableton, Studio One,
# the MPC software and other DAWs find them. By BeatsbyLonza.
cd "$(dirname "$0")"
VST3="$HOME/Library/Audio/Plug-Ins/VST3"
AU="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$VST3" "$AU"
rm -rf "$VST3/ZaY-o-Nator.vst3" "$AU/ZaY-o-Nator.component"
cp -R "ZaY-o-Nator.vst3" "$VST3/"
cp -R "ZaY-o-Nator.component" "$AU/"
# downloaded files are quarantined by macOS; these are ours, so lift it
xattr -dr com.apple.quarantine "$VST3/ZaY-o-Nator.vst3" "$AU/ZaY-o-Nator.component" 2>/dev/null
# make Logic and GarageBand see the new AU without a restart
killall -9 AudioComponentRegistrar 2>/dev/null
echo
echo "ZaY-o-Nator is installed:"
echo "  VST3: $VST3/ZaY-o-Nator.vst3"
echo "  AU:   $AU/ZaY-o-Nator.component"
echo
echo "Restart your DAW (or rescan plugins), then add ZaY-o-Nator by BeatsbyLonza to a track or the master."
echo "You can close this window."
