# PlatformIO pre-script: injects GQ_VERSION (short, fits on the clock's status line).
import subprocess
Import("env")  # noqa: F821

def describe():
    try:
        v = subprocess.check_output(["git", "describe", "--tags", "--always", "--dirty"],
                                    stderr=subprocess.DEVNULL).decode().strip()
    except Exception:
        v = "dev"
    return v[:16]

ver = describe()
env.Append(CPPDEFINES=[("GQ_VERSION", '\\"%s\\"' % ver)])  # noqa: F821
env["GQ_VERSION_STR"] = ver  # noqa: F821
print("GeekQuest version:", ver)
