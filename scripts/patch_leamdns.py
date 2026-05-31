#!/usr/bin/env python3
# patch_leamdns.py — work around the arduino-pico LEAmDNS multi-homing bug
# in the USB-NCM dual-netif setup.
#
# Without this patch LEAmDNS advertises hostname.local on every netif in
# lwIP's global netif_list and responds with that netif's IP. With our
# device on both W5500 (e.g. 192.168.15.x) and USB-NCM (192.168.7.1),
# LAN-side clients may cache the 192.168.7.1 record — which they can't
# route to — and the device looks unreachable for everything from ping
# to HTTP until the mDNS cache expires.
#
# The fix: skip the USB-NCM netif in every netif_list iteration inside
# LEAmDNS. usb_ncm.h tags the netif with name = "UN" specifically so we
# can identify it cheaply. W5500's LwipIntfDev names its netif "e0" so
# the filter is unambiguous.
#
# This script runs as a PIO pre-build action (extra_scripts =
# pre:scripts/patch_leamdns.py). It's idempotent: the marker line is
# checked for, the patch is only re-applied if it's missing.

from pathlib import Path

Import("env")  # type: ignore

PKG = Path(env.PioPlatform().get_package_dir("framework-arduinopico"))  # type: ignore
SRC = PKG / "libraries" / "LEAmDNS" / "src"

# Marker we look for to know we've patched a given file already.
MARKER = "agri-patch: skip USB-NCM netif"

# The line we look for. All four iteration sites share this exact text.
LOOP_LINE = "for (netif* pNetIf = netif_list; pNetIf; pNetIf = pNetIf->next) {"

# Files where the loop appears.
FILES = [
    SRC / "LEAmDNS_Transfer.cpp",   # _sendMDNSMessage_Multicast
    SRC / "LEAmDNS.cpp",            # _joinMulticastGroups + _leaveMulticastGroups
    SRC / "LEAmDNS_Control.cpp",    # reverse-PTR scan
]


def patch(file: Path) -> bool:
    if not file.exists():
        print(f"[patch_leamdns] WARN: {file} not found, skipping")
        return False

    text = file.read_text()
    if MARKER in text:
        return False  # already patched

    out_lines = []
    inserted = 0
    for line in text.split("\n"):
        out_lines.append(line)
        if LOOP_LINE in line:
            indent = len(line) - len(line.lstrip())
            body_indent = " " * (indent + 4)
            out_lines.append(
                body_indent
                + "if (pNetIf->name[0] == 'U' && pNetIf->name[1] == 'N') continue;  // "
                + MARKER
            )
            inserted += 1

    if inserted == 0:
        print(f"[patch_leamdns] WARN: loop line not found in {file.name}")
        return False

    file.write_text("\n".join(out_lines))
    print(f"[patch_leamdns] patched {file.name} ({inserted} site(s))")
    return True


for f in FILES:
    patch(f)
