"""Size report (design 13.3 gate G9): .bin bytes and .data + .bss bytes per environment, with the
difference to the previous build on the `builds` branch when it can be read.

    python tools/size_report.py dist/*.bin        (writes Markdown to $GITHUB_STEP_SUMMARY when set, always to stdout)
"""
# RECOVERY-CHANGE (chunk1): new file (gate G9)
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_bin  # noqa: E402


def previous_sizes():
    """{env name: bin size} of the files in firmware/ on origin/builds, or {}."""
    try:
        out = subprocess.check_output(["git", "ls-tree", "-l", "origin/builds", "firmware/"], stderr=subprocess.DEVNULL).decode()
    except Exception:  # noqa: BLE001
        return {}
    res = {}
    for line in out.splitlines():
        m = re.match(r"\S+\s+blob\s+\S+\s+(\d+)\s+firmware/SDP_GeekQuest_(.+?)_[^_]+\.bin$", line)
        if m:
            res[m.group(2)] = int(m.group(1))
    return res


def main(paths):
    prev = previous_sizes()
    rows = ["| build | .bin bytes | change | .data+.bss | note |", "|---|---|---|---|---|"]
    for p in paths:
        name = os.path.basename(p)
        m = re.match(r"SDP_GeekQuest_(.+?)_[^_]+\.bin$", name)
        env = m.group(1) if m else name
        size = os.path.getsize(p)
        elf = check_bin.read_elf_info(os.path.splitext(p)[0] + ".elf")
        ram = elf.ram if elf and elf.ram is not None else None
        diff = "new" if env not in prev else f"{size - prev[env]:+d}"
        note = "" if size <= check_bin.WARN_SIZE else "above the 485 KB warning level"
        rows.append(f"| {env} | {size} | {diff} | {ram if ram is not None else 'n/a'} | {note} |")
    text = "\n".join(rows) + "\n"
    print(text)
    s = os.environ.get("GITHUB_STEP_SUMMARY")
    if s:
        with open(s, "a") as f:
            f.write("### Size report\n\n" + text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
