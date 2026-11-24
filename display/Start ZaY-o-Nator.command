#!/bin/bash
# ZaY-o-Nator Display by BeatsbyLonza - double-click to start.
cd "$(dirname "$0")"
xattr -dr com.apple.quarantine . 2>/dev/null
if [ "$(uname -m)" = "arm64" ]; then
  exec ./zayonator-display-apple-silicon "$@"
else
  exec ./zayonator-display-intel "$@"
fi
