#!/usr/bin/env python3
"""Dialogue compiler and checker (docs/DESIGN-R3.md 4, chunk D1).

  python3 tools/gen_talk.py                 compile content/talk/*.txt into lib/gamecore/src/talk_data.h
  python3 tools/gen_talk.py --check         validate every file that exists, print the category table, exit 1 on any error
  python3 tools/gen_talk.py --check content/talk/bram.txt ...    validate only those speaker files (writers: run this)
  python3 tools/gen_talk.py --check --strict                     also fail when a speaker file is missing
  python3 tools/gen_talk.py --verify        compile in memory and fail if talk_data.h is not byte-identical (CI)

Source format (see content/talk/mira.txt, the model file). One file per speaker.  A script is a header line
`@ COND [ARG]` followed by 2 to 4 turns, `N:` (the villager) and `H:` (the hero), which must alternate; either may open.
A turn is one or two lines of the 5x7 box separated by `|`.  Lines starting with `#` and blank lines are ignored.

  @ ANY                                    always possible (personality)
  @ TIME MORNING|MIDDAY|EVENING|LATE       06..10, 10..15, 15..22, 22..06 (game hour)
  @ WX CLEAR|CLOUDY|RAIN|STORM|FOG|SNOW|WINDY      the weather the screen shows (live weather if there is any)
  @ TEMP COLD|HOT                          live temperature at most 5 C / at least 28 C (needs live data)
  @ SEASON SPRING|SUMMER|AUTUMN|WINTER
  @ FEST TODAY|RUNNING                     festival day before the festival starts / festival running
  @ DID CHOPPED|FISHED|FORAGED|MINED|FOUGHT|FAINTED|SOLD|BOUGHT|BUILT|ATE|GIFTED|SLEPT    the hero did it in the last 30 min
  @ QUEST MINE|STORY|NONE                  a quest of this villager is active / a story quest is / no quest is active
  @ CHAPTER 0..5                           story chapter (0 first, 5 and later: the bell)
  @ FRIEND 1|2|3                           friendship tier of this villager (25, 50, 75 and up)
  @ GOAL TOOL|PROJECT                      what the hero saves for
  @ EVENT 1..12                            today's daily event (calendar.cpp)
  @ LIVE                                   a live temperature exists (shared pool only; the only way besides TEMP to use {T})

Slots: {T} temperature with unit (23C, -5C, 104F)  {W} weather word (SUN CLOUDS RAIN STORM FOG SNOW WIND)
       {S} season word  {G} current goal name  {I} focus item name  {N} name of the villager spoken to.

_hero.txt has a different shape: `@ BED`, `@ WAKE`, `@ WAKE TEMP` (uses {T}), `@ DID <what>` (after-action remark) and
`@ QREPLY <kind>` (the hero's answer after a quest line: OFFER REPORT FETCH DELIVER GOTO SLAY DO WAIT SCENE THANKS), each
followed by one or more `H:` lines; every `H:` line is one entry.
"""
import collections, pathlib, re, sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
CONTENT = ROOT / "content/talk"
OUT = ROOT / "lib/gamecore/src/talk_data.h"
sys.path.insert(0, str(HERE))
import font5x7  # noqa: E402

GLYPHS = set(font5x7.GLYPHS.keys())
assert all(0x20 <= ord(c) <= 0x5F for c in GLYPHS), "the encoding needs glyphs inside 0x20..0x5F"

SPEAKERS = ["mira", "bram", "pip", "odda", "finn", "tess", "luma", "wren", "miso", "_shared", "_hero"]
NPC_FILES = SPEAKERS[:9]
GROUP = {n: i for i, n in enumerate(SPEAKERS)}   # script groups: 0..8 npc 1..9, 9 shared, 10 solo + quest replies (one file)

SLOT_CODE = {"T": 2, "W": 3, "S": 4, "G": 5, "I": 6, "N": 7}
SLOT_MAX = {"T": 4, "W": 6, "S": 6, "G": 9, "I": 8, "N": 4}      # longest text of each slot
SLOT_SAMPLES = {   # every value a slot can take (the round trip tests all of them)
    "T": ["-99C", "-5C", "0C", "23C", "99C", "-4F", "104F", "199F"],
    "W": ["SUN", "CLOUDS", "RAIN", "STORM", "FOG", "SNOW", "WIND"],
    "S": ["SPRING", "SUMMER", "AUTUMN", "WINTER"],
    "G": ["AXE II", "PURSE III", "FOUNTAIN", "FUND"],
    "I": ["WOOD", "MUSHROOM", "GEL"],
    "N": ["MIRA", "BRAM", "PIP", "ODDA", "FINN", "TESS", "LUMA", "WREN", "MISO"],
}
BANNED = {"TELL ME MORE!", "I'LL KEEP WATCH.", "SOUNDS GOOD!", "SURE THING.", "I SEE!", "GOOD TO KNOW.", "REALLY? NICE!"}

CONDS = ["ANY", "TIME", "WX", "TEMP", "SEASON", "FEST", "DID", "QUEST", "CHAPTER", "FRIEND", "GOAL", "EVENT", "LIVE",
         "BED", "WAKE", "SOLODID", "QREPLY"]
TD = ["CHOPPED", "FISHED", "FORAGED", "MINED", "FOUGHT", "FAINTED", "SOLD", "BOUGHT", "BUILT", "ATE", "GIFTED", "SLEPT"]  # id = index + 1
QK = ["OFFER", "REPORT", "FETCH", "DELIVER", "GOTO", "SLAY", "DO", "WAIT", "SCENE", "THANKS"]
ARGS = {
    "TIME": ["MORNING", "MIDDAY", "EVENING", "LATE"],
    "WX": ["CLEAR", "CLOUDY", "RAIN", "STORM", "FOG", "SNOW", "WINDY"],
    "TEMP": ["COLD", "HOT"],
    "SEASON": ["SPRING", "SUMMER", "AUTUMN", "WINTER"],
    "FEST": ["TODAY", "RUNNING"],
    "DID": TD,
    "QUEST": ["MINE", "STORY", "NONE"],
    "GOAL": ["TOOL", "PROJECT"],
    "SOLODID": TD,
    "QREPLY": QK,
}
NUMERIC = {"CHAPTER": range(0, 6), "FRIEND": range(1, 4), "EVENT": range(1, 13)}

# What every speaker file must contain (docs/DESIGN-R3.md 4.5; the design's categories add up to 21, so TC_ANY is 3).
VILLAGER_CATS = [("ANY", ["ANY"], 3), ("TIME", ["TIME"], 2), ("WX/TEMP", ["WX", "TEMP"], 3), ("SEASON", ["SEASON"], 2),
                 ("FEST", ["FEST"], 1), ("DID", ["DID"], 3), ("QUEST/CHAPTER", ["QUEST", "CHAPTER"], 2),
                 ("FRIEND", ["FRIEND"], 3), ("GOAL/EVENT", ["GOAL", "EVENT"], 1)]
VILLAGER_TURNS = {2: 8, 3: 9, 4: 3}
MIN_HERO_OPENS = 6
EXPECT = {n: dict(scripts=20, lines=55) for n in NPC_FILES[:8]}
EXPECT["miso"] = dict(scripts=6, lines=12)
EXPECT["_shared"] = dict(scripts=12, lines=30)
EXPECT["_hero"] = dict(bed=6, wake=6, wake_temp=3, did=18, qreply=16)


class Script:
    def __init__(self, speaker, cond, arg, turns, src):
        self.speaker, self.cond, self.arg, self.turns, self.src = speaker, cond, arg, turns, src
        self.hero_opens = turns[0][0] == "H"

    @property
    def lines(self):
        return len(self.turns)


ERRORS = []


def err(where, msg):
    ERRORS.append(f"{where}: {msg}")


def parse_arg(cond, tok, where, solo):
    """Header argument -> (cond name, numeric arg)."""
    if solo:
        if cond == "BED":
            return ("BED", 0) if tok is None else (err(where, "BED takes no argument") or ("BED", 0))
        if cond == "WAKE":
            if tok in (None, "TEMP"):
                return ("WAKE", 1 if tok else 0)
            err(where, "WAKE takes no argument or TEMP")
            return ("WAKE", 0)
        if cond == "DID":
            cond = "SOLODID"
        elif cond != "QREPLY":
            err(where, f"unknown condition {cond} in the hero file")
            return ("BED", 0)
    if cond not in CONDS:
        err(where, f"unknown condition {cond}")
        return ("ANY", 0)
    if cond in ("ANY", "LIVE"):
        if tok is not None:
            err(where, f"{cond} takes no argument")
        return (cond, 0)
    if tok is None:
        err(where, f"{cond} needs an argument")
        return (cond, 0)
    if cond in ARGS:
        if tok not in ARGS[cond]:
            err(where, f"bad argument {tok} for {cond} (one of {' '.join(ARGS[cond])})")
            return (cond, 0)
        i = ARGS[cond].index(tok)
        return (cond, i + 1 if cond in ("DID", "SOLODID") else i)
    if cond in NUMERIC:
        if not tok.isdigit() or int(tok) not in NUMERIC[cond]:
            err(where, f"bad argument {tok} for {cond}")
            return (cond, 0)
        return (cond, int(tok))
    err(where, f"cannot use {cond} here")
    return (cond, 0)


def parse_file(name, path):
    solo = name == "_hero"
    scripts = []
    cur = None
    for no, raw in enumerate(path.read_text().split("\n"), 1):
        where = f"{path.name}:{no}"
        line = raw.rstrip()
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        if line.startswith("@"):
            parts = line[1:].split()
            if not parts or len(parts) > 2:
                err(where, "header must be `@ COND [ARG]`")
                cur = None
                continue
            cond, arg = parse_arg(parts[0], parts[1] if len(parts) > 1 else None, where, solo)
            cur = dict(cond=cond, arg=arg, turns=[], where=where, hdr=parts)
            if solo:
                cur["is_solo"] = True
            scripts.append(cur)
            continue
        m = re.match(r"^([NH]): ?(.*)$", line)
        if not m or cur is None:
            err(where, "expected `N: text`, `H: text` or a header")
            continue
        if solo and m.group(1) != "H":
            err(where, "the hero file only has H: lines")
        cur["turns"].append((m.group(1), m.group(2), where))
    out = []
    for s in scripts:
        if solo:    # every H line is its own entry
            if not s["turns"]:
                err(s["where"], "header without lines")
            for t in s["turns"]:
                out.append(Script(name, s["cond"], s["arg"], [t], s["where"]))
        else:
            out.append(Script(name, s["cond"], s["arg"], s["turns"], s["where"]))
    return out


def expand(text, mode):
    """Text with slots replaced by their longest value (mode 'max') or removed (mode 'raw')."""
    def rep(m):
        return "#" * SLOT_MAX[m.group(1)] if mode == "max" else ""
    return re.sub(r"\{([TWSGIN])\}", rep, text)


def check_turn(where, side, text, solo_ok_temp, script):
    raw = text
    if re.search(r"\{[^}]*\}", re.sub(r"\{[TWSGIN]\}", "", text)):
        err(where, f"unknown slot in `{text}`")
    stripped = re.sub(r"\{[TWSGIN]\}", "", text)
    for ch in stripped.replace("|", ""):
        if ch not in GLYPHS:
            err(where, f"character `{ch}` is not a font glyph (upper case, no quotes) in `{raw}`")
            break
    lines = text.split("|")
    if len(lines) > 2:
        err(where, f"more than 2 lines in `{raw}`")
    for ln in lines:
        ex = expand(ln, "max")
        if not (1 <= len(ex) <= 19):
            err(where, f"line `{ln}` is {len(ex)} characters after slots (1..19)")
        if ln != ln.strip() or "  " in ln:
            err(where, f"leading, trailing or double space in `{ln}`")
    if "{T}" in text and not solo_ok_temp:
        err(where, f"`{{T}}` needs `@ TEMP`, `@ LIVE` or `@ WAKE TEMP`: {raw}")
    if len("|".join(expand(l, "max") for l in lines)) > 39:
        err(where, "decoded turn longer than 39 characters")
    if side == "H":
        flat = " ".join(lines)
        if text in BANNED or "|".join(lines) in BANNED or flat in BANNED:
            err(where, f"banned hero line `{raw}`")
        if len(expand(flat, "max")) < 8:
            err(where, f"hero line shorter than 8 characters: `{raw}`")


def validate(name, scripts, strict_counts):
    for s in scripts:
        where = s.src
        live = s.cond in ("TEMP", "LIVE") or (s.cond == "WAKE" and s.arg == 1)
        if name == "_hero":
            pass
        else:
            if s.cond in ("BED", "WAKE", "SOLODID", "QREPLY"):
                err(where, f"{s.cond} is only for _hero.txt")
            if not 2 <= len(s.turns) <= 4:
                err(where, f"a script has 2 to 4 turns, this one has {len(s.turns)}")
            for i in range(1, len(s.turns)):
                if s.turns[i][0] == s.turns[i - 1][0]:
                    err(s.turns[i][2], "speakers must alternate N: / H:")
            if name == "_shared" and any(t[0] for t in s.turns[:0]):
                pass
            if s.cond == "FRIEND" and name == "miso":
                pass
        for side, text, w in s.turns:
            check_turn(w, side, text, live, s)
    if not strict_counts:
        return
    e = EXPECT[name]
    if name == "_hero":
        by = collections.Counter(s.cond for s in scripts)
        wt = sum(1 for s in scripts if s.cond == "WAKE" and s.arg == 1)
        for key, have, want in (("bed", by["BED"], e["bed"]), ("wake", by["WAKE"], e["wake"]), ("wake_temp", wt, e["wake_temp"]),
                                ("did", by["SOLODID"], e["did"]), ("qreply", by["QREPLY"], e["qreply"])):
            if have != want:
                err(name, f"{key}: {have} entries, expected {want}")
        for k in range(1, 13):
            if not any(s.cond == "SOLODID" and s.arg == k for s in scripts):
                err(name, f"no DID remark for {TD[k - 1]}")
        for k in range(10):
            if not any(s.cond == "QREPLY" and s.arg == k for s in scripts):
                err(name, f"no QREPLY for {QK[k]}")
        return
    if len(scripts) != e["scripts"]:
        err(name, f"{len(scripts)} scripts, expected {e['scripts']}")
    nl = sum(s.lines for s in scripts)
    if nl != e["lines"]:
        err(name, f"{nl} lines, expected {e['lines']}")
    if name in NPC_FILES[:8]:
        by = collections.Counter(s.cond for s in scripts)
        for label, conds, want in VILLAGER_CATS:
            have = sum(by[c] for c in conds)
            if have != want:
                err(name, f"category {label}: {have} scripts, expected {want}")
        if not any(s.cond == "TEMP" for s in scripts):
            err(name, "needs at least one TEMP script")
        tiers = sorted(s.arg for s in scripts if s.cond == "FRIEND")
        if tiers != [1, 2, 3]:
            err(name, f"FRIEND scripts must be one per tier 1, 2, 3, found {tiers}")
        lens = collections.Counter(s.lines for s in scripts)
        for n, want in VILLAGER_TURNS.items():
            if lens[n] != want:
                err(name, f"{lens[n]} scripts of {n} turns, expected {want}")
        ho = sum(1 for s in scripts if s.hero_opens)
        if ho < MIN_HERO_OPENS:
            err(name, f"the hero opens {ho} scripts, at least {MIN_HERO_OPENS} needed")
    if name == "miso" and any(s.lines != 2 for s in scripts):
        err(name, "all of Miso's scripts have exactly 2 turns")


def category_table(per_file):
    rows = []
    hdr = ["file", "scripts", "lines", "2t", "3t", "4t", "hero opens", "avg chars"] + [c[0] for c in VILLAGER_CATS]
    rows.append(hdr)
    for name in SPEAKERS:
        if name not in per_file:
            rows.append([name, "MISSING"])
            continue
        sc = per_file[name]
        if name == "_hero":
            by = collections.Counter(s.cond for s in sc)
            rows.append([name, f"bed {by['BED']}", f"wake {by['WAKE']}", f"did {by['SOLODID']}", f"qreply {by['QREPLY']}"])
            continue
        by = collections.Counter(s.cond for s in sc)
        lens = collections.Counter(s.lines for s in sc)
        tl = [len(expand(t, "max")) for s in sc for _side, t, _w in s.turns]
        row = [name, len(sc), sum(s.lines for s in sc), lens[2], lens[3], lens[4], sum(1 for s in sc if s.hero_opens),
               f"{sum(tl) / max(1, len(tl)):.1f}"]
        for label, conds, want in VILLAGER_CATS:
            have = sum(by[c] for c in conds)
            row.append(f"{have}/{want}" if name in NPC_FILES[:8] else str(have))
        rows.append(row)
    w = [max(len(str(r[i])) for r in rows if i < len(r)) for i in range(len(hdr))]
    return "\n".join("  ".join(str(c).ljust(w[i]) for i, c in enumerate(r)) for r in rows)


def missing_report(per_file):
    out = []
    for name in SPEAKERS:
        e = EXPECT[name]
        if name not in per_file:
            if name == "_hero":
                out.append(f"  {name}: missing ({e['bed'] + e['wake'] + e['did'] + e['qreply']} lines)")
            else:
                out.append(f"  {name}: missing ({e['scripts']} scripts, {e['lines']} lines)")
    return out


# ------------------------------------------------------------------------------------------ encoding
def to_work(text):
    """Source text of one turn -> work string: glyphs, \\x01 break, \\x02..\\x07 slots."""
    s = text.replace("|", "\x01")
    return re.sub(r"\{([TWSGIN])\}", lambda m: chr(SLOT_CODE[m.group(1)]), s)


def build_dictionary(work_lines, max_entries=160):
    lines = list(work_lines)
    dic = []
    seg_re = re.compile(r"[\x20-\x5f]+")
    for _ in range(max_entries):
        cnt = collections.Counter()
        for l in lines:
            for seg in seg_re.findall(l):
                for n in range(2, 9):
                    for i in range(len(seg) - n + 1):
                        cnt[seg[i:i + n]] += 1
        cands = sorted(cnt.items(), key=lambda kv: (-(kv[1] * (len(kv[0]) - 1) - len(kv[0]) - 2), kv[0]))[:40]
        best = None
        for sub, _c in cands:
            c = sum(seg.count(sub) for l in lines for seg in seg_re.findall(l))
            gain = c * (len(sub) - 1) - len(sub) - 2
            key = (-gain, sub)
            if best is None or key < best[0]:
                best = (key, sub, gain)
        if best is None or best[2] <= 0:
            break
        sub = best[1]
        tok = chr(0x60 + len(dic))
        dic.append(sub)
        lines = [l.replace(sub, tok) for l in lines]
    return dic, lines


def decode(blob_line, dic, slots):
    out = []
    for b in blob_line:
        if b == 1:
            out.append("|")
        elif 2 <= b <= 7:
            out.append(slots[b])
        elif 0x20 <= b <= 0x5F:
            out.append(chr(b))
        elif b >= 0x60:
            out.append(dic[b - 0x60])
        else:
            raise ValueError(b)
    return "".join(out)


def compile_all(per_file):
    scripts = []       # (group, Script) in group order
    for name in SPEAKERS:
        if name in per_file:
            for s in per_file[name]:
                scripts.append(s)
    work = []
    meta = []
    for s in scripts:
        first = len(work)
        for side, text, _w in s.turns:
            work.append(to_work(text))
        meta.append(first)
    dic, tokd = build_dictionary(work)
    blob = bytearray()
    loff = []
    for l in tokd:
        loff.append(len(blob))
        for ch in l:
            blob.append(ord(ch))
        blob.append(0)
    loff.append(len(blob))
    # round trip
    for i, l in enumerate(work):
        enc = list(blob[loff[i]:loff[i + 1] - 1])
        for sv in range(8):
            slots = {c: SLOT_SAMPLES[k][sv % len(SLOT_SAMPLES[k])] for k, c in SLOT_CODE.items()}
            back = decode(enc, dic, slots)
            want = "".join(slots[ord(ch)] if 2 <= ord(ch) <= 7 else ("|" if ord(ch) == 1 else ch) for ch in l)
            if back != want:
                err("round trip", f"line {i}: {back!r} != {want!r}")
    return scripts, meta, dic, blob, loff


def render_header(scripts, meta, dic, blob, loff):
    first = [0] * 13
    # groups: 0..8 npcs, 9 shared, 10 hero solo (BED/WAKE/SOLODID), 11 quest replies, 12 end
    def group_of(s):
        if s.speaker == "_hero":
            return 11 if s.cond == "QREPLY" else 10
        return GROUP[s.speaker]
    order = sorted(range(len(scripts)), key=lambda i: (group_of(scripts[i]), i))
    scripts = [scripts[i] for i in order]
    meta = [meta[i] for i in order]
    counts = [0] * 12
    for s in scripts:
        counts[group_of(s)] += 1
    acc = 0
    for g in range(12):
        first[g] = acc
        acc += counts[g]
    first[12] = acc
    assert len(scripts) <= 255, "script indexes are 8 bits"
    assert len(blob) <= 12288, f"blob {len(blob)} bytes over the 12,288 limit"
    o = []
    o.append("// GENERATED by tools/gen_talk.py from content/talk/*.txt. Do not edit; change the sources and rerun it.\n")
    o.append("// Dialogue scripts (docs/DESIGN-R3.md 4). The turns of a script are consecutive, each ends with a 0 byte. Line bytes: 0 end, 1 line break, 2..7 slots {T}{W}{S}{G}{I}{N}, 0x20..0x5F glyphs,\n")
    o.append("// 0x60.. dictionary entry (plain glyphs only). The tables exist only where TALK_DATA_IMPL is defined (talk.cpp);\n")
    o.append("// other files get the enums and the struct.\n")
    o.append("#pragma once\n#include \"platform.h\"\n\n")
    o.append("enum {\n  " + ", ".join(f"TC_{c}" + (" = 0" if i == 0 else "") for i, c in enumerate(CONDS)) + "\n};\n")
    o.append("enum {\n  TD_NONE, " + ", ".join(f"TD_{t}" for t in TD) + ", TD_COUNT\n};\n")
    o.append("enum {   // kind of the last quest line (quest.cpp quest_chat_kind), the argument of TC_QREPLY\n  "
             + ", ".join(f"QK_{k}" + (" = 0" if i == 0 else "") for i, k in enumerate(QK)) + ", QK_COUNT\n};\n")
    o.append("enum { TS_HERO = 0x80 };\n")
    o.append("typedef struct { uint8_t cond, arg; uint16_t pack; } TalkScript;   // cond | TS_HERO if the hero opens; pack: bits 0..13 blob offset of the first turn, 14..15 turns - 1\n\n")
    o.append(f"#define TALK_NSCRIPTS {len(scripts)}\n#define TALK_NLINES {len(loff) - 1}\n#define TALK_DICT_N {len(dic)}\n")
    o.append(f"#define TALK_BLOB_BYTES {len(blob)}\n")
    o.append("// groups of TALK_FIRST: 0..8 villager 1..9, 9 shared pool, 10 hero alone (BED, WAKE, SOLODID), 11 quest answers, 12 end\n\n")
    o.append("#ifdef TALK_DATA_IMPL\n")
    o.append("static const uint8_t TALK_FIRST[13] GC_ROM_ATTR = {" + ", ".join(str(x) for x in first) + "};\n")
    o.append("static const TalkScript TALK_SCRIPTS[TALK_NSCRIPTS] GC_ROM_ATTR = {\n")
    for s, f in zip(scripts, meta):
        pack = loff[f] | ((len(s.turns) - 1) << 14)
        o.append(f"  {{TC_{s.cond}{' | TS_HERO' if s.hero_opens else ''}, {s.arg}, {pack}}},  // {s.speaker} {s.src.split(':')[1] if ':' in s.src else ''}\n")
    o.append("};\n\n")
    o.append("static const uint8_t TALK_BLOB[TALK_BLOB_BYTES] GC_ROM_ATTR = {")
    for i, v in enumerate(blob):
        o.append(("\n  " if i % 24 == 0 else " ") + f"{v},")
    o.append("\n};\n\n")
    dblob = "".join(dic)
    doff = [0]
    for d in dic:
        doff.append(doff[-1] + len(d))
    o.append("static const uint16_t TALK_DOFF[TALK_DICT_N + 1] GC_ROM_ATTR = {" + ", ".join(str(x) for x in doff) + "};\n")
    o.append("static const char TALK_DICT[] GC_ROM_ATTR =\n" + "".join(f'  "{d}"\n' for d in dic) + ";\n")
    o.append("static_assert(TALK_BLOB_BYTES <= 12288, \"dialogue blob over budget (design 4.3)\");\n")
    o.append("static_assert(TALK_NSCRIPTS <= 255, \"script index must fit a byte\");\n")
    o.append("#endif\n")
    return "".join(o), first, len(blob)


def main(argv):
    check = "--check" in argv
    verify = "--verify" in argv
    strict = "--strict" in argv
    files = [a for a in argv if not a.startswith("--")]
    per_file = {}
    only = None
    if files:
        only = set()
        for f in files:
            n = pathlib.Path(f).stem
            if n not in SPEAKERS:
                print(f"unknown speaker file {f}")
                return 2
            only.add(n)
    for name in SPEAKERS:
        if only is not None and name not in only:
            continue
        p = CONTENT / f"{name}.txt"
        if only is not None:
            for f in files:
                if pathlib.Path(f).stem == name:
                    p = pathlib.Path(f)
        if p.exists():
            per_file[name] = parse_file(name, p)
    for name, sc in per_file.items():
        validate(name, sc, True)
    # no line occurs twice anywhere (compared as flattened text)
    seen = {}
    for name, sc in per_file.items():
        for s in sc:
            for side, text, w in s.turns:
                key = text.replace("|", " ")
                if key in seen:
                    err(w, f"duplicate line, also at {seen[key]}: `{text}`")
                else:
                    seen[key] = w
    print(category_table(per_file))
    print("size note: the blob is capped at 12,288 bytes (about 21 bytes per turn after compression); mira.txt averages 34 characters\n"
          "per turn, so keep your average at or below that (column avg chars) and prefer short turns.")
    miss = missing_report(per_file) if only is None else []
    if miss:
        print("still missing:\n" + "\n".join(miss))
    if strict and miss:
        err("strict", "speaker files are missing")
    allsc = [s for n in SPEAKERS for s in per_file.get(n, [])]
    if not ERRORS and allsc and only is None:
        scripts, meta, dic, blob, loff = compile_all(per_file)
        if not ERRORS:
            text, first, nb = render_header(scripts, meta, dic, blob, loff)
            print(f"{len(scripts)} scripts, {len(loff) - 1} lines, blob {nb} bytes, dictionary {len(dic)} entries "
                  f"({sum(len(d) for d in dic)} bytes)")
            if verify:
                if not OUT.exists() or OUT.read_text() != text:
                    err("verify", f"{OUT.relative_to(ROOT)} is stale: run python3 tools/gen_talk.py")
            elif not check:
                OUT.write_text(text)
                print(f"wrote {OUT.relative_to(ROOT)}")
    if ERRORS:
        print("\nERRORS:")
        for e in ERRORS[:80]:
            print("  " + e)
        if len(ERRORS) > 80:
            print(f"  ... {len(ERRORS) - 80} more")
        return 1
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
