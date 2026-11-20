# Correct the readme's white-footprint live-verification paragraph.
#
# WHY: the paragraph currently claims "代理关闭时白色像素占 4.9%，打开后升到 41.9%",
# taken from an early run of the live probe. That number turned out to be an artefact:
# the probe captures the SCREEN area of the pet's window, so it includes the wallpaper
# and the cat icon's own pure-white highlight pixels, and it varied run to run
# (off=0/on=498, off=436/on=448, off=91/on=46). The probe now reports the cue instead
# of asserting it, so the readme must say what the portable, reproducible evidence is:
# the renderer unit test, which reads the sprite buffer directly.
#
# Pure-ASCII script; Chinese lives in a UTF-8 side file (standing rule).

import io
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
README = os.path.join(HERE, "dist", u"\u4f7f\u7528\u8bf4\u660e.txt")
OLD_FILE = os.path.join(HERE, "packaging", "white_verify_old.txt")
NEW_FILE = os.path.join(HERE, "packaging", "white_verify_new.txt")


def read(path):
    with io.open(path, "r", encoding="utf-8-sig", newline="") as f:
        return f.read()


def main():
    text = read(README)
    old = read(OLD_FILE)
    new = read(NEW_FILE)

    # Normalise the side files' trailing newline so the anchor matches the readme
    # exactly; the readme is LF-only with no trailing blank line at that point.
    old = old.rstrip("\r\n")
    new = new.rstrip("\r\n")

    n = text.count(old)
    if n != 1:
        print("anchor count %d != 1 - refusing to write" % n)
        return 1

    text = text.replace(old, new)

    with io.open(README, "w", encoding="utf-8-sig", newline="") as f:
        f.write(text)

    print("patched: live-verification paragraph corrected (%d bytes)" % os.path.getsize(README))
    return 0


if __name__ == "__main__":
    sys.exit(main())
