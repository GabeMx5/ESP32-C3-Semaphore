Import("env")
import os
import gzip
import shutil

# Extensions to gzip-compress
COMPRESS_EXTS = {".html", ".js", ".css", ".svg"}

def compress_web(source, target, env):
    project_dir = env.subst("$PROJECT_DIR")
    src_dir = os.path.join(project_dir, "src_data")
    dst_dir = os.path.join(project_dir, "data")

    if not os.path.isdir(src_dir):
        print("compress_web: src_data/ not found, skipping")
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

env.AddPreAction("$BUILD_DIR/littlefs.bin", compress_web)
