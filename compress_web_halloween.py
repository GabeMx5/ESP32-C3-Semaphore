Import("env")
import sys
import os

sys.path.insert(0, env.subst("$PROJECT_DIR"))
import compress_web_standalone
compress_web_standalone.compress(
    src_dir=os.path.join(env.subst("$PROJECT_DIR"), "src_data_halloween"),
    dst_dir=os.path.join(env.subst("$PROJECT_DIR"), "data"),
)
