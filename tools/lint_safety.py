"""Recovery-contract lint (design 13.3 gate G4, rules L1..L7 of section 13.3 plus L8).

    python tools/lint_safety.py              # lint the repository
    python tools/lint_safety.py --root DIR   # lint another tree (used by the self-test)
    python tools/lint_safety.py --selftest   # every fixture in tools/lint_fixtures/ must trigger its rule

Regular expressions run over the sources with comments and string literals removed.
Exit code 0 = clean, 1 = violations.
"""
# RECOVERY-CHANGE (chunk1): new file (design 8.10 rule 1 lists it)
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)


def strip_code(src):
    """Removes // and /* */ comments, "strings", 'chars' and raw strings R"x(...)x". Keeps line breaks."""
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        nxt = src[i + 1] if i + 1 < n else ""
        if c == "/" and nxt == "/":
            while i < n and src[i] != "\n":
                i += 1
        elif c == "/" and nxt == "*":
            i += 2
            while i < n and not (src[i] == "*" and i + 1 < n and src[i + 1] == "/"):
                out.append("\n" if src[i] == "\n" else " ")
                i += 1
            i += 2
        elif c == "R" and nxt == '"' and (i == 0 or not (src[i - 1].isalnum() or src[i - 1] == "_")):
            m = re.match(r'R"([^(\s]{0,16})\(', src[i:])
            if not m:
                out.append(c)
                i += 1
                continue
            end = ")" + m.group(1) + '"'
            j = src.find(end, i + len(m.group(0)))
            j = n if j < 0 else j + len(end)
            out.append('""' + "".join("\n" for ch in src[i:j] if ch == "\n"))
            i = j
        elif c == '"' or c == "'":
            q = c
            out.append(q)
            i += 1
            while i < n and src[i] != q:
                if src[i] == "\\":
                    i += 1
                if i < n and src[i] == "\n":
                    out.append("\n")
                i += 1
            out.append(q)
            i += 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def read(root, rel):
    with open(os.path.join(root, rel), encoding="utf-8", errors="replace") as f:
        return f.read()


def files(root, subdir, exts=(".cpp", ".h", ".c", ".hpp")):
    base = os.path.join(root, subdir)
    res = []
    for dp, _, fn in os.walk(base):
        for f in sorted(fn):
            if f.endswith(exts):
                res.append(os.path.relpath(os.path.join(dp, f), root).replace(os.sep, "/"))
    return sorted(res)


def line_of(text, pos):
    return text.count("\n", 0, pos) + 1


class Lint:
    def __init__(self, root):
        self.root = root
        self.hits = []          # (rule, file, line, message)

    def add(self, rule, rel, line, msg):
        self.hits.append((rule, rel, line, msg))

    def scan(self, rule, rel, text, pattern, msg, flags=0):
        for m in re.finditer(pattern, text, flags):
            self.add(rule, rel, line_of(text, m.start()), msg % m.group(0).strip() if "%s" in msg else msg)

    # ---------------------------------------------------------------- L1
    # Known exception, reported to the lead: chunk 0A's gq_types.h includes <stddef.h> for offsetof (design 8.10 rule 3
    # says no includes). It is a freestanding header with no libc code behind it. Remove this entry when 0A drops it.
    L1_ALLOWED = {("lib/gamecore/src/gq_types.h", "stddef.h")}

    def l1(self):
        words = r"\b(?:malloc|calloc|realloc|new|delete|printf|sprintf|memcpy|memset|strlen|float|double)\b|\bfree\s*\("
        for rel in files(self.root, "lib/gamecore/src"):
            if os.path.basename(rel) == "platform.h":
                continue
            t = strip_code(read(self.root, rel))
            for m in re.finditer(r"#\s*include\s*<([^>]*)>", t):
                if (rel, m.group(1)) not in self.L1_ALLOWED:
                    self.add("L1", rel, line_of(t, m.start()), "angle-bracket include in the core: <%s>" % m.group(1))
            self.scan("L1", rel, t, words, "forbidden in the core: %s")

    # ---------------------------------------------------------------- L2, L3
    def l2_l3(self):
        flash_only = r"\b(?:EEPROM|LittleFS|SPIFFS|flashEraseSector|flashWrite|spi_flash_\w*)\b"
        wifi_only = r"WiFi\s*\.\s*mode\s*\(|\benableAP\b|\benableSTA\b|\bsoftAP\w*"
        persist_total = 0
        for rel in files(self.root, "firmware"):
            base = os.path.basename(rel)
            t = strip_code(read(self.root, rel))
            if base not in ("persist.cpp", "persist.h"):
                self.scan("L2", rel, t, flash_only, "flash/file-system access outside persist.cpp: %s")
            if base != "safety.cpp":
                self.scan("L2", rel, t, r"\bESP\s*\.\s*(?:restart|reset)\b", "ESP.restart/reset outside safety.cpp")
                self.scan("L2", rel, t, r"WiFi\s*\.\s*persistent\s*\(\s*true\s*\)", "WiFi.persistent(true) outside safety.cpp")
            else:
                persist_total += len(re.findall(r"WiFi\s*\.\s*persistent\s*\(\s*true\s*\)", t))
            if base not in ("safety.cpp", "netwatch.cpp"):
                self.scan("L2", rel, t, wifi_only, "WiFi mode/AP/STA control outside safety.cpp and netwatch.cpp: %s")
            # L3
            self.scan("L3", rel, t, r"\bwhile\s*\(\s*WiFi\s*\.", "blocking wait on WiFi")
            self.scan("L3", rel, t, r"WiFiClientSecure|HTTPClient|ArduinoOTA", "forbidden network class: %s")
            if base != "st7789.cpp":
                for m in re.finditer(r"\bdelay\s*\(\s*(\d+)", t):
                    if int(m.group(1)) > 500:
                        self.add("L3", rel, line_of(t, m.start()), "delay() above 500 ms")
        if os.path.isdir(os.path.join(self.root, "firmware")) and persist_total != 1:
            self.add("L2", "firmware/safety.cpp", 1, "WiFi.persistent(true) must occur exactly once in safety.cpp (found %d)" % persist_total)

    # ---------------------------------------------------------------- L4
    def l4(self):
        rel = "firmware/main.cpp"
        if not os.path.exists(os.path.join(self.root, rel)):
            return
        t = strip_code(read(self.root, rel))

        def body(name):
            m = re.search(r"\bvoid\s+" + name + r"\s*\(\s*(?:void)?\s*\)\s*\{", t)
            if not m:
                return None, 1
            i, depth = m.end(), 1
            while i < len(t) and depth:
                depth += (t[i] == "{") - (t[i] == "}")
                i += 1
            return t[m.end():i - 1], line_of(t, m.start())

        b, ln = body("setup")
        if b is None:
            self.add("L4", rel, 1, "setup() not found")
        else:
            stmts = [s.strip() for s in b.split(";") if s.strip()]
            idx = next((k for k, s in enumerate(stmts) if re.match(r"Serial\s*\.\s*begin\s*\(", s)), None)
            first = stmts[idx + 1] if idx is not None and idx + 1 < len(stmts) else (stmts[0] if stmts and idx is None else "")
            if not re.match(r"safety\s*::\s*begin\s*\(\s*\)$", first):
                self.add("L4", rel, ln, "first call in setup() after Serial.begin must be safety::begin()")
        b, ln = body("loop")
        if b is None:
            self.add("L4", rel, 1, "loop() not found")
        else:
            stmts = [s.strip() for s in b.split(";") if s.strip()]
            if not stmts or not re.match(r"safety\s*::\s*loop\s*\(\s*\)$", stmts[0]):
                self.add("L4", rel, ln, "first statement of loop() must be safety::loop();")

    # ---------------------------------------------------------------- L5, L7
    def l5_l7(self):
        for rel in files(self.root, "firmware") + files(self.root, "lib/gqsafe"):
            t = strip_code(read(self.root, rel))
            self.scan("L5", rel, t, r"millis\s*\(\s*\)\s*(?:<=|>=|<(?!<)|>(?!>))", "absolute millis() comparison (use now - since >= period)")
        for rel in files(self.root, "lib/gqsafe/src"):
            t = strip_code(read(self.root, rel))
            self.scan("L7", rel, t, r"\bESP\s*\.", "ESP. call in lib/gqsafe")
            self.scan("L7", rel, t, r"#\s*include\s*<Arduino\.h>", "#include <Arduino.h> in lib/gqsafe")

    # ---------------------------------------------------------------- L6, L8
    def l6_l8(self):
        rel = "platformio.ini"
        if not os.path.exists(os.path.join(self.root, rel)):
            return
        lines = [ln for ln in read(self.root, rel).splitlines() if not ln.lstrip().startswith((";", "#"))]
        t = "\n".join(lines)
        for needle in (r"eagle\.flash\.4m3m\.ld", r"flash_size\s*=\s*4MB", r"flash_mode\s*=\s*dio"):
            if not re.search(needle, t):
                self.add("L6", rel, 1, "platformio.ini must contain " + needle.replace("\\", ""))
        # L8: approvals are off by default; turning one on is a deliberate edit of this rule as well
        for flag in ("GQ_FLASH_SAVE", "GQ_AUTO_RESTART", "GQ_STRICT_UPLOAD"):
            if not re.search(r"^\s*-D" + flag + r"=0\s*$", t, re.M):
                self.add("L8", rel, 1, "[env] build_flags must contain -D%s=0 (approval not given yet; see design 12.2)" % flag)

    def run(self):
        self.l1(); self.l2_l3(); self.l4(); self.l5_l7(); self.l6_l8()
        return self.hits


def lint(root):
    return Lint(root).run()


def report(hits):
    for rule, rel, line, msg in sorted(hits):
        print(f"{rule} {rel}:{line}: {msg}")


def selftest():
    fx = os.path.join(HERE, "lint_fixtures")
    bad = 0
    names = sorted(d for d in os.listdir(fx) if os.path.isdir(os.path.join(fx, d)))
    if not names:
        print("no fixtures found")
        return 1
    for d in names:
        want = d.split("_")[0]
        hits = lint(os.path.join(fx, d))
        rules = {h[0] for h in hits}
        if want == "good":
            ok = not hits
        else:
            ok = want in rules
        print(("ok   " if ok else "FAIL ") + d + ("" if ok else f" (expected {want}, got {sorted(rules) or 'nothing'})"))
        bad += not ok
    if bad:
        return 1
    print("lint_safety selftest: all passed")
    return 0


def main(argv):
    if "--selftest" in argv:
        return selftest()
    root = REPO
    if "--root" in argv:
        root = os.path.abspath(argv[argv.index("--root") + 1])
    hits = lint(root)
    report(hits)
    if hits:
        print(f"lint_safety: {len(hits)} violation(s)")
        return 1
    print("lint_safety: clean")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
