# PlatformIO pre-script: injects GQ_VERSION (short, fits on the clock's status line).
import subprocess
Import("env")  # noqa: F821

def describe():
    # "r4-1a2b3c4": the release number from the VERSION file plus the short commit ("+" when the tree is dirty)
    try:
        rel = open("VERSION").read().strip() or "dev"
    except Exception:
        rel = "dev"
    try:
        sha = subprocess.check_output(["git", "rev-parse", "--short=7", "HEAD"], stderr=subprocess.DEVNULL).decode().strip()
        dirty = subprocess.call(["git", "diff", "--quiet", "HEAD"], stderr=subprocess.DEVNULL) != 0
    except Exception:
        return rel[:16]
    return ("%s-%s%s" % (rel, sha, "+" if dirty else ""))[:16]

ver = describe()
env.Append(CPPDEFINES=[("GQ_VERSION", '\\"%s\\"' % ver)])  # noqa: F821
env["GQ_VERSION_STR"] = ver  # noqa: F821
print("GeekQuest version:", ver)
