#!/usr/bin/env python3
"""A stand-in for a ZaY-o-Nator Display in the tests: listens for meter packets on UDP
port 47911 for a few seconds and prints the name of every sender it heard
("ZAYO1|<name>|..."), one per line.

Usage: python3 tests/udp_listen.py <seconds> > heard.txt
"""
import socket
import sys
import time

secs = float(sys.argv[1]) if len(sys.argv) > 1 else 3.0
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("", 47911))
s.settimeout(0.2)
heard = set()
end = time.time() + secs
while time.time() < end:
    try:
        data, _ = s.recvfrom(4096)
    except socket.timeout:
        continue
    parts = data.decode("utf-8", "replace").split("|")
    if len(parts) >= 6 and parts[0] == "ZAYO1":
        heard.add(parts[1])
for name in sorted(heard):
    print(name)
