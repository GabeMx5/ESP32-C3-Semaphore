#!/usr/bin/env python3
"""Compresses src_data/ web assets into data/ as .gz files.
Called directly by CI (compress_web_standalone.py) and also
imported by PlatformIO extra_scripts (compress_web.py)."""

import os
import gzip
import shutil

COMPRESS_EXTS = {".html", ".js", ".css", ".svg"}

def compress(src_dir="src_data", dst_dir="data"):
    if not os.path.isdir(src_dir):
        print(f"compress_web: {src_dir}/ not found, skipping")
        return
    os.makedirs(dst_dir, exist_ok=True)
    print("compress_web: compressing web assets...")
    for fname in sorted(os.listdir(src_dir)):
        src = os.path.join(src_dir, fname)
        if not os.path.isfile(src):
            continue
        ext = os.path.splitext(fname)[1].lower()
        if ext in COMPRESS_EXTS:
            dst = os.path.join(dst_dir, fname + ".gz")
            with open(src, "rb") as f_in, gzip.open(dst, "wb", compresslevel=9) as f_out:
                shutil.copyfileobj(f_in, f_out)
            orig_kb = os.path.getsize(src) / 1024
            comp_kb = os.path.getsize(dst) / 1024
            ratio   = (1 - comp_kb / orig_kb) * 100 if orig_kb > 0 else 0
            print(f"  {fname}: {orig_kb:.1f} KB → {comp_kb:.1f} KB ({ratio:.0f}% smaller)")
        else:
            dst = os.path.join(dst_dir, fname)
            shutil.copy2(src, dst)
            print(f"  {fname}: copied as-is")

if __name__ == "__main__":
    compress()
