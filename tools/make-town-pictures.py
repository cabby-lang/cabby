#!/usr/bin/env python3
"""Draws the pictures for tutorials 69 to 73 and Cabby Quest (77 to 88) (a walking character in a small
town, its people and buildings, and a spaceship): pixel art, each pixel 3
by 3, into tutorials/pictures/. Tiles and people are 16 by 16; a tall
tree is a tile and a half high, and a house three by three.

    python3 tools/make-town-pictures.py
"""
import os
from PIL import Image

SCALE = 3
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "tutorials", "pictures")

PALETTE = {
    ".": None,
    "k": (43, 43, 56), "s": (242, 199, 154), "e": (43, 43, 56), "h": (107, 63, 34),
    "b": (62, 155, 90), "B": (47, 119, 68), "j": (232, 129, 58), "J": (184, 95, 34),
    "p": (59, 95, 168), "o": (74, 52, 38), "w": (255, 255, 255),
    # the town
    "g": (124, 198, 106), "G": (95, 168, 81), "y": (231, 211, 154), "Y": (212, 189, 126),
    "t": (46, 125, 60), "T": (31, 92, 44), "l": (86, 170, 82), "r": (122, 78, 44),
    "R": (220, 60, 60), "O": (250, 210, 60), "c": (79, 143, 224), "C": (150, 195, 245),
    "n": (160, 112, 60), "N": (120, 82, 44),
    # people and houses
    "a": (205, 205, 210), "A": (160, 160, 170), "W": (225, 222, 210), "m": (190, 60, 70), "M": (150, 40, 52),
    "q": (200, 160, 110), "Q": (170, 130, 85), "u": (150, 60, 50), "U": (115, 42, 36), "v": (90, 60, 40),
    "x": (40, 80, 120), "i": (120, 70, 170), "I": (88, 48, 130),
    # the hills (Cabby Quest, part 10): high grass, rock, stone and planks
    "1": (146, 212, 122), "2": (118, 186, 100), "3": (164, 132, 98), "4": (118, 90, 66),
    "5": (182, 180, 172), "6": (132, 130, 124), "7": (186, 136, 82), "8": (134, 92, 54),
}

def save(name, rows, width=16, height=16):
    assert len(rows) == height, (name, len(rows))
    im = Image.new("RGBA", (width * SCALE, height * SCALE), (0, 0, 0, 0))
    for y, row in enumerate(rows):
        assert len(row) == width, (name, y, row, len(row))
        for x, ch in enumerate(row):
            c = PALETTE[ch]
            if c:
                for dy in range(SCALE):
                    for dx in range(SCALE):
                        im.putpixel((x * SCALE + dx, y * SCALE + dy), c + (255,))
    im.save(os.path.join(OUT, name + ".png"))

def mirror(rows):
    return [r[::-1] for r in rows]

# ---- the hero: a green hat, an orange jacket

HEAD_DOWN = [
    "................",
    ".....kkkkkk.....",
    "....kbbbbbbk....",
    "...kbbbbbbbbk...",
    "...kBBBBBBBBk...",
    "...khhhhhhhhk...",
    "...khsssssshk...",
    "...ksesssesk....",
    "...kssssssssk...",
    "....kssssssk....",
]
HEAD_DOWN[7] = "...kseSsssesk...".replace("S", "s")
HEAD_UP = HEAD_DOWN[:5] + [
    "...khhhhhhhhk...",
    "...khhhhhhhhk...",
    "...khhhhhhhhk...",
    "...kkhhhhhhkk...",
    "....kssssssk....",
]
BODY_STAND = [
    "...kjjjjjjjjk...",
    "..ksjjjJJjjjsk..",
    "..ksjjjjjjjjsk..",
    "...kppppppppk...",
    "...kppk..kppk...",
    "...kook..kook...",
]
BODY_STEP = [
    "...kjjjjjjjjk...",
    "..ksjjjJJjjjjk..",
    "...kjjjjjjjjsk..",
    "...kppppppppk...",
    "...kppk..kook...",
    "...kook.........",
]

# a person is a tile high, standing on it: their feet at its bottom
def save_person(name, rows):
    save(name, rows)

save_person("hero-down-1", HEAD_DOWN + BODY_STAND)
save_person("hero-down-2", HEAD_DOWN + BODY_STEP)
save_person("hero-down-3", mirror(HEAD_DOWN + BODY_STEP))
save_person("hero-up-1", HEAD_UP + BODY_STAND)
save_person("hero-up-2", HEAD_UP + BODY_STEP)
save_person("hero-up-3", mirror(HEAD_UP + BODY_STEP))

HEAD_LEFT = [
    "................",
    ".....kkkkkk.....",
    "....kbbbbbbk....",
    "...kbbbbbbbbk...",
    "..kBBBBBBBBBk...",
    "...khhhhhhhhk...",
    "...ksshhhhhhk...",
    "...ksehhhhhhk...",
    "...ksssshhhhk...",
    "....kssssssk....",
]
LEFT_STAND = [
    "....kjjjjjjk....",
    "....kjjjjjjk....",
    "....kjjsjjjk....",
    "....kppppppk....",
    "....kppkppk.....",
    "...kookkook.....",
]
LEFT_STEP_1 = [
    "....kjjjjjjk....",
    "...ksjjjjjjk....",
    "....kjjjjjjsk...",
    "....kppppppk....",
    "...kppk..kppk...",
    "..kook....kook..",
]
LEFT_STEP_2 = [
    "....kjjjjjjk....",
    "....kjjjjjjsk...",
    "...ksjjjjjjk....",
    "....kppppppk....",
    ".....kpppppk....",
    "....kookook.....",
]
for n, body in (("1", LEFT_STAND), ("2", LEFT_STEP_1), ("3", LEFT_STEP_2)):
    save_person("hero-left-" + n, HEAD_LEFT + body)
    save_person("hero-right-" + n, mirror(HEAD_LEFT + body))

# ---- the town's tiles

GRASS = [
    "gggggggggggggggg",
    "ggggGgggggggggGg",
    "gggGgGgggggggGgg",
    "gggggggggggggggg",
    "gggggggggGgggggg",
    "ggggggggGgGggggg",
    "gggggggggggggggg",
    "gGgggggggggggggg",
    "GgGggggggggggggg",
    "ggggggggggggGggg",
    "gggggggggggGgGgg",
    "gggggggggggggggg",
    "gggggGgggggggggg",
    "ggggGgGggggggggg",
    "gggggggggggggggg",
    "gggggggggggggggg",
]
save("tile-grass", GRASS)
PATH = [
    "yyyyyyyyyyyyyyyy",
    "yyyyyyYyyyyyyyyy",
    "yyyyyyyyyyyyYyyy",
    "yyYyyyyyyyyyyyyy",
    "yyyyyyyyyyyyyyyy",
    "yyyyyyyyyYyyyyyy",
    "yyyyyyyyyyyyyyyY",
    "yyyyYyyyyyyyyyyy",
    "yyyyyyyyyyyyyyyy",
    "yyyyyyyyyyyYyyyy",
    "yYyyyyyyyyyyyyyy",
    "yyyyyyyyyyyyyyyy",
    "yyyyyyYyyyyyyyyy",
    "yyyyyyyyyyyyyYyy",
    "yyyyyyyyyyyyyyyy",
    "yyyYyyyyyyyyyyyy",
]
save("tile-path", PATH)
TREE = [
    "gggggkkkkkkggggg",
    "ggggkltttttkgggg",
    "gggkltttttttkggg",
    "ggkllttttttttkgg",
    "ggklttttttttTkgg",
    "gkltttttttttTTkg",
    "gkltttttttttTTkg",
    "gktttttttttTTTkg",
    "gkttttttttTTTTkg",
    "ggkTttttTTTTTkgg",
    "gggkkTTTTTTkkggg",
    "gggggkkrrkkggggg",
    "ggggggkrrkgggggg",
    "ggggggkrrkgggggg",
    "gggggGkrrkGggggg",
    "gggggggkkggggggg",
]
save("tile-tree", TREE)

def flowers(spots):
    rows = [list(r) for r in GRASS]
    for (x, y, c) in spots:
        rows[y][x] = c
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            rows[y + dy][x + dx] = "R" if c == "O" else "O"
    return ["".join(r) for r in rows]

save("tile-flowers-1", flowers([(3, 3, "O"), (11, 5, "R"), (6, 10, "O"), (12, 12, "R")]))
save("tile-flowers-2", flowers([(4, 3, "O"), (12, 5, "R"), (7, 10, "O"), (13, 12, "R")]))

def water(shift):
    rows = []
    for y in range(16):
        row = ["c"] * 16
        if y % 5 == 2:
            for x in range(16):
                if (x + shift + y) % 8 in (0, 1, 2):
                    row[x] = "C"
        rows.append("".join(row))
    return rows

save("tile-water-1", water(0))
save("tile-water-2", water(4))
SIGN = [
    "gggggggggggggggg",
    "gggggggggggggggg",
    "ggkkkkkkkkkkkkgg",
    "gknnnnnnnnnnnnkg",
    "gknNNNNNNNNNNnkg",
    "gknnnnnnnnnnnnkg",
    "gknNNNNNNNNNnnkg",
    "gknnnnnnnnnnnnkg",
    "ggkkkkkkkkkkkkgg",
    "ggggggkNNkgggggg",
    "ggggggkNNkgggggg",
    "ggggggkNNkgggggg",
    "gggggGkNNkGggggg",
    "ggggggkkkkgggggg",
    "gggggggggggggggg",
    "gggggggggggggggg",
]
save("tile-sign", SIGN)
# ---- tutorial 73: a tree two tiles tall, a house, and people
#
# They stand on the ground (grass is drawn under them), so round them is
# see-through. The tree is a tile and a half tall: its trunk is on its own
# tile, and its leaves reach halfway up the tile above, so a hero standing
# there is behind them from the knees down.

TALL_TREE = [
    ".....kkkkkk.....",
    "...kklltttkk....",
    "..kllttttttTk...",
    ".klltttttttTTk..",
    ".kltttttttttTTk.",
    "klltttttttttTTk.",
    "klttttttttttTTTk",
    "kltttttttttTTTTk",
    "ktttttttttTTTTTk",
    "kttttttttTTTTTTk",
    ".kttttttTTTTTTk.",
    ".kTttttTTTTTTTk.",
    "..kTTTTTTTTTTk..",
    "...kkTTTTTTkk...",
    ".....kkkkkk.....",
    "......krrk......",
    "......krrk......",
    "......krrk......",
    "......krrk......",
    ".....krrrrk.....",
    ".....krrrrk.....",
    "....krrkkrrk....",
    "....kkk..kkk....",
    "................",
]
save("tall-tree", TALL_TREE, 16, 24)

# The house: three tiles wide and three high. Its walls stand on the
# bottom two rows (and the door in the middle of the bottom one); its roof
# rises over the row above them, so a hero behind the house is hidden.
def roof():
    rows = ["." * 48] * 3
    for y in range(17):
        half = 6 + y * 18 // 16                 # wider as it goes down
        left, right = 24 - half, 24 + half
        row = ["."] * 48
        for x in range(left, right):
            row[x] = "u"
            if (x * 7 + y * 3) % 11 == 0 and 0 < y < 15:
                row[x] = "U"                     # a few darker tiles
        row[left] = row[right - 1] = "k"
        if y == 0:
            row[left:right] = "k" * (right - left)
        if y == 15:
            row[left + 1:right - 1] = "U" * (right - left - 2)
        if y == 16:
            row[left:right] = "k" * (right - left)
        rows.append("".join(row))
    return rows

WALLS = [
    "..kqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqk..",
    "..kQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQk..",
    "..kqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqk..",
    "..kqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqk..",
    "..kqqqqkkkkkkkqqqqqqqqqqqqqqqqqqqkkkkkkkqqqqqk..",
    "..kqqqqkCCCCCkqqqqqqqqqqqqqqqqqqqkCCCCCkqqqqqk..",
    "..kqqqqkCwCCCkqqqqqqqqqqqqqqqqqqqkCwCCCkqqqqqk..",
    "..kqqqqkkkkkkkqqqqqqqqqqqqqqqqqqqkkkkkkkqqqqqk..",
    "..kqqqqkCCCCCkqqqqqqqqqqqqqqqqqqqkCCCCCkqqqqqk..",
    "..kqqqqkCCCCCkqqqqqqqqqqqqqqqqqqqkCCCCCkqqqqqk..",
    "..kqqqqkkkkkkkqqqqqqqqqqqqqqqqqqqkkkkkkkqqqqqk..",
    "..kQQQQQQQQQQQQQQQQQQkkkkkkkkkQQQQQQQQQQQQQQQk..",
    "..kqqqqqqqqqqqqqqqqqkvvvvvvvvkqqqqqqqqqqqqqqqk..",
    "..kqqqqqqqqqqqqqqqqqkvvvvvvvvkqqqqqqqqqqqqqqqk..",
    "..kqqqqqqqqqqqqqqqqqkvvvvvvvvkqqqqqqqqqqqqqqqk..",
    "..kQQQQQQQQQQQQQQQQQkvvvvvvvvkQQQQQQQQQQQQQQQk..",
    "..kqqqqqqqqqqqqqqqqqkvvvvvvOvkqqqqqqqqqqqqqqqk..",
    "..kqqqqqqqqqqqqqqqqqkvvvvvvvvkqqqqqqqqqqqqqqqk..",
    "..kqqqqqqqqqqqqqqqqqkvvvvvvvvkqqqqqqqqqqqqqqqk..",
    "..kQQQQQQQQQQQQQQQQQkvvvvvvvvkQQQQQQQQQQQQQQQk..",
    "..kqqqqqqqqqqqqqqqqqkvvvvvvvvkqqqqqqqqqqqqqqqk..",
    "..kqqqqqqqqqqqqqqqqqkvvvvvvvvkqqqqqqqqqqqqqqqk..",
    "..kqqqqqqqqqqqqqqqqqkvvvvvvvvkqqqqqqqqqqqqqqqk..",
    "..kkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkk..",
]
WALLS = [r for r in WALLS if r.strip()]
PLAIN = "..k" + "q" * 42 + "k.."
HOUSE = roof() + [PLAIN, "..k" + "Q" * 42 + "k..", PLAIN, PLAIN] + WALLS
save("house", HOUSE, 48, 48)

def person(name, colors):
    """A townsperson, standing, facing each way: the hero's shape, in other
    colors (hat: hair, jacket and trousers: clothes)."""
    def paint(rows):
        return ["".join(colors.get(c, c) for c in r) for r in rows]
    save_person(name + "-down", paint(HEAD_DOWN + BODY_STAND))
    save_person(name + "-up", paint(HEAD_UP + BODY_STAND))
    save_person(name + "-left", paint(HEAD_LEFT + LEFT_STAND))
    save_person(name + "-right", mirror(paint(HEAD_LEFT + LEFT_STAND)))
    # and walking, as the hero does (Cabby Quest's people walk about)
    for n, body in (("1", BODY_STAND), ("2", BODY_STEP)):
        save_person(name + "-down-" + n, paint(HEAD_DOWN + body))
        save_person(name + "-up-" + n, paint(HEAD_UP + body))
    save_person(name + "-down-3", mirror(paint(HEAD_DOWN + BODY_STEP)))
    save_person(name + "-up-3", mirror(paint(HEAD_UP + BODY_STEP)))
    for n, body in (("1", LEFT_STAND), ("2", LEFT_STEP_1), ("3", LEFT_STEP_2)):
        save_person(name + "-left-" + n, paint(HEAD_LEFT + body))
        save_person(name + "-right-" + n, mirror(paint(HEAD_LEFT + body)))

# an old man in a white toga, gray haired; a baker in red, with a white cap
person("elder", {"b": "a", "B": "A", "h": "a", "j": "W", "J": "a", "p": "W"})
person("baker", {"b": "w", "B": "a", "j": "m", "J": "M", "p": "x"})
# Rufus, the rival (Cabby Quest, part 9): a blue cap, a purple jacket, dark hair
person("rival", {"b": "c", "B": "x", "h": "k", "j": "i", "J": "I", "p": "k"})

# ---- Cabby Quest (tutorials 77 to 86): inside the bakery, and what's in it

WALL = [
    "qqqqqqqqqqqqqqqq",
    "qqqqqqqqqqqqqqqq",
    "qqqqqqqQqqqqqqqq",
    "qqqqqqqqqqqqqqqq",
    "qqqqqqqqqqqqqQqq",
    "NNNNNNNNNNNNNNNN",
    "qqqqqqqqqqqqqqqq",
    "qqQqqqqqqqqqqqqq",
    "qqqqqqqqqqqqqqqq",
    "qqqqqqqqqqQqqqqq",
    "qqqqqqqqqqqqqqqq",
    "qqqqqqqqqqqqqqqq",
    "NNNNNNNNNNNNNNNN",
    "nnnnnnnnnnnnnnnn",
    "nnnnnnnnnnnnnnnn",
    "kkkkkkkkkkkkkkkk",
]
save("tile-wall", WALL)
FLOOR = [
    "nnnnnnnnNnnnnnnn",
    "nnnnnnnnNnnnnnnn",
    "nnnnnnnnNnnnnnnn",
    "NNNNNNNNNNNNNNNN",
    "nnnnNnnnnnnnnnnn",
    "nnnnNnnnnnnnnnnn",
    "nnnnNnnnnnnnnnnn",
    "NNNNNNNNNNNNNNNN",
    "nnnnnnnnnnnnNnnn",
    "nnnnnnnnnnnnNnnn",
    "nnnnnnnnnnnnNnnn",
    "NNNNNNNNNNNNNNNN",
    "nnnnnnNnnnnnnnnn",
    "nnnnnnNnnnnnnnnn",
    "nnnnnnNnnnnnnnnn",
    "NNNNNNNNNNNNNNNN",
]
save("tile-floor", FLOOR)
OVEN = [
    "kkkkkkkkkkkkkkkk",
    "kuuUuuuuUuuuuUuk",
    "kUuuuUuuuuUuuuuk",
    "kuuUuuukkkuuUuuk",
    "kuuuuukkkkkuuuuk",
    "kuUuukkOOOkkuUuk",
    "kuuukkORROROkkuk",
    "kuuukORRORRROkuk",
    "kuUukROOROROOkuk",
    "kuuukkkkkkkkkkuk",
    "kUuuuuUuuuuUuuuk",
    "kuuuUuuuuUuuuuUk",
    "kuuuuuuUuuuuuuuk",
    "kUuuUuuuuuUuuuuk",
    "kkkkkkkkkkkkkkkk",
    "NNNNNNNNNNNNNNNN",
]
save("tile-oven", OVEN)
TABLE = [
    "nnnnnnnnNnnnnnnn",
    "nnnnnnnnNnnnnnnn",
    "nkkkkkkkkkkkkkkn",
    "kyyyyyyyyyyyyyyk",
    "kyYyyyyyyyyyyYyk",
    "kyyyyyyyyyyyyyyk",
    "kYYYYYYYYYYYYYYk",
    "kkkkkkkkkkkkkkkk",
    "nkNkkkkkkkkkkNkn",
    "nkNknnnnnnnnkNkn",
    "nkNknnnnnnnnkNkn",
    "nkNknnnnnnnnkNkn",
    "nkkknnnnnnnnkkkn",
    "nnnnnnnnnnnnnnnn",
    "nnnnnnnnNnnnnnnn",
    "NNNNNNNNNNNNNNNN",
]
save("tile-table", TABLE)
MAT = [
    "nnnnnnnnNnnnnnnn",
    "nnnnnnnnNnnnnnnn",
    "nnnnnnnnNnnnnnnn",
    "NNNNNNNNNNNNNNNN",
    "nkkkkkkkkkkkkkkn",
    "kmmmmmmmmmmmmmmk",
    "kmMmMmMmMmMmMmmk",
    "kmmmmmmmmmmmmmmk",
    "kmMmMmMmMmMmMmmk",
    "kmmmmmmmmmmmmmmk",
    "kmMmMmMmMmMmMmmk",
    "kmmmmmmmmmmmmmmk",
    "nkkkkkkkkkkkkkkn",
    "nnnnnnnnnnnnnnnn",
    "nnnnnnNnnnnnnnnn",
    "NNNNNNNNNNNNNNNN",
]
save("tile-mat", MAT)
# what can be picked up (on the floor, so round it is see-through)
SPOON = [
    "................",
    "................",
    "..........kkk...",
    ".........knnnk..",
    "........knnnnnk.",
    "........knnNnnk.",
    "........knnnnk..",
    ".......kNnnkk...",
    "......kNnk......",
    ".....kNnk.......",
    "....kNnk........",
    "...kNnk.........",
    "..kNnk..........",
    "..kkk...........",
    "................",
    "................",
]
save("item-spoon", SPOON)
BREAD = [
    "................",
    "................",
    "................",
    "....kkkkkkkk....",
    "..kkOyOyyOyOkk..",
    ".kOyyyyyyyyyyOk.",
    "kOyYyyyYyyyYyyOk",
    "kOyyyyyyyyyyyyOk",
    "kOOyyyyyyyyyyOOk",
    "kNOOOOOOOOOOOONk",
    ".kNNNNNNNNNNNNk.",
    "..kkkkkkkkkkkk..",
    "................",
    "................",
    "................",
    "................",
]
save("item-bread", BREAD)

# ---- a spaceship, pointing up

SHIP = [
    ".......kk.......",
    "......kwwk......",
    "......kwwk......",
    ".....kwccwk.....",
    ".....kwccwk.....",
    "....kwwwwwwk....",
    "....kwwwwwwk....",
    "...kwwwRRwwwk...",
    "..kwwwwRRwwwwk..",
    ".kRwwwwwwwwwwRk.",
    ".kRRwwwwwwwwRRk.",
    ".kRRkwwwwwwkRRk.",
    ".kkk.kwwwwk.kkk.",
    "......kOOk......",
    "......kOOk......",
    ".......kk.......",
]
save("ship", SHIP)
print("pictures written to", os.path.normpath(OUT))

# ---- Cabby Quest, part 10 (tutorial 86): the hills

def recolor(rows, change):
    return ["".join(change.get(c, c) for c in r) for r in rows]

# high ground: the grass up on a hill, a little lighter
HIGH = recolor(GRASS, {"g": "1", "G": "2"})
save("tile-high", HIGH)

# a cliff's face: the high grass's edge at the top, then rock
CLIFF = HIGH[:2] + ["2" * 16] + [
    "4444444444444444",
    "3333433333334333",
    "3333433333334333",
    "4444444444444444",
    "3433333334333333",
    "3433333334333333",
    "4444444444444444",
    "3333343333333433",
    "3333343333333433",
    "4444444444444444",
    "3343333333343333",
    "4444444444444444",
    "ggggGggggggggGgg",
]
save("tile-cliff", CLIFF)

# stairs up it: stone steps, each with its shadow
STAIRS = []
for y in range(16):
    band = "5" if y % 4 < 3 else "6"
    STAIRS.append("4" + band * 14 + "4")
save("tile-stairs", STAIRS)

# a bridge: planks across, a rail along each side
BRIDGE = ["k" * 16, "8" * 16]
for y in range(12):
    BRIDGE.append("".join("8" if x % 4 == 3 else "7" for x in range(16)))
BRIDGE += ["8" * 16, "k" * 16]
save("tile-bridge", BRIDGE)

# ledges: grass with a lip across its lower half, facing the way to hop
# down: where the hero's feet are, as he walks up to it
LEDGE_DOWN = GRASS[:7] + ["2222222222222222", "4444444444444444", "3343333433334333",
                          "3333333333333333", "3433334333343333", "4444444444444444",
                          "GGGGGGGGGGGGGGGG", "GgGGgGGGgGGgGGGg"] + GRASS[15:]
save("tile-ledge-down", LEDGE_DOWN)
save("tile-ledge-up", LEDGE_DOWN[::-1])
LEDGE_RIGHT = ["".join(LEDGE_DOWN[x][y] for x in range(16)) for y in range(16)]
save("tile-ledge-right", LEDGE_RIGHT)
save("tile-ledge-left", mirror(LEDGE_RIGHT))

# ---- Cabby Quest, part 11: the battle

# tall grass: longer blades, darker, the ground showing between them
TALL_GRASS = [
    "gTggtggTgggtgTgg",
    "gTtgtgTTggttgTtg",
    "tTttTgTtgtTtgTtt",
    "tTtTTtTttTTttTTt",
    "TTtTTTTtTTtTTTTt",
    "TtTTtTTTTtTTtTTT",
    "TTTTTTTTTTTTTTTT",
    "GGGGGGGGGGGGGGGG",
    "gTggtggTgggtgTgg",
    "gTtgtgTTggttgTtg",
    "tTttTgTtgtTtgTtt",
    "tTtTTtTttTTttTTt",
    "TTtTTTTtTTtTTTTt",
    "TtTTtTTTTtTTtTTT",
    "TTTTTTTTTTTTTTTT",
    "GGGGGGGGGGGGGGGG",
]
save("tile-tall-grass", TALL_GRASS)

# the creatures: drawn from shapes on a 32 by 32 grid, then outlined, as
# FireRed's are, and saved 4 to a pixel (128 by 128)
PALETTE.update({
    "F": (232, 120, 48), "E": (186, 84, 28), "Z": (252, 236, 214),   # the fox
    "V": (150, 128, 110), "X": (112, 94, 80), "K": (246, 170, 180),  # the mouse
    "L": (58, 62, 92), "H": (36, 38, 60), "P": (246, 196, 70),       # the crow
})

def canvas(n=32):
    return [["."] * n for _ in range(n)]

def ellipse(g, cx, cy, rx, ry, ch):
    for y in range(len(g)):
        for x in range(len(g)):
            if ((x + 0.5 - cx) / rx) ** 2 + ((y + 0.5 - cy) / ry) ** 2 <= 1:
                g[y][x] = ch

def triangle(g, a, b, c, ch):
    def side(p, q, r):
        return (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0])
    for y in range(len(g)):
        for x in range(len(g)):
            p = (x + 0.5, y + 0.5)
            s1, s2, s3 = side(a, b, p), side(b, c, p), side(c, a, p)
            if (s1 >= 0 and s2 >= 0 and s3 >= 0) or (s1 <= 0 and s2 <= 0 and s3 <= 0):
                g[y][x] = ch

def dot(g, x, y, ch):
    g[y][x] = ch

def outlined(g):
    """each empty pixel next to a coloured one becomes the dark outline"""
    n = len(g)
    out = [row[:] for row in g]
    for y in range(n):
        for x in range(n):
            if g[y][x] == ".":
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    X, Y = x + dx, y + dy
                    if 0 <= X < n and 0 <= Y < n and g[Y][X] not in ".k":
                        out[y][x] = "k"
                        break
    return ["".join(r) for r in out]

def save_big(name, g):
    global SCALE
    was, SCALE = SCALE, 4
    save(name, outlined(g), 32, 32)
    SCALE = was

# Ember, the hero's fox, seen from behind: ears up, the tail to one side
fox = canvas()
triangle(fox, (19, 20), (31, 6), (27, 26), "F")         # the tail, a brush
triangle(fox, (27, 7), (31, 6), (29.5, 10.5), "Z")      # its white tip
ellipse(fox, 14, 23, 9, 8, "F")                          # the body
ellipse(fox, 14, 26, 6, 4, "E")                          # its shadow, behind
ellipse(fox, 14, 12, 7.5, 6.5, "F")                      # the head
triangle(fox, (7, 10), (9, 1), (13, 7), "F")             # the ears
triangle(fox, (15, 7), (19, 1), (21, 10), "F")
triangle(fox, (9, 8), (9.5, 4), (12, 7), "E")            # their insides
triangle(fox, (16, 7), (18.5, 4), (19, 8), "E")
ellipse(fox, 9, 30, 2.5, 1.5, "E")                       # the back feet
ellipse(fox, 19, 30, 2.5, 1.5, "E")
save_big("critter-fox-back", fox)

# the field mouse, facing the hero: round ears, a long tail
mouse = canvas()
triangle(mouse, (22, 26), (31, 18), (30, 21), "X")       # the tail
ellipse(mouse, 16, 22, 8.5, 7.5, "V")                    # the body
ellipse(mouse, 16, 24, 5, 4.5, "Z")                      # its front
ellipse(mouse, 16, 13, 7, 6, "V")                        # the head
ellipse(mouse, 8, 6, 4.5, 4.5, "V")                      # the ears
ellipse(mouse, 24, 6, 4.5, 4.5, "V")
ellipse(mouse, 8, 6, 2.5, 2.5, "K")
ellipse(mouse, 24, 6, 2.5, 2.5, "K")
for x, y in ((13, 12), (19, 12)):                        # the eyes, with a shine
    dot(mouse, x, y, "k"); dot(mouse, x, y + 1, "k"); dot(mouse, x + 1, y, "k"); dot(mouse, x + 1, y + 1, "k")
    dot(mouse, x, y, "w")
ellipse(mouse, 16.5, 16, 1.6, 1.2, "K")                  # the nose
ellipse(mouse, 11, 29.5, 2.5, 1.5, "X")                  # the feet
ellipse(mouse, 21, 29.5, 2.5, 1.5, "X")
save_big("critter-mouse", mouse)

# the hill crow: wings half open, a yellow beak
crow = canvas()
triangle(crow, (6, 14), (1, 27), (13, 22), "H")          # the wings
triangle(crow, (26, 14), (31, 27), (19, 22), "H")
ellipse(crow, 16, 21, 8, 8.5, "L")                       # the body
triangle(crow, (12, 27), (16, 31), (20, 27), "H")        # the tail
ellipse(crow, 16, 10, 6.5, 6, "L")                       # the head
triangle(crow, (14, 12), (18, 12), (16, 17), "P")        # the beak
for x in (12, 19):                                       # the eyes
    dot(crow, x, 9, "w"); dot(crow, x + 1, 9, "w"); dot(crow, x, 10, "w"); dot(crow, x + 1, 10, "k")
ellipse(crow, 16, 22, 4, 5, "H")                         # its breast, darker
dot(crow, 13, 30, "P"); dot(crow, 19, 30, "P")           # the feet
save_big("critter-crow", crow)

# ---- Cabby Quest, part 12: Rufus's pup, Bruno, facing the hero
PALETTE.update({"D": (176, 120, 72), "d": (128, 82, 46)})
pup = canvas()
triangle(pup, (24, 24), (31, 15), (29, 13), "d")         # the tail, up
ellipse(pup, 16, 23, 9, 7.5, "D")                        # the body
ellipse(pup, 16, 25, 5, 4.5, "Z")                        # its chest
ellipse(pup, 16, 12, 7.5, 6.5, "D")                      # the head
ellipse(pup, 7.5, 13, 3, 6, "d")                         # floppy ears
ellipse(pup, 24.5, 13, 3, 6, "d")
ellipse(pup, 16, 15.5, 3.5, 2.5, "Z")                    # the muzzle
ellipse(pup, 16, 14.2, 1.6, 1.1, "k")                    # the nose
for x in (12, 19):                                       # the eyes
    dot(pup, x, 10, "k"); dot(pup, x + 1, 10, "k"); dot(pup, x, 11, "k"); dot(pup, x + 1, 11, "k")
    dot(pup, x, 10, "w")
ellipse(pup, 10.5, 30, 2.5, 1.5, "d")                    # the feet
ellipse(pup, 21.5, 30, 2.5, 1.5, "d")
save_big("critter-pup", pup)
