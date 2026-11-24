#!/bin/sh
# Runs the plugin in a pretend MPC next to a real ZaY-o-Nator Display and checks the
# display receives this MPC's meters.
# Usage: sh tests/plugin_test.sh <display binary> <plugin_host_test binary> <plugin.so>
set -e
DISPLAY_BIN=$1 HOST=$2 PLUGIN=$3
# broadcasts may not loop back on a build machine: point the plugin at this computer
echo 127.0.0.1 > "$(dirname "$PLUGIN")/ZaY-o-Nator display.txt"
"$DISPLAY_BIN" --no-open --port 47990 >/dev/null 2>&1 &
PID=$!
trap 'kill $PID 2>/dev/null || true' EXIT
sleep 1
"$HOST" "$PLUGIN" 3 &
HPID=$!
sleep 2
SOURCES=$(curl -sf http://localhost:47990/api/sources)
wait $HPID
echo "display sees: $SOURCES"
case "$SOURCES" in
  *'"sources":["'*) echo "PASS  the display receives the MPC's meters" ;;
  *) echo "FAIL  the display did not receive any meters"; exit 1 ;;
esac
exit 0
