# Draws the Cabby icons (run: python3 icons/make-icons.py; needs fontTools).
# The cabbage, the page and the colors are all here, to change and redraw.
import math, os
HERE = os.path.dirname(os.path.abspath(__file__))
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.boundsPen import BoundsPen

FONT = TTFont('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf')
GS = FONT.getGlyphSet(); CMAP = FONT.getBestCmap(); UPM = FONT['head'].unitsPerEm

def text_path(s, cx, baseline, size, track=0.0):
    """the text as one SVG path, centered on cx"""
    scale = size / UPM
    names = [CMAP[ord(c)] for c in s]
    widths = [GS[n].width * scale for n in names]
    total = sum(widths) + track * (len(s) - 1)
    x = cx - total / 2
    pen = SVGPathPen(GS)
    for n, w in zip(names, widths):
        tp = TransformPen(pen, (scale, 0, 0, -scale, x, baseline))
        GS[n].draw(tp)
        x += w + track
    return pen.getCommands()

def r(v): return f'{v:.2f}'.rstrip('0').rstrip('.')

def cabbage(cx, cy, R, label=None):
    """a cabbage of radius R centered at (cx, cy)"""
    out = []
    # back leaves: a scalloped ring
    dark, mid, light, head, headhi, vein, ink = '#13a538', '#2ccd3f', '#5ee34a', '#a8f03c', '#e4ff7a', '#d4ff6b', '#0b5d1e'
    for k in range(9):
        a = math.radians(k * 40 - 90)
        x, y = cx + math.cos(a) * R * 0.62, cy + math.sin(a) * R * 0.62
        out.append(f'<circle cx="{r(x)}" cy="{r(y)}" r="{r(R*0.42)}" fill="{dark}"/>')
    out.append(f'<circle cx="{r(cx)}" cy="{r(cy)}" r="{r(R*0.8)}" fill="{dark}"/>')
    # veins on the back leaves
    for k in range(9):
        a = math.radians(k * 40 - 90)
        x1, y1 = cx + math.cos(a) * R * 0.55, cy + math.sin(a) * R * 0.55
        x2, y2 = cx + math.cos(a) * R * 0.93, cy + math.sin(a) * R * 0.93
        out.append(f'<path d="M{r(x1)} {r(y1)}L{r(x2)} {r(y2)}" stroke="{light}" stroke-width="{r(R*0.06)}" stroke-linecap="round"/>')
    # middle leaves, wrapping round the head from both sides and below
    def leaf(angle, color):
        a = math.radians(angle)
        x, y = cx + math.cos(a) * R * 0.3, cy + math.sin(a) * R * 0.3
        return (f'<ellipse cx="{r(x)}" cy="{r(y)}" rx="{r(R*0.62)}" ry="{r(R*0.45)}" '
                f'transform="rotate({angle} {r(x)} {r(y)})" fill="{color}"/>')
    out.append(leaf(150, mid)); out.append(leaf(30, mid)); out.append(leaf(90, light))
    # the head
    hy = cy - R * 0.06
    out.append(f'<circle cx="{r(cx)}" cy="{r(hy)}" r="{r(R*0.5)}" fill="{head}"/>')
    out.append(f'<path d="M{r(cx-R*0.38)} {r(hy-R*0.12)}A{r(R*0.5)} {r(R*0.5)} 0 0 1 {r(cx+R*0.3)} {r(hy-R*0.4)}" '
               f'stroke="{headhi}" stroke-width="{r(R*0.09)}" fill="none" stroke-linecap="round"/>')
    # the front leaf's edge, curling over the bottom of the head
    out.append(f'<path d="M{r(cx-R*0.55)} {r(cy+R*0.28)}Q{r(cx)} {r(cy+R*0.62)} {r(cx+R*0.55)} {r(cy+R*0.28)}" '
               f'stroke="{vein}" stroke-width="{r(R*0.07)}" fill="none" stroke-linecap="round"/>')
    if label:
        # big enough to read at a glance: white, with a dark green edge so it
        # stands out from every green behind it
        size = R * 0.98
        out.append(f'<path d="{text_path(label, cx, cy + size*0.28, size, track=-R*0.04)}" fill="#fff" '
                   f'stroke="{ink}" stroke-width="{r(R*0.17)}" stroke-linejoin="round" paint-order="stroke"/>')
    return '\n'.join(out)

def page(color, fold, shade, label):
    # a Papirus-style page, a little narrower than the theme's own (x 12 to
    # 52): flat color, the top right corner folded over, and the label in
    # big white letters, as on Papirus's Calc icons
    L, R_, T, B, F = 12, 52, 4, 60, 13     # left, right, top, bottom, fold size
    body = (f'M{L+4} {T}H{R_-F}L{R_} {T+F}V{B-3}C{R_} {B-1.34} {R_-1.34} {B} {R_-3} {B}'
            f'H{L+3}C{L+1.34} {B} {L} {B-1.34} {L} {B-3}V{T+4}C{L} {T+1.79} {L+1.79} {T} {L+4} {T}Z')
    edge = (f'M{L} {B-5}V{B-3}C{L} {B-1.34} {L+1.34} {B} {L+3} {B}H{R_-3}C{R_-1.34} {B} {R_} {B-1.34} {R_} {B-3}'
            f'V{B-5}C{R_} {B-3.34} {R_-1.34} {B-2} {R_-3} {B-2}H{L+3}C{L+1.34} {B-2} {L} {B-3.34} {L} {B-5}Z')
    flap = f'M{R_-F} {T}V{T+F-3}C{R_-F} {T+F-1.34} {R_-F+1.34} {T+F} {R_-F+3} {T+F}H{R_}Z'
    size = 17.5                              # big, with a little room either side
    text = text_path(label, (L + R_) / 2, 42, size, track=-0.4)
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 64 64">
<path d="{body}" fill="{color}"/>
<path d="{edge}" fill="{shade}" opacity=".35"/>
<path d="{flap}" fill="{fold}"/>
<path d="{text}" fill="#fff"/>
</svg>
'''

def logo():
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 64 64">
<ellipse cx="32" cy="58" rx="20" ry="3" fill="#000" opacity=".2"/>
{cabbage(32, 32, 26)}
</svg>
'''

# Papirus's Calc green, with white letters; the cabbage logo is kept for
# the website (and as the app icon)
GREEN, GREEN_FOLD, GREEN_SHADE = '#2e8b3e', '#7cc488', '#0d3f17'
open('' + HERE + '/text-x-cabby.svg', 'w').write(page(GREEN, GREEN_FOLD, GREEN_SHADE, 'cby'))
open('' + HERE + '/text-x-cabby-header.svg', 'w').write(page(GREEN, GREEN_FOLD, GREEN_SHADE, 'hby'))
open('' + HERE + '/cabby.svg', 'w').write(logo())
