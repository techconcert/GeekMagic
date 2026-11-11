"""One-time migration of the art sheets to the layout of docs/DESIGN.md 10.5.

    python tools/expand_sheets.py

  tiles.png  128x64  (8x4 cells)   -> 256x96  (16x6 cells), ids unchanged
  items.png  128x32  (8x2 cells)   -> 128x96  (8x6 cells),  ids unchanged
  hero.png   48x48   (3x3 cells)   -> chars.png 128x128 (8x8 cells):
             hero down 0..2, up 3..5, left 8..10 (right = flipped)

Cells that have no art yet get the placeholder (2x2 checker of palette
entries 1 and 14). Two cells are painted for real because release R0 shows
them: I_STAR (items 32) and I_FACE (items 34). T_VOID (tiles 29) is solid
outline colour.

The script is idempotent and never overwrites painted art: a sheet that
already has its final size is left alone. Chunk 7 paints the placeholders
directly in the PNGs.
"""
import os
import sys
from PIL import Image

from palette import PALETTE, CHAR_TO_INDEX

ROOT = os.path.join(os.path.dirname(__file__), "..")
SRC = os.path.join(ROOT, "assets", "src")

TRANSPARENT = PALETTE[0][2]

STAR = [
    "................",
    "................",
    ".......kk.......",
    "......kYYk......",
    "......kYYk......",
    "..kkkkkYYkkkkk..",
    "..kYYYYYYYYYYk..",
    "...kYYYoYYYYk...",
    "....kYYYYYYk....",
    ".....kYYYYk.....",
    ".....kYYYYk.....",
    "....kYYkkYYk....",
    "....kYk..kYk....",
    "...kYk....kYk...",
    "...kk......kk...",
    "................",
]

FACE = [
    "................",
    "................",
    "....kkkkkkkk....",
    "...kbbbbbbbbk...",
    "..kbbbbbbbbbbk..",
    "..kbbppppppbbk..",
    "..kbppppppppbk..",
    "..kppkppppkppk..",
    "..kppkppppkppk..",
    "..kppppppppppk..",
    "..kpppprrppppk..",
    "...kppppppppk...",
    "....kkkkkkkk....",
    "................",
    "................",
    "................",
]


def cell_pixels(rows):
    assert len(rows) == 16, len(rows)
    out = []
    for r in rows:
        assert len(r) == 16, (r, len(r))
        out.append([PALETTE[CHAR_TO_INDEX[c]][2] for c in r])
    return out


def placeholder():
    c1, c2 = PALETTE[1][2], PALETTE[14][2]
    return [[c1 if ((x // 2) + (y // 2)) % 2 == 0 else c2 for x in range(16)] for y in range(16)]


def solid(index):
    return [[PALETTE[index][2]] * 16 for _ in range(16)]


def get_cell(img, cx, cy):
    return [[img.getpixel((cx * 16 + x, cy * 16 + y)) for x in range(16)] for y in range(16)]


def put_cell(img, cx, cy, px):
    for y in range(16):
        for x in range(16):
            img.putpixel((cx * 16 + x, cy * 16 + y), px[y][x])


def relayout(old_path, old_cols, keep, new_cols, new_rows, paint):
    old = Image.open(old_path).convert("RGB")
    new = Image.new("RGB", (new_cols * 16, new_rows * 16), TRANSPARENT)
    for i in range(new_cols * new_rows):
        if i < keep:
            px = get_cell(old, i % old_cols, i // old_cols)
        elif i in paint:
            px = paint[i]
        else:
            px = placeholder()
        put_cell(new, i % new_cols, i // new_cols, px)
    return new


def main():
    tiles_p = os.path.join(SRC, "tiles.png")
    items_p = os.path.join(SRC, "items.png")
    hero_p = os.path.join(SRC, "hero.png")
    chars_p = os.path.join(SRC, "chars.png")

    if Image.open(tiles_p).size == (128, 64):
        img = relayout(tiles_p, 8, 29, 16, 6, {29: solid(1)})
        img.save(tiles_p)
        print("expanded tiles.png to 256x96")
    else:
        print("tiles.png already expanded")

    if Image.open(items_p).size == (128, 32):
        img = relayout(items_p, 8, 11, 8, 6, {32: cell_pixels(STAR), 34: cell_pixels(FACE)})
        img.save(items_p)
        print("expanded items.png to 128x96 (I_STAR, I_FACE painted)")
    else:
        print("items.png already expanded")

    if not os.path.exists(chars_p):
        if not os.path.exists(hero_p):
            sys.exit("neither chars.png nor hero.png exists")
        hero = Image.open(hero_p).convert("RGB")
        chars = Image.new("RGB", (128, 128), TRANSPARENT)
        for i in range(64):
            put_cell(chars, i % 8, i // 8, placeholder())
        for row, base in ((0, 0), (1, 3), (2, 8)):
            for col in range(3):
                put_cell(chars, (base + col) % 8, (base + col) // 8, get_cell(hero, col, row))
        chars.save(chars_p)
        os.remove(hero_p)
        print("created chars.png (hero cells at 0..2, 3..5, 8..10), removed hero.png")
    else:
        print("chars.png already exists")


if __name__ == "__main__":
    main()
