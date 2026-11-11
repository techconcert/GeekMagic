"""Paints the real art for every placeholder cell of docs/DESIGN.md 10.5 (chunk 7).

    python tools/paint_art.py      (then: python tools/png2tiles.py)

Reads assets/src/{tiles,chars,items}.png, paints every cell listed in EXPECTED
(cell ids are the ones of DESIGN.md 10.5) and writes the sheets back. Cells that
are not listed (tiles 0..29, hero cells 0..5 and 8..10, items 0..10, I_STAR,
I_FACE and the spare chars cell 63) are never touched, so the script is
idempotent: running it twice gives the same PNGs.

Every pixel is a colour of tools/palette.py; '.' is transparent (magenta in the
PNG). Letters:  k outline  g dark green  G green  l light green  b dark brown
B brown  s sand  w dark blue  W blue  f foam  p skin  r red  o white  a grey
Y yellow.  In S() blocks a space means "leave the pixel as it is".

Do NOT run tools/make_art.py: it overwrites the sheets with the bootstrap art.
"""
import os
import random
import sys
from PIL import Image

from palette import PALETTE, CHAR_TO_INDEX

ROOT = os.path.join(os.path.dirname(__file__), "..")
SRC = os.path.join(ROOT, "assets", "src")

# ------------------------------------------------------------------ helpers


def blank():
    return [["."] * 16 for _ in range(16)]


def fill(ch):
    return [[ch] * 16 for _ in range(16)]


def S(rows, x=0, y=0, base=None):
    """Draw a block of ASCII rows (any width) at (x, y); space = keep."""
    c = base if base is not None else blank()
    for j, r in enumerate(rows):
        for i, ch in enumerate(r):
            if ch == " ":
                continue
            assert 0 <= x + i < 16 and 0 <= y + j < 16, (x, y, i, j, r)
            assert ch in CHAR_TO_INDEX, ch
            c[y + j][x + i] = ch
    return c


def noise(base, specks, seed):
    rnd = random.Random(seed)
    c = fill(base)
    for ch, n in specks:
        for _ in range(n):
            c[rnd.randrange(16)][rnd.randrange(16)] = ch
    return c


def rect(c, x0, y0, x1, y1, ch):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            c[y][x] = ch
    return c


def dots(c, pts, ch):
    for (x, y) in pts:
        c[y][x] = ch
    return c


def water_base(seed_off=0):
    c = fill("W")
    for y in range(16):
        for x in range(16):
            if (x + y * 3) % 11 == 0:
                c[y][x] = "w"
    return c


def brick(c, rows_y, offsets, outline="k"):
    """Horizontal mortar lines at rows_y and vertical joints per band."""
    for y in rows_y:
        rect(c, 0, y, 15, y, outline)
    for y, xs in offsets:
        for x in xs:
            rect(c, x, y, x, y + 3, outline)
    return c


T, C, I = {}, {}, {}

# ================================================================== TILES
# 30 T_ICE
c = noise("f", [("o", 14), ("W", 6)], 30)
dots(c, [(2, 3), (3, 4), (4, 4), (5, 5), (10, 10), (11, 11), (12, 11), (13, 12)], "W")
dots(c, [(2, 2), (3, 2), (9, 9), (10, 9)], "o")
T[30] = c

# 31 T_SNOWMAN
T[31] = S([
    "   kkkk   ",
    "   kbbk   ",
    "  kkkkkk  ",
    "  kooook  ",
    " kokoookk ",
    " kooorrok ",
    "  kooook  ",
    " kkrrrkkk ",
    "kooooooook",
    "kooookooak",
    "kooooooook",
    " kooooook ",
    "  kkkkkk  ",
], 3, 2)

# 32 T_BOARD
T[32] = S([
    "kkkkkkkkkkkkkk",
    "kBBBBBBBBBBBBk",
    "kBooBrrBooBYBk",
    "kBooBrrBooBYBk",
    "kBooBBBBkkBBBk",
    "kBBBBBBBBBBBBk",
    "kkkkkkkkkkkkkk",
    "  kbk    kbk  ",
    "  kbk    kbk  ",
    "  kbk    kbk  ",
    "  kkk    kkk  ",
], 1, 3)

# 33 T_WELL
T[33] = S([
    "  kkkkkkkk  ",
    " krrrrrrrrk ",
    "krrrrrrrrrrk",
    "kkkkkkkkkkkk",
    " kb      bk ",
    " kb  kk  bk ",
    " kkaaooaakk ",
    "kaooaaaaaoak",
    "kaaWWWWWWaak",
    "kaaWwwwwWaak",
    "kkaaaaaaaakk",
    " kkkkkkkkkk ",
], 2, 3)

# 34 / 35 T_LANTERN_POST (unlit, lit)
POST = ["  kbk ", "  kbk ", "  kbk ", "  kbk ", "  kbk ", "  kbk ", " kbbbk", " kkkkk"]
T[34] = S(["  kk  ", " kaak ", "kaWWak", "kaWWak", "kaaaak", " kkkk "] + POST, 5, 1)
T[35] = S(["  kk  ", " kYYk ", "kYooYk", "kYooYk", "kYYYYk", " kkkk "] + POST, 5, 1)
dots(T[35], [(4, 3), (11, 3), (4, 5), (11, 5), (3, 4), (12, 4)], "Y")

# 36 / 37 T_BRIDGE_H / T_BRIDGE_V (opaque planks over water)
c = water_base()
rect(c, 0, 1, 15, 14, "k")
rect(c, 0, 3, 15, 12, "B")
for x in range(0, 16, 4):
    rect(c, x, 3, x, 12, "b")
dots(c, [(x, 4) for x in range(16) if x % 4], "s")
dots(c, [(x, 11) for x in range(16) if x % 4], "b")
T[36] = c
c = water_base()
rect(c, 1, 0, 14, 15, "k")
rect(c, 3, 0, 12, 15, "B")
for y in range(0, 16, 4):
    rect(c, 3, y, 12, y, "b")
dots(c, [(4, y) for y in range(16) if y % 4], "s")
dots(c, [(11, y) for y in range(16) if y % 4], "b")
T[37] = c


# 38..40 T_CROP_0..2 (opaque soil)
def soil():
    c = fill("b")
    for y in range(1, 16, 5):
        rect(c, 0, y, 15, y + 1, "B")
    dots(c, [(3, 4), (9, 9), (13, 14), (6, 13), (12, 3)], "k")
    return c


T[38] = S([" gg ", "gllg", " kgk", "  g "], 6, 5, soil())
T[39] = S([
    "g   l  g ",
    "gk lGk gl",
    "kGgGGgGkG",
    " kkgGgkk ",
    "   kgk   ",
], 3, 4, soil())
T[40] = S([
    "Y  Y   Y ",
    "sY sY sY ",
    "YsYYsYYsY",
    "kYkkYkkYk",
    " k gk gk ",
    " k gk gk ",
], 3, 4, soil())
dots(T[40], [(4, 9), (7, 9), (10, 9)], "G")


# 41 / 42 T_STALL_L / T_STALL_R (awning stripes continue across the halves)
def stall(left):
    c = blank()
    for x in range(16):
        ch = "r" if (x // 2) % 2 == 0 else "o"
        c[2][x] = c[6][x] = "k"
        for y in (3, 4, 5):
            c[y][x] = ch
        if x % 2 == 0:
            c[7][x] = ch
            c[8][x] = "k"
    rect(c, 0, 10, 15, 13, "k")
    rect(c, 0, 11, 15, 12, "B")
    rect(c, 0, 11, 15, 11, "b")
    edge = 0 if left else 15
    inner = 1 if left else 14
    rect(c, edge, 8, edge, 15, "k")
    rect(c, inner, 8, inner, 9, "b")
    if left:
        rect(c, 1, 14, 3, 15, "b")
        S(["  rr  ", " rkkr "], 5, 7, c)
        dots(c, [(4, 9), (5, 9), (6, 9)], "r")
        dots(c, [(9, 9), (10, 9), (11, 9), (12, 9), (13, 9)], "Y")
    else:
        rect(c, 12, 14, 14, 15, "b")
        S(["  WW  ", " WffW "], 4, 7, c)
        dots(c, [(8, 9), (9, 9), (10, 9)], "g")
        dots(c, [(2, 9), (3, 9)], "o")
    return c


T[41] = stall(True)
T[42] = stall(False)

# 43 T_FLOWERBED
T[43] = S([
    "  kkkkkkkkkkkk  ",
    " kBBBBBBBBBBBBk ",
    "kBkbbbbbbbbbbkBk",
    "kbbrbbYbbobbrbbk",
    "kbrrrbYYbooobrrk",
    "kbbrbbYbbobbrbbk",
    "kBkbbbbbbbbbbkBk",
    " kkBBBBBBBBBBkk ",
    "  kkkkkkkkkkkk  ",
], 0, 4)
dots(T[43], [(3, 7), (9, 7), (12, 7)], "g")
dots(T[43], [(4, 10), (7, 10), (11, 10)], "G")

# 44..46 bell tower: base, top, top swinging
T[44] = S([
    " kb          bk ",
    " kb          bk ",
    " kb          bk ",
    " kb          bk ",
    " kb    BB    bk ",
    " kb    Bb    bk ",
    " kb          bk ",
    " kb          bk ",
    " kb          bk ",
    " kb          bk ",
    " kb          bk ",
    " kkkkkkkkkkkkkk ",
    "kaooaaaaaaaaaaak",
    "kaaaaaaaaaaaaaak",
    "kkkkkkkkkkkkkkkk",
], 0, 0)


def bell_top(sway):
    c = blank()
    rect(c, 1, 0, 14, 2, "k")
    rect(c, 2, 1, 13, 1, "b")
    rect(c, 1, 3, 1, 15, "k")
    rect(c, 14, 3, 14, 15, "k")
    rect(c, 2, 3, 2, 15, "b")
    rect(c, 13, 3, 13, 15, "b")
    rect(c, 7, 2, 8, 4, "k")
    S([
        "  kYYk  ",
        " kYooYk ",
        " kYoYYk ",
        "kYoYYYYk",
        "kYYYYYYk",
        "kkkkkkkk",
    ], 4 + sway, 5, c)
    dots(c, [(7 + sway, 11), (8 + sway, 11)], "k")
    dots(c, [(7 + sway * 2, 12), (8 + sway * 2, 12)], "Y")
    return c


T[45] = bell_top(0)
T[46] = bell_top(1)


# 47 / 48 T_FOUNTAIN (two water frames)
def fountain(f):
    c = S([
        "   kkkkkkkkkk   ",
        " kkaooooooooakk ",
        "kaoWWWWWWWWWWoak",
        "kaWWWfWWWWfWWWak",
        "kaWWWWWWWWWWWWak",
        "kaoWWWWWWWWWWoak",
        " kaaWWWWWWWWaak ",
        "  kkaaaaaaaakk  ",
        "    kkkkkkkk    ",
    ], 0, 7)
    S(["kaak", "kaok", "kaak", "kkkk"], 6, 6, c)
    if f:
        dots(c, [(7, 2), (8, 2)], "o")
        dots(c, [(7, 3), (8, 3), (6, 4), (9, 4), (5, 5), (10, 5)], "f")
    else:
        dots(c, [(7, 3), (8, 3)], "o")
        dots(c, [(7, 4), (8, 4), (5, 4), (10, 4), (4, 6), (11, 6)], "f")
    return c


T[47] = fountain(0)
T[48] = fountain(1)


# 49 / 50 T_STAGE_L / T_STAGE_R (bunting, red banner, planks)
def stage(left):
    c = blank()
    rect(c, 0, 1, 15, 1, "k")
    for i, x in enumerate(range(1, 16, 3)):
        ch = "rYW"[i % 3]
        S(["kkk", ch * 3, " " + ch + " "], x - 1, 2, c)
    rect(c, 0, 5, 15, 5, "k")
    rect(c, 0, 6, 15, 8, "B")
    rect(c, 0, 9, 15, 11, "s")
    rect(c, 0, 9, 15, 9, "o")
    for x in range(0, 16, 5):
        rect(c, x, 10, x, 11, "B")
    rect(c, 0, 12, 15, 15, "b")
    rect(c, 0, 12, 15, 12, "k")
    rect(c, 0, 15, 15, 15, "k")
    for x in range(1, 16, 4):
        dots(c, [(x, 13), (x, 14)], "k")
    edge = 0 if left else 15
    rect(c, edge, 5, edge, 15, "k")
    return c


T[49] = stage(True)
T[50] = stage(False)
BANNER = ["rrrrr", "rrYrr", "rYYYr", "rrYrr"]
S(BANNER, 11, 6, T[49])
S(BANNER, 0, 6, T[50])

# 51 T_FEST_TABLE
T[51] = S([
    "kkkkkkkkkkkkkkkk",
    "kooooooooooooook",
    "kooooooooooooook",
    "kkkkkkkkkkkkkkkk",
    "kooooooooooooook",
    "kkbkoooooooookbk",
    " kbk kkkkkkk kbk",
    " kbk         kbk",
    " kkk         kkk",
], 0, 6)
dots(T[51], [(3, 7), (4, 7), (7, 7), (8, 7), (11, 7), (12, 7)], "k")
dots(T[51], [(3, 8), (4, 8)], "r")
dots(T[51], [(7, 8), (8, 8)], "Y")
dots(T[51], [(11, 8), (12, 8)], "g")
dots(T[51], [(3, 6), (4, 6), (7, 6), (8, 6), (11, 6), (12, 6)], "k")

# 52 T_FEST_LIGHTS : string of lit bulbs
c = blank()
for x in range(16):
    c[2 + (1 if 4 < x < 11 else 0)][x] = "k"
for x, ch in ((1, "r"), (4, "Y"), (8, "W"), (12, "G"), (15, "r")):
    y = 3 + (1 if 4 < x < 11 else 0)
    S(["k", ch, ch], x, y, c)
T[52] = c

# 53 T_BUSH
T[53] = S([
    "   kkkkkkkk   ",
    "  kGGlGGGGGk  ",
    " kGGGGGGlGGGk ",
    "kGGlGGrGGGGGGk",
    "kGGGGGGGGGrGGk",
    "kgGGGlGGGGGGgk",
    "kggGGGGGGlGggk",
    " kgggGGGGgggk ",
    "  kkkkkkkkkk  ",
], 1, 6)

# 54 T_MUSHROOM
T[54] = S([
    "   kkkk   ",
    "  krrrrk  ",
    " krorrorrk",
    " krrrrrrrk",
    " kkkkkkkkk",
    "   kooak  ",
    "   kooak  ",
    "  kkkkkk  ",
], 3, 7)

# 55 T_HERB
T[55] = S([
    "  l   l   ",
    " lGl lGl  ",
    "kGGkGkGGk ",
    "kgGGkGGgk ",
    " kgGGGgk  ",
    "  kkGkk   ",
    "    g     ",
    "    g     ",
], 3, 7)
dots(T[55], [(5, 7), (10, 7)], "Y")

# 56 T_PINE
T[56] = S([
    "     kk     ",
    "    kggk    ",
    "   kgGggk   ",
    "  kkgGGggk  ",
    "    kgGgk   ",
    "   kgGGggk  ",
    "  kgGGGGggk ",
    " kkkgGGgkkk ",
    "   kgGGGggk ",
    "  kgGGGGGggk",
    " kkkkgGgkkkk",
    "     kbbk   ",
    "     kbbk   ",
    "     kkkk   ",
], 2, 1)

# 57 / 58 T_SAND, T_SAND_SHELL
T[57] = noise("s", [("Y", 10), ("B", 6), ("o", 5)], 57)
T[58] = S([" kkk ", "koppk", "kpoak", " kkk "], 5, 6, noise("s", [("Y", 10), ("B", 5), ("o", 4)], 58))


# 59 / 60 T_WAVE, T_WAVE2 (shore water with foam crests)
def wave(f):
    c = fill("W")
    for y in range(16):
        for x in range(16):
            if (x * 2 + y * 5) % 13 == 0:
                c[y][x] = "w"
    off = 0 if f == 0 else 5
    for (y, w) in ((2, 5), (8, 6), (13, 4)):
        for i in range(w):
            c[y][(off + i + y * 3) % 16] = "o"
        for i in range(w + 2):
            c[y + 1][(off + i - 1 + y * 3) % 16] = "f"
    return c


T[59] = wave(0)
T[60] = wave(1)

# 61 T_REEDS
T[61] = S([
    "  kk       k  ",
    " kBBk     kBk ",
    " kBBk  kk kBk ",
    " kbBk kBBk kk ",
    "  kg  kBBk kg ",
    "  gk  kbBk kg ",
    "  gk   kg  gk ",
    "  gk   gk  gk ",
    " kgk   gk kgk ",
    " kgk   gk kgk ",
], 1, 6)

# 62 T_BOAT (drawn over water)
T[62] = S([
    "          kb    ",
    "         kBk    ",
    "         kBk    ",
    "  kkkkkkkkkkkk  ",
    " kBBBBBBBBBBBBk ",
    "  kbBBBBBBBBbk  ",
    "   kbbbbbbbbk   ",
    "    kkkkkkkk    ",
], 0, 7)
dots(T[62], [(5, 11), (8, 11), (11, 11)], "k")
dots(T[62], [(2, 15), (3, 15), (12, 15), (13, 15)], "f")
dots(T[62], [(6, 15), (7, 15), (8, 15)], "o")

# 63 T_CHEST, 76 T_CHEST_OPEN
T[63] = S([
    "kkkkkkkkkkkk",
    "kBBBBBBBBBBk",
    "kBbbbbbbbbBk",
    "kkkkkkkkkkkk",
    "kBBBBYYBBBBk",
    "kBbbbYkbbbBk",
    "kBBBBBBBBBBk",
    "kkkkkkkkkkkk",
], 2, 6)
T[76] = S([
    "  kkkkkkkkkk  ",
    " kbBBBBBBBBbk ",
    " kbBBBBBBBBbk ",
    "kkkkkkkkkkkkkk",
    "kYoYYYYoYYYoYk",
    "kYYYoYYYYYYYYk",
    "kkkkkkkkkkkkkk",
    "kBBBBYYBBBBBBk",
    "kBbbbYkbbbbbBk",
    "kBBBBBBBBBBBBk",
    "kkkkkkkkkkkkkk",
], 1, 4)
dots(T[76], [(3, 3), (12, 4), (8, 3)], "Y")

# 64..66 cave floor / wall / wall top
T[64] = noise("b", [("k", 18), ("B", 8), ("a", 6)], 64)
c = fill("a")
brick(c, (0, 5, 10, 15), [(1, (5, 12)), (6, (2, 10)), (11, (6, 14))])
dots(c, [(1, 2), (7, 7), (9, 12), (3, 13), (13, 2)], "o")
T[65] = c
c = fill("a")
rect(c, 0, 0, 15, 2, "o")
for x in range(1, 16, 3):
    c[2][x] = "a"
rect(c, 0, 3, 15, 3, "k")
brick(c, (9, 15), [(4, (5, 12)), (10, (2, 9))])
dots(c, [(2, 6), (9, 12), (14, 7), (6, 13)], "b")
T[66] = c

# 67..69 cave rocks and stalagmites (objects)
T[67] = S([
    "     kkkkk      ",
    "    kaooaak     ",
    "   kaoaaaaak    ",
    "   kaaYaaaakk   ",
    "  kaaaaaaYaaak  ",
    "  kaYaaaaaaaak  ",
    "  kkaaaaaYaakk  ",
    "   kkkkkkkkkk   ",
], 0, 6)
T[68] = S([
    "     kkkkk      ",
    "    kaooaak     ",
    "   kaoaWaaak    ",
    "   kaaWfWaakk   ",
    "  kaaaaWaaaaak  ",
    "  kaaWaaaaWaak  ",
    "  kkaWfaaaaakk  ",
    "   kkkkkkkkkk   ",
], 0, 6)
T[69] = S([
    "     kk        ",
    "    kook   kk  ",
    "    kaak  koak ",
    "   kkaaak kaak ",
    "   kaoaaakkaaak",
    "  kaoaaaaakaaak",
    " kkaoaaaaaaaaak",
    " kaaaaaaaaaaaak",
    " kkkkkkkkkkkkkk",
], 0, 7)

# 70 T_CAVE_EXIT : daylight through an arched opening
c = fill("a")
S([
    "  kkkkkk  ",
    " kffffffk ",
    "kfoooooofk",
    "kfoooooofk",
    "kfoooooofk",
    "kfoooooofk",
    "kfoooooofk",
    "kfoooooofk",
    "kBBBBBBBBk",
    "kbbbbbbbbk",
    "kkkkkkkkkk",
], 3, 2, c)
rect(c, 0, 0, 15, 0, "o")
T[70] = c

# 71 / 72 ruin floor / wall
c = fill("a")
rect(c, 0, 0, 15, 0, "k")
rect(c, 0, 8, 15, 8, "k")
for x in (0, 8):
    rect(c, x, 0, x, 7, "k")
for x in (4, 12):
    rect(c, x, 8, x, 15, "k")
rect(c, 0, 15, 15, 15, "k")
dots(c, [(2, 2), (3, 2), (10, 3), (6, 11), (13, 12), (14, 5)], "o")
dots(c, [(5, 5), (1, 13), (9, 10)], "g")
T[71] = c
c = fill("a")
brick(c, (0, 5, 10, 15), [(1, (4, 12)), (6, (8,)), (11, (4, 12))])
dots(c, [(1, 2), (6, 7), (10, 12), (14, 7)], "o")
dots(c, [(2, 3), (3, 4), (9, 8), (9, 9), (13, 13), (5, 13), (5, 14)], "g")
T[72] = c

# 73 T_RUIN_PILLAR (broken top)
T[73] = S([
    "  kk kkkkk  ",
    " kaokaooak  ",
    " kaaaaaaak  ",
    "kkkkkkkkkkk ",
    " kaoaaaaak  ",
    " kaoaaaaak  ",
    " kaoaaagak  ",
    " kaoaaaggk  ",
    " kaoaaaaak  ",
    " kaoaaaaak  ",
    "kkkkkkkkkkk ",
    "kaoaaaaaaak ",
    "kkkkkkkkkkk ",
], 2, 2)

# 74 T_RUIN_DOOR : arch with a wooden door
c = fill("a")
S([
    "  kkkkkkkkkkkk  ",
    " kaooooooooooak ",
    "kaookkkkkkkkkaak",
    "kaakkkkkkkkkkaak",
    "kaakkbbbbbbkkaak",
    "kaakkbBBBBbkkaak",
    "kaakkbBBBBbkkaak",
    "kaakkbBBYBbkkaak",
    "kaakkbBBBBbkkaak",
    "kaakkbBBBBbkkaak",
    "kaakkbBBBBbkkaak",
    "kaakkbBBBBbkkaak",
    "kkkkkkkkkkkkkkkk",
], 0, 1, c)
rect(c, 0, 0, 15, 0, "k")
dots(c, [(1, 14), (14, 14)], "g")
T[74] = c

# 75 T_ALTAR
T[75] = S([
    "    kYYk    ",
    "   kYooYk   ",
    "    kYYk    ",
    " kkkkkkkkkk ",
    "kaooooooooak",
    "kaaaaaaaaaak",
    "kkkkkkkkkkkk",
    " kaoaaaaaak ",
    " kaoaaaaaak ",
    " kaoaagaaak ",
    " kaoaaaagak ",
    "kkkkkkkkkkkk",
    "kaaaaaaaaaak",
    "kkkkkkkkkkkk",
], 2, 1)
T[75] = S([
    "    kYYk    ",
    "   kYooYk   ",
    "    kYYk    ",
    " kkkkkkkkkk ",
    "kaooooooooak",
    "kaaaaaaaaaak",
    "kkkkkkkkkkkk",
    " kaoaaaaaak ",
    " kaoaaaaaak ",
    " kaoaagaaak ",
    "kkkkkkkkkkkk",
    "kaaaaaaaaaak",
    "kkkkkkkkkkkk",
], 2, 2)


# 77 / 78 T_TORCH, T_TORCH2 (wall torch, two flame frames)
def torch(f):
    c = S(["kBk", "kbk", "kbk", "kbk", "kkk"], 6, 10)
    S(["kaaak", " kkk "], 5, 9, c)
    flame = [
        ["  k   ", " krk  ", " krYk ", "krYYrk", "kYoYYk", " kYYk "],
        ["   k  ", "  krk ", " kYrk ", "krYYrk", "kYYoYk", " kYYk "],
    ][f]
    S(flame, 5, 3, c)
    return c


T[77] = torch(0)
T[78] = torch(1)

# 79 T_STAIRS (three steps down into the dark)
c = fill("k")
for i, (y, x0) in enumerate(((0, 0), (4, 1), (8, 2), (12, 3))):
    x1 = 15 - x0
    rect(c, x0, y, x1, y + 2, "a" if i < 3 else "b")
    rect(c, x0, y, x1, y, "o" if i < 3 else "a")
T[79] = c

# 80 T_FLOOR_WOOD
c = fill("B")
brick(c, (0, 4, 8, 12), [(0, (3, 11)), (4, (7, 15)), (8, (2, 10)), (12, (6, 14))], "b")
dots(c, [(5, 2), (13, 6), (4, 10), (9, 14), (1, 6)], "s")
T[80] = c

# 81 T_WALL_IN : plaster over a wooden dado
c = fill("s")
rect(c, 0, 0, 15, 0, "k")
rect(c, 0, 1, 15, 1, "o")
rect(c, 0, 8, 15, 8, "k")
rect(c, 0, 9, 15, 15, "B")
for x in range(0, 16, 4):
    rect(c, x, 9, x, 15, "b")
rect(c, 0, 15, 15, 15, "k")
for x in range(0, 16, 4):
    c[3][x + 1] = "B"
    c[5][x + 3] = "B"
T[81] = c
# 82 T_WALL_IN_WINDOW
c = [list(r) for r in T[81]]
S([
    "kkkkkkkkkk",
    "kWffWWffWk",
    "kWoffWoWWk",
    "kffffffffk",
    "kkkkkkkkkk",
    "kbbbbbbbbk",
], 3, 1, c)
T[82] = c
# 83 T_DOOR_MAT (opaque, on the wooden floor)
c = fill("B")
brick(c, (0, 4, 8, 12), [(0, (3, 11)), (4, (7, 15)), (8, (2, 10)), (12, (6, 14))], "b")
rect(c, 2, 4, 13, 11, "k")
rect(c, 3, 5, 12, 10, "r")
rect(c, 3, 5, 12, 5, "s")
dots(c, [(x, 7) for x in (5, 8, 11)], "s")
dots(c, [(x, 9) for x in (5, 8, 11)], "s")
T[83] = c

# 84 / 85 T_BED_TOP, T_BED_BOT (a vertical bed over two cells)
T[84] = S([
    "kkkkkkkkkkkk",
    "kbbbbbbbbbbk",
    "kBBBBBBBBBBk",
    "kBBBBBBBBBBk",
    "kBkkkkkkkkBk",
    "kBkooooookBk",
    "kBkooaaaokBk",
    "kBkooooookBk",
    "kBkkkkkkkkBk",
    "kBkooooookBk",
    "kBkooooookBk",
    "kBkooooookBk",
    "kBkrrrrrrkBk",
    "kBkrooooorBk",
    "kBkrrrrrrkBk",
    "kBkrrrrrrkBk",
], 2, 0)
T[85] = S([
    "kBkrrrrrrkBk",
    "kBkrYrrYrkBk",
    "kBkrrrrrrkBk",
    "kBkrrYrrYkBk",
    "kBkrrrrrrkBk",
    "kBkrYrrYrkBk",
    "kBkrrrrrrkBk",
    "kBkkkkkkkkBk",
    "kbBBBBBBBBbk",
    "kbBBBBBBBBbk",
    "kkkkkkkkkkkk",
    " kbk    kbk ",
    " kbk    kbk ",
    " kkk    kkk ",
], 2, 0)

# 86 T_TABLE (mug and bread on top)
T[86] = S([
    "kkkkkkkkkkkkkk",
    "kBBBBBBBBBBBBk",
    "kBBBBBBBBBBBbk",
    "kkkkkkkkkkkkkk",
    " kbk      kbk ",
    " kbk      kbk ",
    " kkk      kkk ",
], 1, 8)
S([" kkk ", "kooWk", " kkk "], 4, 5, T[86])
S(["kkkk", "kYYk"], 10, 6, T[86])

# 87 T_STOOL
T[87] = S([
    "  kkkkkkkk  ",
    " kBBBBBBBBk ",
    "kBBsssssBBbk",
    "kbBBBBBBBbbk",
    " kkkkkkkkkk ",
    "  kbk  kbk  ",
    "  kbk  kbk  ",
    "  kbk  kbk  ",
    "  kkk  kkk  ",
], 2, 6)

# 88 T_COUNTER
T[88] = S([
    "kkkkkkkkkkkkkkkk",
    "kBBBBBBBBBBBBBBk",
    "kssssssssssssssk",
    "kkkkkkkkkkkkkkkk",
    "kbbbbbbbbbbbbbbk",
    "kbBBBBkbBBBBBBbk",
    "kbBBBBkbBBBYBBbk",
    "kbBBBBkbBBBBBBbk",
    "kbbbbbkbbbbbbbbk",
    "kkkkkkkkkkkkkkkk",
], 0, 6)

# 89 T_SHELF
T[89] = S([
    "kkkkkkkkkkkkkk",
    "kbbbbbbbbbbbbk",
    "krrYWWrGGroWWk",
    "krrYWWrGGroWWk",
    "krrYWWrGGroWWk",
    "kbbbbbbbbbbbbk",
    "kWGrrYYWWrrGGk",
    "kWGrrYYWWrrGGk",
    "kWGrrYYWWrrGGk",
    "kbbbbbbbbbbbbk",
    "kkkkkkkkkkkkkk",
], 1, 3)

# 90 T_BARREL
T[90] = S([
    "  kkkkkkkk  ",
    " kBBBBBBBBk ",
    "kkkkkkkkkkkk",
    "kBsBBBBBBBBk",
    "kBsBBBBBBBbk",
    "kkkkkkkkkkkk",
    "kBsBBBBBBBbk",
    "kBBBBBBBBBbk",
    "kkkkkkkkkkkk",
    " kbbbbbbbbk ",
    "  kkkkkkkk  ",
], 2, 4)


# 91 / 92 T_HEARTH (two flame frames)
def hearth(f):
    c = S([
        "kkkkkkkkkkkkkkkk",
        "kaooaaaaaaaaaaak",
        "kkkkkkkkkkkkkkkk",
        "kaoaakkkkkkaaoak",
        "kaoakkkkkkkkaaak",
        "kaoakkkkkkkkaaak",
        "kaaakkkkkkkkaaak",
        "kaaakkkkkkkkaaak",
        "kaaakbbbbbbkaaak",
        "kkkkkkkkkkkkkkkk",
    ], 0, 5)
    if f == 0:
        S(["  r  ", " rYr ", "rYoYr", "kbbbk"], 5, 9, c)
    else:
        S(["   r ", " rrYr", "rYYoYr", "kbbbbk"], 4, 9, c)
    return c


T[91] = hearth(0)
T[92] = hearth(1)

# 93 T_RUG (opaque)
c = fill("r")
rect(c, 0, 0, 15, 0, "k")
rect(c, 0, 15, 15, 15, "k")
rect(c, 0, 1, 15, 1, "Y")
rect(c, 0, 14, 15, 14, "Y")
for x in range(0, 16, 2):
    c[2][x] = c[13][x] = "o"
S([
    "   kk   ",
    "  kWWk  ",
    " kWffWk ",
    "kWfooffk",
    " kWffWk ",
    "  kWWk  ",
    "   kk   ",
], 4, 4, c)
T[93] = c

# 94 T_PLANT (potted)
T[94] = S([
    "   l  g   ",
    "  lGl gGg ",
    " lGGGlGGGg",
    "  gGGGGGg ",
    "   gGGGg  ",
    "    kgk   ",
    "  kkkkkkk ",
    "  krrrrrk ",
    "  krBBBrk ",
    "   krrrk  ",
    "   kkkkk  ",
], 2, 4)

# 95 T_WINDOW_MOON : night window in the interior wall
c = [list(r) for r in T[81]]
rect(c, 3, 1, 12, 7, "k")
rect(c, 4, 2, 11, 6, "w")
rect(c, 7, 2, 7, 6, "k")
rect(c, 4, 4, 11, 4, "k")
rect(c, 2, 7, 13, 7, "b")
dots(c, [(5, 3), (6, 3), (5, 2)], "Y")
dots(c, [(9, 5), (10, 5), (9, 2), (5, 5)], "o")
dots(c, [(10, 3)], "f")
T[95] = c

# ============================================================== CHARACTERS
HF = [  # hero facing down (rows of cell 0, stand pose)
    "................",
    ".....kkkkkk.....",
    "....kbBBBBbk....",
    "...kbBBBBBBbk...",
    "...kbbppppbbk...",
    "...kbpkppkpbk...",
    "....kppppppk....",
    ".....kkppkk.....",
    "....kWWWWWWk....",
    "...kpWWYYWWpk...",
    "...kpkWWWWkpk...",
    "....kkWWWWkk....",
    "....kkbbbbkk....",
    "....kbk..kbk....",
    "....kbk..kbk....",
    "....kkk..kkk....",
]
HB = [HF[0], HF[1], HF[2], HF[3], "...kbBBBBBBbk...", "...kbBBBBBBbk...", "....kbbbbbbk....",
      ".....kkkkkk.....", HF[8], "...kpWWWWWWpk...", HF[10], HF[11], HF[12], HF[13], HF[14], HF[15]]
HS = [  # hero facing left (rows of cell 8, stand pose)
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
    ".....kbbbk......",
    ".....kbbbk......",
    ".....kbbbk......",
    ".....kkkkk......",
]

# 6 hero asleep in bed (head on the pillow, blanket up to the chin)
C[6] = S([
    " kkkkkkkk ",
    "kBBBBBBBBk",
    "kBBBBBBBBk",
    "kbpppppbbk",
    "kbpkppkpbk",
    " kkppppkk ",
    "kkWWWWWWkk",
    "kWWWWwWWWk",
    "kWWwWWWWwk",
    "kWWWWWWWWk",
    "kkkkkkkkkk",
], 3, 3)
dots(C[6], [(7, 7), (8, 7), (11, 7), (12, 7)], "p")
dots(C[6], [(7, 7), (8, 7), (11, 7), (12, 7)], "k")
dots(C[6], [(8, 7), (11, 7)], "p")

# 7 hero sitting (front view, knees forward)
C[7] = S(HF[:13] + ["...kbbbkkbbbk...", "...kkkkk.kkkk..."])

# 11 sword swing left (side view, blade extended to the left)
c = S(HS)
S(["kkkkkkkk", "kooooakp", "kkkkkkkk"], 0, 8, c)
dots(c, [(8, 9)], "W")
C[11] = c
# 12 sword swing down (front, blade at the right hand pointing down)
c = S(HF)
S([
    "kkkk",
    "kYYk",
    "kkkk",
    " kok",
    " kok",
    " kak",
    " kok",
    " kak",
    " kkk",
], 11, 7, c)
dots(c, [(11, 9)], "p")
C[12] = c
# 15 sword swing up (back, blade at the right hand pointing up)
c = S(HB)
S([
    " kk",
    "kok",
    "kak",
    "kok",
    "kak",
    "kok",
    "kak",
    "kkk",
    "kYk",
    "kkk",
], 12, 0, c)
dots(c, [(11, 9)], "p")
C[15] = c

# 13 hero hurt (eyes shut, arms flung out, stars)
c = S([
    "................",
    ".....kkkkkk.....",
    "....kbBBBBbk....",
    "...kbBBBBBBbk...",
    "...kbbppppbbk...",
    "...kbkkppkkbk...",
    "....kpprrpk.....",
    ".....kkppkk.....",
    "..k.kWWWWWWk.k..",
    ".kpkWWWWWWWWkpk.",
    ".kk.kWWWWWWk.kk.",
    "....kkWWWWkk....",
    "....kkbbbbkk....",
    "...kbk....kbk...",
    "..kbbk....kbbk..",
    "..kkkk....kkkk..",
])
dots(c, [(1, 2), (14, 2), (2, 1), (13, 1)], "Y")
C[13] = c
# 14 hero cheering (holds a star up with the right hand)
c = S([
    "................",
    ".....kkkkkk.....",
    "....kbBBBBbk....",
    "...kbBBBBBBbk...",
    "...kbbppppbbk...",
    "...kbpkppkpbk...",
    "....kpprrpk.....",
    ".....kkppkk.....",
    ".k..kWWWWWWk..k.",
    "kpk.kWWYYWWk.kpk",
    "kpkkWWWWWWWWkkpk",
    ".kpkkWWWWWWkkpk.",
    "..kk.kkbbkk.kk..",
    "....kbbbbbbk....",
    "....kbk..kbk....",
    "....kkk..kkk....",
])
c[6][11] = "p"
S([" k ", "kYk", " k "], 0, 0, c)
S([" k ", "kYk", " k "], 13, 0, c)
C[14] = c

# 16..39 head overlays: rows 0..7 of the cell, drawn over the hero body
HEAD_BASE = (HF[:8], HB[:8], HS[:8])  # down, up, left


def head(d, main, shade, patches):
    rows = [[{"B": main, "b": shade}.get(ch, ch) for ch in r] for r in HEAD_BASE[d]]
    c = blank()
    c[:8] = rows
    for (blk, x, y) in patches:
        S(blk, x, y, c)
    return c


HAT = {  # shared pieces
    "straw": [(["kkkkkk"], 5, 0), (["kYYYYYYk"], 4, 1), (["krrrrrrk"], 4, 2), (["kYYYYYYYYYYYYk"], 1, 3)],
    "wizard": [(["kk"], 7, 0), (["kWWk"], 6, 1), (["kWYWWk"], 5, 2), (["kWWWWWWk"], 4, 3),
               (["kkwwwwwwwwkk"], 2, 4)],
    "bun": [(["kkkk"], 6, 0), (["kaaaak"], 5, 1)],
}

# (main hair, shade, patches for down, up, left)
HEADS = [
    # 0 Mira: blonde, long hair, red bow
    ("Y", "B",
     [(["rr"], 9, 2), (["kB        Bk"], 3, 6), (["kBk    kBk"], 3, 7)],
     [(["kBBBBBBBBk"], 3, 6), (["kkkkkkkkkk"], 3, 7), (["rr"], 9, 2)],
     [(["BBk"], 10, 5), (["BBk"], 9, 6), (["BBk"], 9, 7), (["kkk"], 9, 8), (["rr"], 9, 2)]),
    # 1 Bram: white hair, full beard
    ("o", "a",
     [(["kooooook"], 4, 6), (["kooook"], 5, 7)],
     [],
     [(["kooo"], 3, 6), (["kooook"], 4, 7), (["kkkk"], 5, 8)]),
    # 2 Pip: dark messy hair, freckles
    ("b", "k",
     [(["k"], 8, 0), (["k"], 6, 0), (["B  B"], 5, 5), ],
     [(["k"], 8, 0), (["k"], 6, 0)],
     [(["k"], 8, 0), (["k"], 6, 0), (["B"], 4, 5)]),
    # 3 Odda: grey bun, round glasses
    ("a", "o",
     HAT["bun"] + [(["WkppkW"], 5, 5)],
     HAT["bun"],
     HAT["bun"] + [(["W"], 4, 5)]),
    # 4 Finn: straw hat
    ("B", "b", HAT["straw"], HAT["straw"], HAT["straw"]),
    # 5 Tess: red hair, pigtails
    ("r", "b",
     [(["kYk"], 1, 4), (["krk"], 1, 5), (["krk"], 1, 6), (["kYk"], 12, 4), (["krk"], 12, 5), (["krk"], 12, 6)],
     [(["kYk"], 1, 4), (["krk"], 1, 5), (["krk"], 1, 6), (["kYk"], 12, 4), (["krk"], 12, 5), (["krk"], 12, 6)],
     [(["kYk"], 11, 4), (["krk"], 11, 5), (["krk"], 11, 6)]),
    # 6 Luma: pointed blue hat with a gold star
    ("B", "b", HAT["wizard"], HAT["wizard"], HAT["wizard"]),
    # 7 Wren: green hood with a red feather
    ("G", "g",
     [(["rr"], 10, 0), (["k"], 11, 1)],
     [(["rr"], 10, 0), (["k"], 11, 1)],
     [(["rr"], 10, 0), (["k"], 11, 1)]),
]
for n, (main, shade, pd, pu, pl) in enumerate(HEADS):
    for d, pt in enumerate((pd, pu, pl)):
        C[16 + n * 3 + d] = head(d, main, shade, pt)

# 40 / 41 slime A / B (B squashed)
C[40] = S([
    "     kkkk     ",
    "   kkGGGGkk   ",
    "  kGGlGGGGGk  ",
    " kGGlGGGGGGGk ",
    " kGGGGkGGkGGk ",
    "kGGGGGkGGkGGGk",
    "kGGGGGGGGGGGgk",
    "kggGGGGGGGGggk",
    " kkgggggggkk  ",
], 1, 7)
C[41] = S([
    "      kk      ",
    "    kkGGkk    ",
    "   kGGlGGGk   ",
    "  kGGlGGGGGk  ",
    "  kGGGkGGkGGk ",
    " kGGGGkGGkGGGk",
    " kGGGGGGGGGGgk",
    " kGGGGGGGGGGgk",
    "kggGGGGGGGGGggk",
    "kkkkkkkkkkkkkk",
], 1, 6)
# 42 / 43 bat A (wings up) / B (wings down)
C[42] = S([
    "k           k",
    "kak   kk  kak",
    "kaak kaak kaak"[:13],
    "kaaakkaakkaaak",
    " kaaaaaaaaaak ",
    "  kkkaaaakkk  ",
    "     kaak     ",
    "     kkkk     ",
], 1, 4)
dots(C[42], [(7, 7), (10, 7)], "r")
C[43] = S([
    "     kk  kk   ",
    "    kaakkaak  ",
    "   kaaaaaaaak ",
    "  kaaaraaraak ",
    " kaaakaaaakak ",
    "kkkk kkkk kkk ",
    "k   kakkak  k ",
    "     kkkk     ",
], 1, 4)
# 44 / 45 crab A (claws up) / B (claws down)
C[44] = S([
    " kk       kk ",
    "krrk     krrk",
    "krrk     krrk",
    " kkkkkkkkkkk ",
    "  kkrrrrrrkk ",
    " krroorrookrk",
    " krrkorrkorrk",
    " kkrrrrrrrrk ",
    "  kk k kk k  ",
    "  k  k  k  k ",
], 1, 5)
C[45] = S([
    "  kk     kk  ",
    " krrk   krrk ",
    " krrkkkkkrrk ",
    "  kkrrrrrkk  ",
    " krroorrookrk",
    " krrkorrkorrk",
    " kkrrrrrrrrk ",
    "  k k kk k k ",
    "   k  kk  k  ",
], 1, 6)
# 46 / 47 ghost A / B (the hem sways)
C[46] = S([
    "    kkkkkk    ",
    "  kkooooookk  ",
    " kooooooooook ",
    " kooookookook ",
    "kooookkokkoook",
    "kooooookooooak",
    "kooooooooooook",
    "kooaooooooaook",
    "kookkookkookok",
    "kk kk kk kk kk"[:14],
], 1, 4)
C[47] = S([
    "    kkkkkk    ",
    "  kkooooookk  ",
    " kooooooooook ",
    " kooookookook ",
    "kooookkokkoook",
    "kooooookooooak",
    "kooooooooooook",
    "kooaooooooaook",
    "kkookkookkooak",
    " kk kk kk kk  ",
], 1, 4)

# 48 / 49 chicken A (stands) / B (pecks)
C[48] = S([
    "     kkk    ",
    "    kooork  ",
    "    kookkYk ",
    "    kkook   ",
    " kkkkoook   ",
    "koooooook   ",
    "kooooooook  ",
    "kooaoooook  ",
    " kkooooook  ",
    "   kkkkk    ",
    "    kY kY   ",
    "    kk kk   ",
], 2, 3)
C[49] = S([
    "     kkkk   ",
    " kkkkoookk  ",
    "koooooorok  ",
    "kooooooookYk",
    "kooaooooookk",
    " kkooooook  ",
    "   kkkkk    ",
    "    kY kY   ",
    "    kk kk   ",
], 2, 6)
# 50 / 51 frog A (sits) / B (leaps)
C[50] = S([
    "  kk    kk  ",
    " kowk  kwok ",
    " kkkkkkkkkk ",
    " kGGGGGGGGk ",
    "kGGlGGGGlGGk",
    "kGGGGGGGGGGk",
    "kGGGGGGGGGGk",
    " kGGGGGGGGk ",
    "kGGkkkkkkGGk",
    "kkk      kkk",
], 2, 5)
C[51] = S([
    "  kk    kk  ",
    " kowk  kwok ",
    " kGGkkkkGGk ",
    "kGGGGGGGGGGk",
    "kGGlGGGGlGGk",
    "kGGGGGGGGGGk",
    " kGGGGGGGGk ",
    "  kkkkkkkk  ",
    "kk        kk",
], 2, 3)
# 52 / 53 butterfly A (open) / B (closed)
C[52] = S([
    "kkk      kkk",
    "kYYkk  kkYYk",
    "kYoYkkkkYoYk",
    "kYYYYkkYYYYk",
    " kYYYkkYYYk ",
    "  kkYkkYkk  ",
    "    kkkk    ",
], 2, 5)
C[53] = S([
    "  k    k  ",
    "   k  k   ",
    "   kkkk   ",
    "  kYYYYk  ",
    " kYYoYYYk ",
    " kYoYYYYk ",
    " kYYYYYYk ",
    "  kYYYYk  ",
    "   kkkk   ",
], 3, 4)
# 54 firefly : glowing dot with a dark core so it reads on any ground
C[54] = S([
    " kk    kk ",
    "kook  kook",
    " kokkkkok ",
    "  kkYYkk  ",
    "   kYoYk  ",
    "   kYYYk  ",
    "    kkk   ",
], 3, 5)
# 55 bird (small songbird)
C[55] = S([
    "  kk    kk   ",
    " kWWk  kWWk  ",
    "kWWWWkkWWWWk ",
    " kkWWWWWWkk  ",
    "   kWWWWwkk  ",
    "   kkWWoWkYk ",
    "     kkkkkk  ",
], 1, 5)
# 56..59 cat: down, side A, side B, asleep
C[56] = S([
    "  k      k  ",
    " kak    kak ",
    " kaakkkkaak ",
    " kaaaaaaaak ",
    " kakaaaakak ",
    " kaaaooaaak ",
    "  kkaaaakk  ",
    " kaaaaaaaak ",
    " kaaaaaaaak ",
    " kakaaaakak ",
    " kkk kk kkk ",
], 2, 4)
dots(C[56], [(5, 8), (10, 8)], "k")
dots(C[56], [(7, 9), (8, 9)], "r")
CAT_SIDE = [
    " k  k         ",
    "kak kak    kk ",
    "kaakaakkkkkak ",
    "kaaaaaaaaaaak ",
    "kroaaaaaaaaaak",
    "kkaaaaaaaaaak ",
    " kaaaaaaaaaak ",
]
C[57] = S(CAT_SIDE + [" kakkaaaakkak ", " kk kk  kk kk "], 1, 6)
C[58] = S(CAT_SIDE + ["  kakaaaakak  ", "  kk kkkk kk  "], 1, 6)
C[59] = S([
    "  kk  kk     ",
    " kaakkaakkkk ",
    " kaaaaaaaaaak",
    " kaokaaaaaaak",
    " kkaaaaaaaaak",
    "  kkaaaaaakk ",
    "   kkkkkkkk  ",
], 1, 8)
dots(C[59], [(12, 11), (13, 11)], "a")


# 60..62 boss "the Hollow King": crowned blue slime (two frames and hurt)
def boss(f, hurt):
    body, shade, hi = ("o", "a", "f") if hurt else ("W", "w", "f")
    rows = [
        "   kkkkkkkkkk   ",
        "  k$$$$$$$$$$k  ",
        " k$$h$$$$$$$$$k ",
        "k$$h$$$$$$$$$$$k",
        "k$$$$$$$$$$$$$$k",
        "k$$$kkk$$kkk$$$k",
        "k$$$krk$$krk$$$k",
        "k$$$$$$$$$$$$$$k",
        "k%$$$kkkkkk$$$%k",
        "k%%$$$$$$$$$$%%k",
        " kk%%%%%%%%%%kk ",
        "  kkkkkkkkkkkk  ",
    ]
    rows = [r.replace("$", body).replace("%", shade).replace("h", hi) for r in rows]
    oy = 4 if f == 0 else 5
    c = S(rows[:12 if f == 0 else 11], 0, oy)
    S(["  k  k  k  ", " kYkkYkkYk ", " kYYoYYYYk "], 2, oy - 3, c)
    return c


C[60] = boss(0, False)
C[61] = boss(1, False)
C[62] = boss(0, True)

# ================================================================== ITEMS
# 11 / 12 I_SWORD_UP / I_SWORD_DOWN
SWORD = [
    "  kk  ",
    " kook ",
    " koak ",
    " koak ",
    " koak ",
    " koak ",
    " koak ",
    " koak ",
    "kkYYkk",
    "kYYYYk",
    " kkBk ",
    "  kBk ",
    "  kkk ",
]
I[11] = S(SWORD, 5, 1)
I[12] = S(SWORD[::-1], 5, 2)
# 13 / 14 pickaxe up / down
I[13] = S([
    "   kkkkk    ",
    "  kaaaaaak  ",
    " kaaookkaak ",
    " kak  kbk k ",
    " k    kBk   ",
    "      kbk   ",
    "      kBk   ",
    "      kbk   ",
    "      kBk   ",
    "      kkk   ",
], 2, 3)
I[14] = S([
    "      kkk   ",
    "      kBk   ",
    "      kbk   ",
    "      kBk   ",
    "      kbk   ",
    "      kBk   ",
    " k    kbk   ",
    " kak  kbk k ",
    " kaaookkaak ",
    "  kaaaaaak  ",
    "   kkkkk    ",
], 2, 3)
# 15 hammer
I[15] = S([
    "  kkkkkkkk  ",
    " kaaooaaaak ",
    " kaaaaaaaak ",
    " kkkkBkkkkk ",
    "    kBk     ",
    "    kbk     ",
    "    kBk     ",
    "    kbk     ",
    "    kBk     ",
    "    kkk     ",
], 2, 3)
# 16 stone, 17 mushroom, 18 berries, 19 flower
I[16] = S([
    "   kkkkk   ",
    "  kaooaak  ",
    " kaoaaaaak ",
    " kaaaaaaakk",
    "kaaaaaaaaaak",
    "kaaaaaaaaaak",
    " kkkkkkkkkk ",
], 2, 5)
I[17] = S([
    "   kkkkkk   ",
    "  krrrrrrk  ",
    " krooorrork ",
    "krrooorrrrrk",
    "kkkkkkkkkkkk",
    "   kooook   ",
    "   kooak    ",
    "   kooak    ",
    "   kkkkk    ",
], 2, 3)
I[18] = S([
    "      kk   ",
    "    kkgGk  ",
    "   kgkgk   ",
    "  kk  k kk ",
    " krrk krrk ",
    "kroorkrorrk",
    "krrrrkrrrrk",
    " kkkk kkkk ",
], 2, 4)
I[19] = S([
    "   kkkk   ",
    "  kooook  ",
    " kkoYYook ",
    "koooYYoook",
    " kooYYookk"[:10],
    "  kkoook  ",
    "   kkgkk  ",
    "    kgk   ",
    "  kkkgGk  ",
    " kGGkgk   ",
    "  kkkgk   ",
], 3, 2)
# 20 shell
I[20] = S([
    "   kkkkkk   ",
    " kkopppppkk ",
    "kopppookppok",
    "kppkookpppok",
    "kppokpppokpk",
    "kpppkkpppopk",
    " kppppppopk ",
    "  kkkkkkkk  ",
], 2, 4)
# 21 ore : grey chunk with gold flecks
I[21] = S([
    "   kkkkk    ",
    "  kaooaakk  ",
    " kaaYaaaaak ",
    "kaaaaaaaYaak",
    "kaYaaaaaaaak",
    "kaaaaaYaaaak",
    " kkkkkkkkkk ",
], 2, 5)
# 22 gem
I[22] = S([
    "  kkkkkk  ",
    " kWfWWWWk ",
    "kWfoWWWWWk",
    "kkkkkkkkkk",
    " kWWWWWWk ",
    "  kWWWWk  ",
    "   kWWk   ",
    "    kk    ",
], 3, 4)
# 23 crop : a radish
I[23] = S([
    "  k    k  ",
    " kGk  kGk ",
    " kGGkkGGk ",
    "  kGGGGk  ",
    "  kkrrkk  ",
    " krrrrrrk ",
    "krrrorrrrk",
    "krrorrrrrk",
    "krrrrrrrrk",
    " krrrrrrk ",
    "  kkkkkk  ",
], 3, 2)
# 24 seed : sack with a sprout
I[24] = S([
    "    k  l    ",
    "   kGk lGk  "[:12],
    "    kGgk    ",
    "   kkkkkk   ",
    "  kBBBBBBk  ",
    " kBBsBBBBBk ",
    " kBsBBBBBbk ",
    "kBBBBBBBBbbk",
    " kbbbbbbbbk ",
    "  kkkkkkkk  ",
], 2, 3)
# 25 herb sprig
I[25] = S([
    "      kk  ",
    "  kk kGGk ",
    " kGGkkGlk ",
    " kGlGkGGk ",
    "  kGGkgk  ",
    "kk kGGk   ",
    "kGkkgk    ",
    " kGGgk    ",
    "  kkgk    ",
    "    gk    ",
], 3, 3)
# 26 gel : green drop
I[26] = S([
    "    kk    ",
    "   kGGk   ",
    "  kGlGGk  ",
    " kGlGGGGk ",
    " kGGGGGGk ",
    " kGGGGGgk ",
    "  kGGGgk  ",
    "   kkkk   ",
], 3, 4)
# 27 map : parchment with a route and a red cross
I[27] = S([
    "kkkkkkkkkkkk",
    "ksssBssssssk",
    "kssWWsssBssk",
    "ksssWsskksrk",
    "kBssWWWkskrk",
    "kssssssrrksk",
    "ksssBsssrrsk",
    "kkkkkkkkkkkk",
], 2, 4)
dots(I[27], [(9, 7), (10, 6)], "r")
# 28 key
I[28] = S([
    " kkkk       ",
    "kYYYYk      ",
    "kYkkYkkkkkkk",
    "kYkkYYYYYYYk",
    "kYYYYkkkYkYk",
    " kkkk   kkkk",
], 2, 5)
# 29 crystal shard
I[29] = S([
    "    kk   ",
    "   kffk  ",
    "  kfoWk  ",
    "  kfoWWk ",
    " kfoWWWk ",
    " kfoWWwk ",
    "kfoWWwwk ",
    "kkkkkkkk ",
], 3, 4)
# 30 parcel tied with a red string
I[30] = S([
    "   kk  kk   ",
    "  krrkkrrk  ",
    "kkkkkkkkkkkk",
    "kBBBBrBBBBBk",
    "kBsBBrBBBsBk",
    "krrrrrrrrrrk",
    "kBBBBrBBBBBk",
    "kBBsBrBBsBBk",
    "kbbbbrbbbbbk",
    "kkkkkkkkkkkk",
], 2, 3)
# 31 charm : pendant on a cord
I[31] = S([
    "kk        kk",
    "kbk      kbk",
    " kbk    kbk ",
    "  kbk  kbk  ",
    "   kbkkbk   ",
    "    kYYk    ",
    "   kYooYk   ",
    "   kYrrYk   ",
    "   kYrrYk   ",
    "    kYYk    ",
    "     kk     ",
], 2, 3)
# 33 I_HEART_EMPTY : the shape of I_HEART with a grey fill
I[33] = S([
    "   kkk   kkk   ",
    "  kaaak kaaak  ",
    "  kaoaakaaaak  ",
    "  kaoaaaaaaak  ",
    "  kaaaaaaaaak  ",
    "   kaaaaaaak   ",
    "    kaaaaak    ",
    "     kaaak     ",
    "      kak      ",
    "       k       ",
], 0, 3)
# 35 I_EXCL : exclamation mark on a gold bubble
I[35] = S([
    "    kkkk    ",
    "   kYYYYk   ",
    "  kYYrrYYk  ",
    " kYYYrrYYYk ",
    " kYYYrrYYYk ",
    " kYYYrrYYYk ",
    " kYYYYYYYYk ",
    " kYYYrrYYYk ",
    "  kYYYYYYk  ",
    "   kkkkkk   ",
], 2, 3)
# 36 I_SCROLL : rolled parchment with a red ribbon
c = S([
    " kkkkkkkkkk ",
    "kssssssssssk",
    "ksBsBsssBssk",
    "kssssssssssk",
    "kBssBsssssBk",
    "kssssssssssk",
    " kkkkkkkkkk ",
], 2, 5)
rect(c, 6, 5, 6, 11, "k")
rect(c, 7, 5, 8, 11, "r")
rect(c, 9, 5, 9, 11, "k")
I[36] = c
# 37 I_NOTE : two beamed eighth notes
I[37] = S([
    "   kkkkkkkkk ",
    "   kWWWWWWWk ",
    "   kWkkkkkWk ",
    "   kWk   kWk ",
    "   kWk   kWk ",
    " kkkWk kkkWk ",
    "kWWWWk kWWWk ",
    "kWWWWk kWWWk ",
    " kkkk   kkk  ",
], 1, 3)
# 38 / 39 I_POOF, I_POOF2 (puff, then scattered wisps)
I[38] = S([
    "   kkk  kkk   ",
    "  kooookooook ",
    " kooooooooook ",
    "kooofooooofook",
    "kooooooooooook",
    " kooofooooook ",
    "  kooooooook  ",
    "   kkkkkkkk   ",
], 1, 4)
I[39] = S([
    "  k   kk    k ",
    " kok kook  kok",
    "  k  kffk   k ",
    "          kk  ",
    "kok      kook ",
    " k       kffk ",
    "   kk     kk  ",
    "  kook        ",
    "  kffk    k   ",
    "   kk    kok  ",
    "          k   ",
], 1, 3)
# 40..47 weather icons: clear, cloudy, rain, storm, fog, snow, windy, moon
c = S([
    "   kkkk   ",
    "  kYYYYk  ",
    " kYYoYYYk ",
    " kYoYYYYk ",
    " kYYYYYYk ",
    "  kYYYYk  ",
    "   kkkk   ",
], 3, 4)
dots(c, [(7, 1), (8, 1), (7, 14), (8, 14), (1, 7), (1, 8), (14, 7), (14, 8),
         (3, 3), (12, 3), (3, 12), (12, 12)], "Y")
dots(c, [(7, 0), (8, 0), (7, 15), (8, 15), (0, 7), (0, 8), (15, 7), (15, 8),
         (2, 2), (13, 2), (2, 13), (13, 13)], "k")
I[40] = c
CLOUD = [
    "    kkkk    ",
    "  kkooookk  ",
    " kooooooook ",
    "kooooaaooook",
    "kooaaaaaaaok",
    "kaaaaaaaaaak",
    " kkkkkkkkkk ",
]
I[41] = S(CLOUD, 2, 4)
I[42] = S(CLOUD, 2, 2)
dots(I[42], [(5, 10), (8, 10), (11, 10), (4, 12), (7, 12), (10, 12), (5, 14), (8, 14), (11, 14)], "W")
dots(I[42], [(5, 11), (8, 11), (11, 11), (4, 13), (7, 13), (10, 13)], "w")
DARK = [
    "    kkkk    ",
    "  kkaaaakk  ",
    " kaaaaaaaak ",
    "kaaaaaaaaaak",
    "kaaaaaaaaaak",
    " kkkkkkkkkk ",
]
I[43] = S(DARK, 2, 1)
S([
    "   kkk ",
    "  kYYk ",
    " kYYk  ",
    "kYYYYk ",
    " kkYYk ",
    "   kYk ",
    "    k  ",
], 5, 7, I[43])
c = blank()
for y, x0, x1 in ((3, 3, 12), (6, 1, 11), (9, 4, 14), (12, 2, 12)):
    rect(c, x0, y, x1, y + 1, "a")
    rect(c, x0 - 1, y, x0 - 1, y + 1, "k")
    rect(c, x1 + 1, y, x1 + 1, y + 1, "k")
    rect(c, x0, y - 1, x1, y - 1, "k")
    rect(c, x0, y + 2, x1, y + 2, "k")
    rect(c, x0 + 1, y, x0 + 3, y, "o")
I[44] = c
I[45] = S(CLOUD, 2, 1)
for (x, y) in [(4, 9), (8, 10), (12, 9), (6, 13), (10, 13)]:
    for dx, dy in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1)):
        I[45][y + dy][x + dx] = "o" if dx == dy == 0 else "f"
I[46] = S([
    " kkkkkkkk   ",
    "kffffffffkk ",
    "kkkkkkkkkfok",
    "         kfk",
    "          k ",
], 1, 2)
S([
    " kkkkkkkkkkkk",
    "kfffffffffffk",
    "kkkkkkkkkkfok",
    "          kkk",
], 0, 7, I[46])
S([
    " kkkkkkkk  ",
    "kffffffffkk",
    " kkkkkkkfok",
    "        kk ",
], 1, 11, I[46])
I[47] = S([
    "     kkkk   ",
    "   kkYYYk   ",
    "  kYYYYk    ",
    " kYYoYk     ",
    " kYoYYk     ",
    " kYYYYk     ",
    " kYYYYYk    ",
    "  kYYYYYkkk ",
    "   kkYYYYYYk",
    "     kkkkkk ",
], 2, 3)
for (x, y) in ((12, 2), (12, 4), (11, 3), (13, 3)):
    I[47][y][x] = "k"
I[47][3][12] = "Y"
dots(I[47], [(10, 9)], "Y")

# ================================================================== driver
SHEETS = [("tiles.png", T, 16), ("chars.png", C, 8), ("items.png", I, 8)]

# ids of the placeholder cells that this script paints
EXPECTED = {
    "tiles.png": list(range(30, 96)),
    "chars.png": [6, 7] + list(range(11, 63)),
    "items.png": list(range(11, 32)) + [33] + list(range(35, 48)),
}


def check_cell(name, i, c):
    assert len(c) == 16 and all(len(r) == 16 for r in c), (name, i)
    flat = [ch for r in c for ch in r]
    assert all(ch in CHAR_TO_INDEX for ch in flat), (name, i)
    if name == "chars.png":
        assert flat.count("k") >= 20, (name, i, "outline pixels", flat.count("k"))
        assert flat.count(".") >= 30, (name, i, "transparent pixels", flat.count("."))
    if name == "items.png":
        assert flat.count("k") >= 8, (name, i)


def main():
    for fname, cells, cols in SHEETS:
        path = os.path.join(SRC, fname)
        img = Image.open(path).convert("RGB")
        for i in EXPECTED[fname]:
            c = cells.get(i)
            if c is None:
                sys.exit(f"{fname}: cell {i} has no art")
            check_cell(fname, i, c)
            ox, oy = (i % cols) * 16, (i // cols) * 16
            for y in range(16):
                for x in range(16):
                    img.putpixel((ox + x, oy + y), PALETTE[CHAR_TO_INDEX[c[y][x]]][2])
        img.save(path)
        print("painted", fname, len(EXPECTED[fname]), "cells")


if __name__ == "__main__":
    main()
