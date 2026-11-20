# Bump the render assertion count in the shipped readme from 48 to 53.
#
# The white-footprint work added five assertions (flag recorded per footprint;
# the white sprite is neutral; it is bright; it paints more light than a coloured
# paw; a repaint does not recolour an existing trail).
#
# Pure-ASCII script, Chinese handled by code points, per the standing rule that
# Windows PowerShell 5.1 corrupts Chinese literals in BOM-less .ps1 files.

import io
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
README = os.path.join(HERE, "dist", u"\u4f7f\u7528\u8bf4\u660e.txt")

# "共 " + "48" + " 项断言"  (the CJK is built from code points to stay ASCII)
HEAD = u"\u5171 "
TAIL = u" \u9879\u65ad\u8a00"
OLD = HEAD + u"48" + TAIL
NEW = HEAD + u"53" + TAIL

def main():
    with io.open(README, "r", encoding="utf-8-sig", newline="") as f:
        text = f.read()

    n = text.count(OLD)
    if n == 0:
        if NEW in text:
            print("already 53 - nothing to do")
            return 0
        print("anchor %r not found - refusing to write" % OLD)
        return 1
    if n != 1:
        print("anchor is ambiguous (%d hits) - refusing to write" % n)
        return 1

    text = text.replace(OLD, NEW)
    with io.open(README, "w", encoding="utf-8-sig", newline="") as f:
        f.write(text)

    print("48 -> 53 assertions, file now %d bytes" % os.path.getsize(README))
    return 0

if __name__ == "__main__":
    sys.exit(main())
