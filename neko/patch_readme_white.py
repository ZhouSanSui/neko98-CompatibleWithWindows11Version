# Insert the white-footprint test description into the shipped readme.
#
# Why a Python script instead of PowerShell: this file has to carry Chinese text,
# and Windows PowerShell 5.1 decodes a BOM-less .ps1 as ANSI, which corrupts any
# Chinese literal in it (every previous attempt at this failed exactly that way).
# Keeping the script pure ASCII and reading the Chinese from a UTF-8 side file is
# the pattern that works.
#
# Usage: python patch_readme_white.py

import io
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
README = os.path.join(HERE, "dist", u"\u4f7f\u7528\u8bf4\u660e.txt")
BLOCK = os.path.join(HERE, "packaging", "readme_white_block.txt")

# "- 抗" + the CJK that starts the anti-jump bullet. Built from code points so
# this script stays ASCII.
JUMP_ANCHOR_HEAD = u"- \u6297"

def read_text(path):
    with io.open(path, "r", encoding="utf-8-sig", newline="") as f:
        return f.read()

def write_text(path, text):
    # UTF-8 with BOM: the shipped readme is opened by Notepad on Chinese Windows.
    with io.open(path, "w", encoding="utf-8-sig", newline="") as f:
        f.write(text)

def main():
    if not os.path.exists(README):
        print("README not found: %s" % README)
        return 1
    if not os.path.exists(BLOCK):
        print("block not found: %s" % BLOCK)
        return 1

    text = read_text(README)
    block = read_text(BLOCK)

    if u"\u767d\u8272\u811a\u5370\u5b9e\u65f6\u9a8c\u8bc1" in text:
        print("already patched (marker present) - nothing to do")
        return 0

    # Locate the anti-jump bullet by its distinctive ASCII part, which does not
    # depend on any Chinese literal living in this script.
    idx = text.find(JUMP_ANCHOR_HEAD)
    if idx < 0:
        print("anchor not found - refusing to write")
        return 1

    # Only one anti-jump bullet should exist; if there were more we would be
    # guessing which one to patch in front of.
    if text.count(JUMP_ANCHOR_HEAD) != 1:
        print("anchor is ambiguous (%d hits) - refusing to write"
              % text.count(JUMP_ANCHOR_HEAD))
        return 1

    patched = text[:idx] + block + text[idx:]
    write_text(README, patched)

    size = os.path.getsize(README)
    print("patched: %d -> %d bytes" % (len(text.encode("utf-8")), size))
    return 0

if __name__ == "__main__":
    sys.exit(main())
