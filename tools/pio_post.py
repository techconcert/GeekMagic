# PlatformIO post-script: after firmware.bin is built, run the recovery safety
# checks and copy it to dist/ with a name the stock updater accepts ("SDP...").
import os
import shutil
import subprocess
import sys
Import("env")  # noqa: F821

ROOT = env.subst("$PROJECT_DIR")  # noqa: F821
sys.path.insert(0, os.path.join(ROOT, "tools"))
import check_bin  # noqa: E402


def fs_start_from_elf(elf):
    nm = "xtensa-lx106-elf-nm"
    try:
        out = subprocess.check_output([nm, elf], env=env["ENV"]).decode()  # noqa: F821
    except Exception as e:
        print("WARNING: could not run nm:", e)
        return None
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == "_FS_start":
            return int(parts[0], 16)
    return None


def after_bin(source, target, env):
    bin_path = str(target[0])
    elf_path = os.path.splitext(bin_path)[0] + ".elf"
    envname = env["PIOENV"]
    ver = env.get("GQ_VERSION_STR", "dev").replace("/", "-")
    out_dir = os.path.join(ROOT, "dist")
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, f"SDP_GeekQuest_{envname}_{ver}.bin")
    shutil.copyfile(bin_path, out)
    shutil.copyfile(elf_path, out[:-4] + ".elf")
    ok = check_bin.check(out, fs_start=fs_start_from_elf(elf_path))
    if not ok:
        os.remove(out)
        os.remove(out[:-4] + ".elf")
        print("RECOVERY SAFETY CHECK FAILED - no .bin produced. See messages above.")
        env.Exit(1)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", after_bin)  # noqa: F821
