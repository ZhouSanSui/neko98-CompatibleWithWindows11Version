# Switch the live white-footprint probe from "near white" to EXACT white.
#
# Why: a screen capture of a layered window includes the wallpaper behind the
# transparent pixels, so the cat's anti-aliased outline blends into it and lands in
# the "near white" band no matter what the cue is doing. Measured with a >=200
# threshold the probe reported MORE white pixels with the proxy off than with it on,
# purely because that window happened to sit over a light part of the wallpaper.
#
# The white sprite's core pixels are exactly (255,255,255) - its premultiplied form
# is R=G=B=A=255 - while wallpaper-blended outline pixels essentially never hit 255
# on all three channels at once. Counting exact white therefore isolates the cue.
#
# Pure-ASCII script (standing rule: Windows PowerShell 5.1 mangles Chinese in
# BOM-less .ps1, so text patches live in Python).

import io
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "probe_white.cpp")

OLD = "            if( a[2] >= 200 && a[1] >= 200 && a[0] >= 200 ) white++;"

NEW = (
    "            //Exact white only, not \"near white\". The white sprite's core\n"
    "            //pixels are precisely (255,255,255) (premultiplied: R=G=B=A=255),\n"
    "            //while the cat's anti-aliased outline blended into the wallpaper\n"
    "            //almost never lands on 255 for all three channels at once. Counting\n"
    "            //near-white made this measurement depend on the wallpaper colour;\n"
    "            //counting exact white measures only the cue.\n"
    "            if( a[2] == 255 && a[1] == 255 && a[0] == 255 ) white++;"
)


def main():
    with io.open(SRC, "r", encoding="utf-8", newline="") as f:
        text = f.read()

    n = text.count(OLD)
    if n != 1:
        print("anchor count %d != 1 - refusing to write" % n)
        return 1

    text = text.replace(OLD, NEW)

    with io.open(SRC, "w", encoding="utf-8", newline="") as f:
        f.write(text)

    print("patched: exact-white criterion in place")
    return 0


if __name__ == "__main__":
    sys.exit(main())
