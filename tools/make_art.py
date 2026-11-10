"""Generates the starter pixel art as editable PNG sheets in assets/src/.

This is only a bootstrap: once the PNGs exist you can edit them directly in
Aseprite/LibreSprite (load assets/palette.gpl) and never run this again.
Running it OVERWRITES assets/src/tiles.png and assets/src/hero.png.

Sheets use a 16x16 grid. Magenta (#FF00FF) = transparent.
"""
import os
import random
from PIL import Image

from palette import PALETTE, CHAR_TO_INDEX

ROOT = os.path.join(os.path.dirname(__file__), "..")
OUT = os.path.join(ROOT, "assets", "src")


def grid(rows):
    rows = [r for r in rows]
    assert len(rows) == 16, f"need 16 rows, got {len(rows)}"
    for r in rows:
        assert len(r) == 16, f"row {r!r} is {len(r)} wide"
        for c in r:
            assert c in CHAR_TO_INDEX, f"unknown colour char {c!r}"
    return [[CHAR_TO_INDEX[c] for c in r] for r in rows]


def noise_tile(base, specks, seed):
    rnd = random.Random(seed)
    t = [[CHAR_TO_INDEX[base]] * 16 for _ in range(16)]
    for ch, n in specks:
        for _ in range(n):
            t[rnd.randrange(16)][rnd.randrange(16)] = CHAR_TO_INDEX[ch]
    return t


def grass_tufts(seed):
    t = noise_tile("G", [("g", 10), ("l", 6)], seed)
    rnd = random.Random(seed + 99)
    for _ in range(3):
        x, y = rnd.randrange(1, 14), rnd.randrange(2, 15)
        t[y][x] = t[y][x + 2] = CHAR_TO_INDEX["g"]
        t[y - 1][x + 1] = CHAR_TO_INDEX["l"]
        t[y][x + 1] = CHAR_TO_INDEX["g"]
    return t


def grass_flowers(seed):
    t = noise_tile("G", [("g", 10), ("l", 5)], seed)
    rnd = random.Random(seed + 7)
    for col in ("o", "Y", "r"):
        x, y = rnd.randrange(1, 15), rnd.randrange(1, 15)
        t[y][x] = CHAR_TO_INDEX[col]
        t[y - 1][x] = t[y + 1][x] = t[y][x - 1] = t[y][x + 1] = CHAR_TO_INDEX["o" if col != "o" else "Y"]
    return t


def water(frame):
    t = [[CHAR_TO_INDEX["W"]] * 16 for _ in range(16)]
    for y in range(16):
        for x in range(16):
            if (x + y * 3) % 11 == 0:
                t[y][x] = CHAR_TO_INDEX["w"]
    off = 0 if frame == 0 else 4
    for y in (3, 11):
        for i in range(4):
            t[y][(off + i + (y * 2)) % 16] = CHAR_TO_INDEX["f"]
    for y in (7, 15):
        for i in range(3):
            t[y][(off + 8 + i + y) % 16] = CHAR_TO_INDEX["f"]
    return t


TREE = grid([
    ".....kkkkkk.....",
    "...kkGGGlGGkk...",
    "..kGGlGGGGGGGk..",
    ".kGlGGGGGlGGGgk.",
    ".kGGGGlGGGGGggk.",
    "kGGGGGGGGGlGGggk",
    "kGlGGGGGGGGGgggk",
    "kGGGGGGlGGGggggk",
    ".kgGGGGGGGgggggk",
    ".kggGGGGGggggk..",
    "..kkggggggggkk..",
    "....kkkbBkkk....",
    "......kbBk......",
    "......kbBk......",
    ".....kbbBBk.....",
    "......kkkk......",
])

ROCK = grid([
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    ".....kkkkk......",
    "....kaooaak.....",
    "...kaoaaaaak....",
    "...kaaaaaaakk...",
    "..kaaaaaaaaaak..",
    "..kaaaaaaaaaak..",
    "..kkaaaaaaaakk..",
    "...kkkkkkkkkk...",
    "................",
    "................",
])

# ---- hero: rows = down, up, left ; cols = stand, walk1, walk2 ----
# (right is drawn by mirroring left at runtime)
HEAD_DOWN = [
    "................",
    ".....kkkkkk.....",
    "....kbBBBBbk....",
    "...kbBBBBBBbk...",
    "...kbbppppbbk...",
    "...kbpkppkpbk...",
    "....kppppppk....",
    ".....kkppkk.....",
]
HEAD_UP = [
    "................",
    ".....kkkkkk.....",
    "....kbBBBBbk....",
    "...kbBBBBBBbk...",
    "...kbBBBBBBbk...",
    "...kbBBBBBBbk...",
    "....kbbbbbbk....",
    ".....kkkkkk.....",
]
BODY_FRONT = [
    "....kWWWWWWk....",
    "...kpWWYYWWpk...",
    "...kpkWWWWkpk...",
    "....kkWWWWkk....",
]
BODY_BACK = [
    "....kWWWWWWk....",
    "...kpWWWWWWpk...",
    "...kpkWWWWkpk...",
    "....kkWWWWkk....",
]
LEGS_FRONT = [
    [
        "....kkbbbbkk....",
        "....kbk..kbk....",
        "....kbk..kbk....",
        "....kkk..kkk....",
    ],
    [
        "....kkbbbbkk....",
        "....kbk..kbk....",
        "....kkk..kbk....",
        ".........kkk....",
    ],
    [
        "....kkbbbbkk....",
        "....kbk..kbk....",
        "....kbk..kkk....",
        "....kkk.........",
    ],
]
SIDE = [
    "................",
    "......kkkkk.....",
    ".....kBBBBBk....",
    "....kBBBBBBbk...",
    "....kppBBBBbk...",
    "...kpkppBBbk....",
    "...kpppppbk.....",
    "....kkpppk......",
    ".....kWWWWk.....",
    "....kWWWWWWk....",
    "....kWpWWWWk....",
    ".....kWWWWk.....",
]
LEGS_SIDE = [
    [
        ".....kbbbk......",
        ".....kbbbk......",
        ".....kbbbk......",
        ".....kkkkk......",
    ],
    [
        ".....kbbbk......",
        "....kbk.kbk.....",
        "...kbk...kbk....",
        "...kk.....kk....",
    ],
    [
        ".....kbbbk......",
        ".....kbkbk......",
        "....kbk.kbk.....",
        "....kk...kk.....",
    ],
]


def hero_frames():
    rows = []
    rows.append([grid(HEAD_DOWN + BODY_FRONT + LEGS_FRONT[i]) for i in range(3)])
    rows.append([grid(HEAD_UP + BODY_BACK + LEGS_FRONT[i]) for i in range(3)])
    rows.append([grid(SIDE + LEGS_SIDE[i]) for i in range(3)])
    return rows


def save_sheet(path, rows):
    h = len(rows) * 16
    w = max(len(r) for r in rows) * 16
    img = Image.new("RGB", (w, h), PALETTE[0][2])
    for ty, row in enumerate(rows):
        for tx, tile in enumerate(row):
            for y in range(16):
                for x in range(16):
                    img.putpixel((tx * 16 + x, ty * 16 + y), PALETTE[tile[y][x]][2])
    img.save(path)
    print("wrote", os.path.relpath(path, ROOT))


def main():
    os.makedirs(OUT, exist_ok=True)
    tiles = [
        noise_tile("G", [("g", 14), ("l", 8)], 1),  # 0 grass
        grass_tufts(2),                             # 1 grass tufts
        grass_flowers(3),                           # 2 flowers
        noise_tile("s", [("B", 10), ("o", 4)], 4),  # 3 path
        water(0),                                   # 4 water frame 0
        water(1),                                   # 5 water frame 1
        TREE,                                       # 6 tree (object layer)
        ROCK,                                       # 7 rock (object layer)
    ]
    save_sheet(os.path.join(OUT, "tiles.png"), [tiles])
    save_sheet(os.path.join(OUT, "hero.png"), hero_frames())


if __name__ == "__main__":
    main()
