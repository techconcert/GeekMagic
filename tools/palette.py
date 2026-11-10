"""Master 16-colour palette shared by every tile and sprite.

Index 0 is transparent (stored as magenta #FF00FF in the PNGs).
Edit colours here, then re-run `python tools/png2tiles.py`.
`python tools/palette.py` writes assets/palette.gpl for Aseprite/LibreSprite.
"""
import os

# (char used in ASCII art, name, RGB)
PALETTE = [
    (".", "transparent", (255, 0, 255)),
    ("k", "outline",     (26, 20, 36)),
    ("g", "dark green",  (46, 92, 40)),
    ("G", "green",       (82, 150, 58)),
    ("l", "light green", (140, 200, 75)),
    ("b", "dark brown",  (100, 64, 38)),
    ("B", "brown",       (160, 108, 60)),
    ("s", "sand",        (216, 180, 112)),
    ("w", "dark blue",   (40, 72, 150)),
    ("W", "blue",        (70, 135, 215)),
    ("f", "foam",        (160, 210, 240)),
    ("p", "skin",        (240, 200, 160)),
    ("r", "red",         (196, 58, 42)),
    ("o", "white",       (240, 240, 232)),
    ("a", "grey",        (128, 128, 144)),
    ("Y", "yellow",      (240, 192, 64)),
]

CHAR_TO_INDEX = {c: i for i, (c, _, _) in enumerate(PALETTE)}
RGB_TO_INDEX = {rgb: i for i, (_, _, rgb) in enumerate(PALETTE)}


def rgb565(rgb):
    r, g, b = rgb
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def write_gpl(path):
    with open(path, "w") as f:
        f.write("GIMP Palette\nName: GeekQuest\nColumns: 16\n#\n")
        for _, name, (r, g, b) in PALETTE:
            f.write(f"{r:3d} {g:3d} {b:3d}\t{name}\n")


if __name__ == "__main__":
    root = os.path.join(os.path.dirname(__file__), "..")
    write_gpl(os.path.join(root, "assets", "palette.gpl"))
    print("wrote assets/palette.gpl")
