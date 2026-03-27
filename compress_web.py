Import("env")
import sys
import os

# Delegate to the standalone script so logic lives in one place
sys.path.insert(0, env.subst("$PROJECT_DIR"))
import compress_web_standalone
compress_web_standalone.compress(
    src_dir=os.path.join(env.subst("$PROJECT_DIR"), "src_data"),
    dst_dir=os.path.join(env.subst("$PROJECT_DIR"), "data"),
)
