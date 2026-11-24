#!/bin/sh
# Runs the plugin in a pretend MPC and checks its meter packets go out on the network
# (what the ZaY-o-Nator Display listens for).
# Usage: sh tests/plugin_test.sh <plugin_host_test binary> <plugin.so>
set -e
HOST=$1 PLUGIN=$2
DIR=$(cd "$(dirname "$0")" && pwd)
# broadcasts may not loop back on a build machine: point the plugin at this computer
echo 127.0.0.1 > "$(dirname "$PLUGIN")/ZaY-o-Nator display.txt"
python3 "$DIR/udp_listen.py" 4 > heard.txt &
LPID=$!
sleep 0.5
"$HOST" "$PLUGIN" 3
wait $LPID
echo "heard: $(tr '\n' ' ' < heard.txt)"
if [ -s heard.txt ]; then echo "PASS  the MPC plugin sends its meters"; else echo "FAIL  no meters were sent"; exit 1; fi
exit 0
