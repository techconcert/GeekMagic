"""Recovery-change guard (design 13.3 gate G8, rule 8.10.6).

    python tools/recovery_guard.py BASE [HEAD]

If any recovery-contract file differs between BASE and HEAD (default HEAD), then
  (a) some commit in BASE..HEAD must have a line "RECOVERY-CHANGE: <why>" in its message, and
  (b) GQ_RECOVERY_REV in firmware/safety.cpp must differ from its value at BASE.
Exit code 1 otherwise. BASE must be a commit that exists (fetch-depth 0 in CI).
"""
# RECOVERY-CHANGE (chunk1): new file
import fnmatch
import re
import subprocess
import sys

CONTRACT_FILES = [
    "firmware/safety.cpp", "firmware/safety.h", "firmware/netwatch.*", "firmware/prefs.*", "firmware/webout.h", "firmware/config.*", "firmware/wxnet.*", "firmware/nightmode.*", "tools/pio_slim.py", "firmware/main.cpp",
    "platformio.ini", "tools/check_bin.py", "tools/pio_post.py", "tools/lint_safety.py", ".github/workflows/build.yml",
]


def git(*args):
    return subprocess.check_output(["git", *args], stderr=subprocess.STDOUT).decode(errors="replace")


def rev_at(ref):
    try:
        text = git("show", f"{ref}:firmware/safety.cpp")
    except subprocess.CalledProcessError:
        return None
    m = re.search(r"GQ_RECOVERY_REV\s*=\s*(\d+)", text)
    return int(m.group(1)) if m else None


def main(argv):
    if not argv:
        sys.exit(__doc__)
    base = argv[0]
    head = argv[1] if len(argv) > 1 else "HEAD"
    if re.fullmatch(r"0+", base):
        print("recovery_guard: no base commit (first push): skipped")
        return 0
    changed = [f for f in git("diff", "--name-only", base, head).split() if any(fnmatch.fnmatch(f, p) for p in CONTRACT_FILES)]
    if not changed:
        print("recovery_guard: no recovery-contract file changed")
        return 0
    print("recovery_guard: changed contract files:", ", ".join(changed))
    ok = True
    msgs = git("log", f"{base}..{head}", "--format=%B")
    if not re.search(r"^RECOVERY-CHANGE:\s*\S+", msgs, re.M):
        print("FAIL: no commit message contains a line 'RECOVERY-CHANGE: <why>'")
        ok = False
    old, new = rev_at(base), rev_at(head)
    if new is None:
        print("FAIL: GQ_RECOVERY_REV not found in firmware/safety.cpp")
        ok = False
    elif old == new:
        print(f"FAIL: GQ_RECOVERY_REV is still {new}: increment it")
        ok = False
    else:
        print(f"GQ_RECOVERY_REV {old} -> {new}")
    print("recovery_guard:", "ok" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
