#!/usr/bin/env python3
"""Draw Couch's XMB icon, pkgfiles/ICON0.PNG (320x176), in PS3 Health's
style (as Cell Bench's art/make_icon.py): a transparent background, a
drawing on the left, the name on the right with a soft shadow, legible on
any XMB wallpaper.

    python3 -I art/make_icon.py <Inter variable TTF>

The drawing is a sofa seen from the front: the back, two seat cushions, the
arms and short legs, in ps3gfx's BLUE with a lighter top on each cushion.
The words are "Couch" and, under it, "Board" (the one page of 0.1.0). Font:
Inter (SIL Open Font License), the variable TTF kept in the ps3sdk archive
(fonts/InterVariable.ttf); only the rendered text ends up in the PNG.
"""
import os
import sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'pkgfiles', 'ICON0.PNG')
SS = 4                                     # supersampling factor
BLUE = (108, 184, 255)                     # ps3gfx's BLUE
WHITE = (242, 245, 250)
DARK = (34, 78, 128)                       # the sofa's shade
LIGHT = (176, 218, 255)                    # the cushions' tops


def font(path, size, weight):
    f = ImageFont.truetype(path, size)
    f.set_variation_by_axes([min(32, max(14, size)), weight])
    return f


def shadow_text(img, xy, s, f, fill, blur):
    layer = Image.new('RGBA', img.size, (0, 0, 0, 0))
    ImageDraw.Draw(layer).text((xy[0] + blur * .4, xy[1] + blur * .6), s, font=f, fill=(0, 0, 0, 230))
    img.alpha_composite(layer.filter(ImageFilter.GaussianBlur(blur)))
    ImageDraw.Draw(img).text(xy, s, font=f, fill=fill)


def draw_sofa(img, x0, y0, w):
    """The sofa in a w wide box from x0, y0: its shadow, then the back, the arms, the seat and the legs."""
    h = w * .62
    sh = Image.new('RGBA', img.size, (0, 0, 0, 0))
    ImageDraw.Draw(sh).rounded_rectangle((x0 + w * .02, y0 + h * .12, x0 + w * 1.02, y0 + h * 1.04), w * .08, fill=(0, 0, 0, 170))
    img.alpha_composite(sh.filter(ImageFilter.GaussianBlur(w * .035)))
    d = ImageDraw.Draw(img)
    r = w * .06
    arm = w * .15
    d.rounded_rectangle((x0 + arm * .7, y0, x0 + w - arm * .7, y0 + h * .62), r, fill=DARK + (255,))          # the back
    for k in range(2):                                                                                     # the back cushions
        bx0 = x0 + arm + k * (w - 2 * arm) / 2 + w * .015
        bx1 = x0 + arm + (k + 1) * (w - 2 * arm) / 2 - w * .015
        d.rounded_rectangle((bx0, y0 + h * .06, bx1, y0 + h * .58), r * .8, fill=BLUE + (255,))
        d.rounded_rectangle((bx0 + w * .03, y0 + h * .1, bx1 - w * .03, y0 + h * .2), r * .5, fill=LIGHT + (255,))
    for k in range(2):                                                                                     # the seat cushions
        sx0 = x0 + arm + k * (w - 2 * arm) / 2 + w * .01
        sx1 = x0 + arm + (k + 1) * (w - 2 * arm) / 2 - w * .01
        d.rounded_rectangle((sx0, y0 + h * .55, sx1, y0 + h * .8), r * .8, fill=BLUE + (255,))
        d.rounded_rectangle((sx0 + w * .025, y0 + h * .58, sx1 - w * .025, y0 + h * .64), r * .4, fill=LIGHT + (255,))
    for ax in (x0, x0 + w - arm):                                                                          # the arms
        d.rounded_rectangle((ax, y0 + h * .3, ax + arm, y0 + h * .9), r, fill=DARK + (255,))
        d.rounded_rectangle((ax + arm * .12, y0 + h * .33, ax + arm * .88, y0 + h * .42), r * .6, fill=BLUE + (255,))
    d.rounded_rectangle((x0 + arm * .5, y0 + h * .78, x0 + w - arm * .5, y0 + h * .9), r * .5, fill=DARK + (255,))   # the base
    for lx in (x0 + arm * .45, x0 + w - arm * .45 - w * .05):                                            # the legs
        d.rectangle((lx, y0 + h * .9, lx + w * .05, y0 + h), fill=(20, 40, 70, 255))


def icon(font_path):
    W, H = 320 * SS, 176 * SS
    img = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    sw = round(W * .44)
    draw_sofa(img, round(W * .05), round((H - sw * .62) / 2), sw)
    tx, room = W * .55, W * .42            # the text column: fit "Couch" to its width
    size = 50 * SS
    while font(font_path, size, 800).getlength('Couch') > room:
        size -= SS
    f1, f2 = font(font_path, size, 800), font(font_path, round(size * .62), 650)
    shadow_text(img, (tx, H * .16), 'Couch', f1, WHITE + (255,), 3 * SS)
    shadow_text(img, (tx + SS, H * .58), 'Board', f2, BLUE + (255,), 3 * SS)
    return img.resize((320, 176), Image.LANCZOS)


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    icon(sys.argv[1]).save(OUT, optimize=True)
    im = Image.open(OUT)
    print(os.path.relpath(OUT, os.path.join(HERE, '..')), im.size, im.mode, os.path.getsize(OUT), 'bytes')
