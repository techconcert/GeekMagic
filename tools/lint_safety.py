"""Recovery-contract lint (design 13.3 gate G4, rules L1..L7 of section 13.3 plus L8, L9; R2 adds the L2 flash rule; R3 adds L10..L13).

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


def strip_comments(src):
    """Removes // and /* */ comments but keeps string literals (L9 looks inside format strings)."""
    return re.sub(r"//[^\n]*|/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group(0)), src, flags=re.S)


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
        # R2: the clock never writes or erases flash by itself (the Updater does it for firmware uploads). There is no
        # file left that may do so, and flashRead (the read-only probe of sector 0x3FA000) is allowed in prefs.cpp only.
        flash_only = r"\b(?:EEPROM|LittleFS|SPIFFS|flashEraseSector|flashWrite|spi_flash_\w*)\b"
        wifi_only = r"WiFi\s*\.\s*mode\s*\(|\benableAP\b|\benableSTA\b|\bsoftAP\w*"
        persist_total = 0
        for rel in files(self.root, "firmware"):
            base = os.path.basename(rel)
            t = strip_code(read(self.root, rel))
            self.scan("L2", rel, t, flash_only, "flash write/erase or file-system access (R2 never writes flash): %s")
            # R3: still no flash backend (addendum item 3): no cfgflash.*, no GQ_CONFIG_FLASH, and the Updater class only in safety.cpp
            if base.startswith("cfgflash"):
                self.add("L2", rel, 1, "no flash backend in R3: cfgflash.* must not exist")
            self.scan("L2", rel, t, r"\bGQ_CONFIG_FLASH\b", "GQ_CONFIG_FLASH: R3 has no flash backend")
            if base != "safety.cpp":
                self.scan("L2", rel, t, r"\bUpdate\s*\.\s*(?:begin|write|end|writeStream)\b|\bUpdater\b", "Updater (firmware flash writer) outside safety.cpp: %s")
            if base != "prefs.cpp":
                self.scan("L2", rel, t, r"\bflashRead\b", "flash access outside prefs.cpp: %s")
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
        # L8: approval switches. Approved so far: A2 automatic restarts (ON), A3 modem sleep off (ON). A4 strict upload checks stays OFF, A1 (flash saving) no longer exists. Changing one is a deliberate
        # edit of this rule as well.
        if re.search(r"GQ_FLASH_SAVE", t):
            self.add("L8", rel, 1, "GQ_FLASH_SAVE must not appear: R2 has no flash saving at all")
        if re.search(r"GQ_CONFIG_FLASH", t):
            self.add("L8", rel, 1, "GQ_CONFIG_FLASH must not appear: R3 has no flash backend (RTC memory only)")
        if not re.search(r"^\s*-DGQ_NET_NO_SLEEP=1\s*$", t, re.M):
            self.add("L8", rel, 1, "[env] build_flags must contain -DGQ_NET_NO_SLEEP=1 (A3, modem sleep off, approved 2026-11-11)")
        if not re.search(r"^\s*-DGQ_AUTO_RESTART=1\s*$", t, re.M):
            self.add("L8", rel, 1, "[env] build_flags must contain -DGQ_AUTO_RESTART=1 (A2, approved)")
        if not re.search(r"^\s*-DGQ_STRICT_UPLOAD=0\s*$", t, re.M):
            self.add("L8", rel, 1, "[env] build_flags must contain -DGQ_STRICT_UPLOAD=0 (A4, not approved)")
        # the display-test scene is compiled into [env:test] only
        for sec in re.split(r"(?m)^(?=\[)", t):
            if "GQ_TEST_SCENE=1" in sec and not sec.startswith("[env:test]"):
                self.add("L8", rel, 1, "-DGQ_TEST_SCENE=1 is only allowed in [env:test]")

    # ---------------------------------------------------------------- L9
    # No floating point formatting or parsing: it would pull ~14 KB of libc back in (see tools/pio_slim.py).
    def l9(self):
        pat = r"%[-+ #0-9.*]*[feEgGaA]\b|\b(?:atof|strtod|strtof|sscanf|dtostrf)\s*\("
        for sub in ("firmware", "lib"):
            for rel in files(self.root, sub):
                t = strip_comments(read(self.root, rel))
                self.scan("L9", rel, t, pat, "floating point formatting or parsing: %s")

    # ---------------------------------------------------------------- L10, L11, L12, L13 (R3)
    # L10  sleep modes: deep sleep, forced/timed/automatic light sleep and WiFi off make the update page unreachable
    # L11  network client code (raw lwIP, extra clients) lives in firmware/wxnet.cpp only, and wxnet.cpp keeps its hands
    #      off WiFi control and off anything that blocks
    # L12  CPU frequency and display sleep only in nightmode.cpp; safety::web() (route registration) only in config.cpp
    # L13  RTC user memory only in the four files that own blocks (safety, netwatch, prefs, config)
    def l10_l13(self):
        sleep_bad = (r"\b(?:deepSleep\w*|system_deep_sleep\w*|forceSleep\w*|wifi_fpm_\w*|WIFI_LIGHT_SLEEP|LIGHT_SLEEP\w*|WIFI_OFF)\b"
                     r"|\bsetSleepMode\s*\(\s*WIFI_(?!NONE_SLEEP\b|MODEM_SLEEP\b)")
        net_bad = r"\b(?:tcp_\w+|udp_\w+|altcp_\w+|pbuf_\w+|dns_gethostbyname\w*|raw_\w+)\s*\("
        client_bad = r"\b(?:WiFiClient|WiFiUDP|AsyncClient|BearSSL\w*|WiFiServer)\b"
        for rel in files(self.root, "firmware") + files(self.root, "lib/gqsafe"):
            base = os.path.basename(rel)
            t = strip_code(read(self.root, rel))
            self.scan("L10", rel, t, sleep_bad, "forbidden sleep mode (deep sleep, forced/light sleep or WiFi off): %s")
            if rel.startswith("firmware/"):
                if base != "wxnet.cpp":
                    self.scan("L11", rel, t, net_bad, "raw lwIP call outside wxnet.cpp: %s")
                    self.scan("L11", rel, t, r"#\s*include\s*[<\"](?:lwip/|WiFiClient|ESP8266HTTPClient|WiFiClientSecure)", "network client include outside wxnet.cpp")
                if base not in ("safety.cpp", "wxnet.cpp"):
                    self.scan("L11", rel, t, client_bad, "network client/server class outside safety.cpp and wxnet.cpp: %s")
                if base != "nightmode.cpp" and not base.startswith("st7789."):
                    self.scan("L12", rel, t, r"\bsystem_update_cpu_freq\b|\bsetCpuFrequencyMhz\b|\blcd\s*::\s*(?:sleep|wake)\b", "CPU frequency or display sleep outside nightmode.cpp: %s")
                if base not in ("config.cpp", "safety.cpp"):
                    self.scan("L12", rel, t, r"\bsafety\s*::\s*web\s*\(", "safety::web() outside config.cpp")
                if base not in ("safety.cpp", "netwatch.cpp", "prefs.cpp", "config.cpp"):
                    self.scan("L13", rel, t, r"\brtcUserMemory(?:Read|Write)\b", "RTC user memory access outside safety.cpp, netwatch.cpp, prefs.cpp, config.cpp: %s")
                if base == "wxnet.cpp":
                    self.scan("L11", rel, t, r"\bWiFi\s*\.|\bWiFi\b\s*::|\bWiFi\w*\s+\w+\s*;", "wxnet.cpp must not call WiFi control (ask safety:: for the link state)")
                    self.scan("L11", rel, t, r"\bdelay\s*\(|\bString\b|\bWiFiClient\b", "wxnet.cpp must not use delay(), String or WiFiClient")

    def run(self):
        self.l1(); self.l2_l3(); self.l4(); self.l5_l7(); self.l6_l8(); self.l9(); self.l10_l13()
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
            # R3 fixtures carry an EXPECT file: a regular expression that one hit message of the wanted rule must match
            # (and no other rule may fire), so a fixture cannot pass by accident through some unrelated violation.
            ex = os.path.join(fx, d, "EXPECT")
            if os.path.exists(ex):
                pat = open(ex).read().strip()
                ok = ok and any(h[0] == want and re.search(pat, h[3]) for h in hits) and rules == {want}
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
