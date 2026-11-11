# PlatformIO post-script: after firmware.bin is built, run the recovery safety
# checks and copy it to dist/ with a name the stock updater accepts ("SDP...").
import os
import shutil
import sys
Import("env")  # noqa: F821

ROOT = env.subst("$PROJECT_DIR")  # noqa: F821
sys.path.insert(0, os.path.join(ROOT, "tools"))
import check_bin  # noqa: E402


# RECOVERY-CHANGE (chunk1): the ELF is read by check_bin.read_elf_info (symbols, linker layout, RAM)
def define_value(name, default="0"):
    """Last -D<name>=<value> of the build flags (the quest_save environment overrides the default 0)."""
    val = default
    for d in env.get("CPPDEFINES", []):  # noqa: F821
        if isinstance(d, (tuple, list)) and len(d) >= 1 and str(d[0]) == name:
            val = str(d[1]) if len(d) > 1 else "1"
        elif isinstance(d, str) and d == name:
            val = "1"
    return val


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
    # RECOVERY-CHANGE (chunk1): full rule set; builds made with flash saving must contain the journal, others must not
    flash_save = define_value("GQ_FLASH_SAVE") not in ("0", "")
    ok = check_bin.check(out, elf=check_bin.read_elf_info(elf_path, env=env["ENV"]),  # noqa: F821
                         flash_save=flash_save, require_elf=True)
    if not ok:
        os.remove(out)
        os.remove(out[:-4] + ".elf")
        print("RECOVERY SAFETY CHECK FAILED - no .bin produced. See messages above.")
        env.Exit(1)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", after_bin)  # noqa: F821
