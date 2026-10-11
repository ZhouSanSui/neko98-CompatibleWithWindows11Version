#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Rebuild neko98-win11.zip with UTF-8 file names.

Why this exists: `7z a -tzip` stores the Chinese readme name as raw GBK bytes
with the archive's "UTF-8 name" flag (general purpose bit 0x800) CLEARED. Any
extractor that is not CP936-aware then shows mojibake for 使用说明.txt. Python's
zipfile writes names as UTF-8 AND sets the 0x800 flag, which is what the ZIP
spec expects for non-ASCII names.

Usage:
  python3 make_zip.py <dist_dir> <out_zip>
"""
import os
import sys
import zipfile

def main():
    if len(sys.argv) != 3:
        print("usage: make_zip.py <dist_dir> <out_zip>")
        return 2
    dist, out = sys.argv[1], sys.argv[2]

    names = sorted(os.listdir(dist))
    if not names:
        print("ERROR: %s is empty" % dist)
        return 1

    if os.path.exists(out):
        os.remove(out)

    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for n in names:
            p = os.path.join(dist, n)
            if os.path.isfile(p):
                z.write(p, arcname=n)

    # verify: every non-ASCII name must carry the 0x800 "UTF-8 name" flag.
    # (For pure-ASCII names the flag is optional: those bytes are identical in
    #  ASCII and UTF-8, so a clear flag is correct and must not be reported.)
    bad = 0
    with zipfile.ZipFile(out, "r") as z:
        for info in z.infolist():
            utf8_flag = bool(info.flag_bits & 0x800)
            needs_flag = not info.filename.isascii()
            mark = "ok"
            if needs_flag and not utf8_flag:
                mark = "MISSING-UTF8-FLAG"
                bad += 1
            print("   %-24s %8d bytes  flag=0x%04x  %s"
                  % (info.filename, info.file_size, info.flag_bits, mark))

    print("entries=%d  utf8-flag-missing=%d" % (len(names), bad))
    return 0

if __name__ == "__main__":
    sys.exit(main())
