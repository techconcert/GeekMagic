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



# ---------------------------------------------------------------- village
def blank():
    return [[0] * 16 for _ in range(16)]


def put(t, x, y, ch):
    if 0 <= x < 16 and 0 <= y < 16:
        t[y][x] = CHAR_TO_INDEX[ch]


def roof(side, dark, main):
    t = blank()
    for y in range(16):
        for x in range(16):
            # slope on the outer edge of the building
            if side == "L" and x < 7 - y // 2:
                continue
            if side == "R" and 15 - x < 7 - y // 2:
                continue
            c = main
            if y % 4 == 3 or (x + (y // 4) * 4) % 8 == 0:
                c = dark
            put(t, x, y, c)
        # outline the slope
        if side == "L":
            put(t, max(0, 7 - y // 2), y, "k")
        if side == "R":
            put(t, min(15, 15 - (7 - y // 2)), y, "k")
    for x in range(16):
        if t[0][x]:
            put(t, x, 0, "k")
        put(t, x, 15, "k")
    return t


def wall(kind):
    t = [[CHAR_TO_INDEX["o"]] * 16 for _ in range(16)]
    for x in range(16):
        put(t, x, 0, "b")
        put(t, x, 15, "a")
        put(t, x, 14, "a")
    if kind in ("L", "LW"):
        for y in range(16):
            put(t, 0, y, "k"); put(t, 1, y, "b")
    if kind in ("R", "RW"):
        for y in range(16):
            put(t, 15, y, "k"); put(t, 14, y, "b")
    if kind in ("LW", "RW", "W"):
        for y in range(4, 11):
            for x in range(5, 11):
                edge = y in (4, 10) or x in (5, 10) or x == 7 or y == 7
                put(t, x, y, "k" if edge else "f")
        put(t, 6, 5, "o")
    if kind == "D":
        for y in range(5, 16):
            for x in range(5, 11):
                edge = y == 5 or x in (5, 10)
                put(t, x, y, "k" if edge else ("b" if x == 7 else "B"))
        put(t, 9, 11, "Y")
    return t


def fence():
    t = blank()
    for x in range(16):
        for y in (6, 7, 11, 12):
            put(t, x, y, "B" if y in (6, 11) else "b")
    for px in (2, 13):
        for y in range(3, 15):
            put(t, px, y, "B"); put(t, px - 1, y, "k"); put(t, px + 1, y, "b")
        put(t, px, 2, "k")
    return t


def dock():
    t = [[CHAR_TO_INDEX["B"]] * 16 for _ in range(16)]
    for y in range(16):
        for x in range(16):
            if x % 4 == 3:
                put(t, x, y, "b")
        if y % 8 == 0:
            for x in (1, 5, 9, 13):
                put(t, x, y + 2, "k")
    return t


BENCH = grid([
    "................",
    "................",
    "................",
    "................",
    "................",
    "..kkkkkkkkkkkk..",
    ".kBBBBBBBBBBBBk.",
    ".kbbbbbbbbbbbbk.",
    "..kkkkkkkkkkkk..",
    ".kBBBBBBBBBBBBk.",
    ".kbbbbbbbbbbbbk.",
    "..kbk......kbk..",
    "..kbk......kbk..",
    "..kbk......kbk..",
    "..kkk......kkk..",
    "................",
])

STUMP = grid([
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "....kkkkkkkk....",
    "...ksssBBsssk...",
    "...kBsBBBBsBk...",
    "...kbBBBBBBbk...",
    "...kbBbBBbBbk...",
    "...kbbbbbbbbk...",
    "..gkkkkkkkkkkg..",
    "................",
    "................",
])

LOGPILE = grid([
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "......kkkk......",
    ".....ksBBsk.....",
    ".....kBssBk.....",
    "...kkkkkkkkkk...",
    "..ksBBskksBBsk..",
    "..kBssBkkBssBk..",
    "kkkkkkkkkkkkkkkk",
    "ksBBskksBBskksBk",
    "kBssBkkBssBkkBsk",
    "kkkkkkkkkkkkkkkk",
])

CAMPFIRE = [grid([
    "................",
    "................",
    "................",
    "................",
    ".......Y........",
    "......YrY.......",
    "......YrrY......",
    ".....YrYrrY.....",
    ".....rYYYrY.....",
    "....rYYoYYr.....",
    "....rYoooYr.....",
    "...kaakkkaak....",
    "..kaBbBbBbBak...",
    "..kabBbBbBbak...",
    "...kaakkkaak....",
    "................",
]), grid([
    "................",
    "................",
    "................",
    "................",
    "........Y.......",
    ".......YrY......",
    "......YrrY......",
    ".....YrrYrY.....",
    ".....YrYYYr.....",
    ".....rYYoYYr....",
    ".....rYoooYr....",
    "...kaakkkaak....",
    "..kaBbBbBbBak...",
    "..kabBbBbBbak...",
    "...kaakkkaak....",
    "................",
])]


def sign(icon):
    t = grid([
        "................",
        ".kkkkkkkkkkkkkk.",
        ".kBBBBBBBBBBBBk.",
        ".kBBBBBBBBBBBBk.",
        ".kBBBBBBBBBBBBk.",
        ".kBBBBBBBBBBBBk.",
        ".kBBBBBBBBBBBBk.",
        ".kBBBBBBBBBBBBk.",
        ".kbbbbbbbbbbbbk.",
        ".kkkkkkkkkkkkkk.",
        "......kbbk......",
        "......kbbk......",
        "......kbbk......",
        "......kbbk......",
        ".....gkkkkg.....",
        "................",
    ])
    for y, row in enumerate(icon):
        for x, ch in enumerate(row):
            if ch != ".":
                put(t, 5 + x, 3 + y, ch)
    return t


COIN_ICON = ["kYYk", "YYoY", "YYYY", "kYYk"]
MUG_ICON = ["oooo.", "YYYYk", "YYYYk", "YYYY."]

# items / effects (16x16 cells)
AXE_UP = grid([
    "................",
    "..........kkk...",
    ".........kaaok..",
    "........kaaaak..",
    ".........kbaak..",
    "........kbk.k...",
    ".......kbk......",
    "......kbk.......",
    ".....kbk........",
    "....kbk.........",
    "....kk..........",
    "................",
    "................",
    "................",
    "................",
    "................",
])
AXE_DOWN = grid([
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "kk..............",
    "kbkk............",
    ".kbbkk..........",
    "..kkbbkk.kk.....",
    "....kkbbkaak....",
    "......kkaaaak...",
    ".......kaaaak...",
    "........kaok....",
    ".........kk.....",
    "................",
])
ROD = grid([
    "..............ok",
    ".............ko.",
    "............kbo.",
    "...........kbk.o",
    "..........kbk..o",
    ".........kbk...o",
    "........kbk....o",
    ".......kbk.....o",
    "......kbk......o",
    ".....kbk.......o",
    "....kbk........o",
    "...kbk.........o",
    "..kbk..........o",
    "..kk...........o",
    "...............o",
    "...............o",
])
BOBBER = grid([
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "......kk........",
    ".....krrk.......",
    ".....kook.......",
    "......kk........",
    "....ff..ff......",
    "................",
    "................",
    "................",
    "................",
])
LOG = grid([
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "...kkkkkkkkkk...",
    "..ksBBBBBBBBBk..",
    "..kBssBBbBBBBk..",
    "..kBssBBBBbBBk..",
    "..ksBBBBBBBBBk..",
    "...kkkkkkkkkk...",
    "................",
    "................",
])
BUBBLE = grid([
    "................",
    "..kkkkkkkkkkkk..",
    ".kooooooooooook.",
    ".kooooooooooook.",
    ".kookkookkookok.",
    ".kookkookkookok.",
    ".kooooooooooook.",
    "..kkkkkookkkkk..",
    ".......kok......",
    "........k.......",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
])
ZZZ = grid([
    "................",
    "........kkkkkkk.",
    "........kooooook",
    "........kkkkook.",
    "..........kook..",
    ".........kook...",
    "........kookkkk.",
    "........kooooook",
    "...kkkkk.kkkkkk.",
    "...koooak.......",
    "....kkoak.......",
    "....koak........",
    "...kooakk.......",
    "...kooooak......",
    "....kkkkk.......",
    "................",
])
FISH = grid([
    "................",
    "................",
    "................",
    "................",
    "................",
    "....kkkkk...kk..",
    "...kfWWWWk.kWk..",
    "..kfWkWWWWkWWk..",
    "..kWWWWWWWWWk...",
    "..kwWWWWWWkWWk..",
    "...kwwwwwk.kWk..",
    "....kkkkk...kk..",
    "................",
    "................",
    "................",
    "................",
])
COIN = grid([
    "................",
    "................",
    "................",
    "....kkkkkk......",
    "...kYYYYYYk.....",
    "..kYYoYYYYYk....",
    "..kYoYYYYYbk....",
    "..kYYYYYYYbk....",
    "..kYYYYYYYbk....",
    "..kYYYYYYbbk....",
    "...kYbbbbbk.....",
    "....kkkkkk......",
    "................",
    "................",
    "................",
    "................",
])
WOOD = grid([
    "................",
    "................",
    "................",
    "................",
    "...kkkkkkkkk....",
    "..ksBBBBBBBBk...",
    "..kBsBbBBBBBk...",
    "..ksBBBBBbBBk...",
    "..kkkkkkkkkkkk..",
    "...ksBBBBBBBBk..",
    "...kBsBBBbBBBk..",
    "...ksBBBBBBBBk..",
    "....kkkkkkkkk...",
    "................",
    "................",
    "................",
])
HEART = grid([
    "................",
    "................",
    "................",
    "...kkk...kkk....",
    "..krrrk.krrrk...",
    "..kroorkrrrrk...",
    "..krorrrrrrrk...",
    "..krrrrrrrrrk...",
    "...krrrrrrrk....",
    "....krrrrrk.....",
    ".....krrrk......",
    "......krk.......",
    ".......k........",
    "................",
    "................",
    "................",
])


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
        roof("L", "w", "W"), roof("M", "w", "W"), roof("R", "w", "W"),   # 8-10 blue roof
        roof("L", "b", "r"), roof("M", "b", "r"), roof("R", "b", "r"),   # 11-13 red roof
        roof("L", "b", "B"), roof("M", "b", "B"), roof("R", "b", "B"),   # 14-16 brown roof
        wall("LW"), wall("D"), wall("RW"),          # 17-19 wall: window, door, window
        fence(),                                    # 20 fence
        dock(),                                     # 21 dock (walkable, over water)
        BENCH,                                      # 22 bench
        STUMP,                                      # 23 stump
        LOGPILE,                                    # 24 log pile
        CAMPFIRE[0], CAMPFIRE[1],                   # 25-26 campfire
        sign(COIN_ICON), sign(MUG_ICON),            # 27-28 shop / tavern signs
    ]
    save_sheet(os.path.join(OUT, "tiles.png"), [tiles[i:i + 8] for i in range(0, len(tiles), 8)])
    save_sheet(os.path.join(OUT, "hero.png"), hero_frames())
    items = [AXE_UP, AXE_DOWN, ROD, BOBBER, LOG, BUBBLE, ZZZ, FISH, COIN, WOOD, HEART]
    save_sheet(os.path.join(OUT, "items.png"), [items[i:i + 8] for i in range(0, len(items), 8)])


if __name__ == "__main__":
    main()
