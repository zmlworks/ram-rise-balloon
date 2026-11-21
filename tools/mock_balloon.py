#!/usr/bin/env python3
"""Layout mockups of the RamRiseBalloon v0.3.0 popup (not a live Wine capture)."""
from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

C_BG = (0x20, 0x22, 0x27, 255)
C_BORDER = (0x44, 0x4A, 0x55, 255)
C_TEXT = (0xEC, 0xEE, 0xF1, 255)
C_DIM = (0x9A, 0xA1, 0xAC, 255)
C_RISE = (0xFF, 0x8A, 0x3D, 255)
C_DROP = (0x3D, 0xC6, 0xE6, 255)
C_COMP = (0xFF, 0xC1, 0x4D, 255)

FONT_REG = "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"
FONT_BOLD = "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc"


def muldiv(a: int, s: int, d: int = 96) -> int:
    return a * s // d


def font(path: str, px: int, index: int = 4) -> ImageFont.FreeTypeFont:
    try:
        return ImageFont.truetype(path, px, index=index)
    except OSError:
        return ImageFont.truetype(path, px)


def text_w(draw: ImageDraw.ImageDraw, f: ImageFont.FreeTypeFont, s: str) -> int:
    b = draw.textbbox((0, 0), s, font=f)
    return b[2] - b[0]


def apply_shape(im: Image.Image, radius: float, bw: float) -> None:
    w, h = im.size
    px = im.load()
    hw, hh = w / 2.0, h / 2.0
    br, bg, bb, _ = C_BORDER
    for y in range(h):
        for x in range(w):
            r, g, b, _a = px[x, y]
            qx = abs(x + 0.5 - hw) - (hw - radius)
            qy = abs(y + 0.5 - hh) - (hh - radius)
            ox = qx if qx > 0 else 0
            oy = qy if qy > 0 else 0
            d = (ox * ox + oy * oy) ** 0.5 + (qx if qx < 0 and qx > qy else (qy if qy < 0 else 0)) - radius
            a = 0.5 - d
            bf = d + bw + 0.5
            if a <= 0:
                px[x, y] = (0, 0, 0, 0)
                continue
            if a > 1:
                a = 1
            if bf > 0:
                if bf > 1:
                    bf = 1
                r = int(r + (br - r) * bf + 0.5)
                g = int(g + (bg - g) * bf + 0.5)
                b = int(b + (bb - b) * bf + 0.5)
            px[x, y] = (
                int(r * a + 0.5),
                int(g * a + 0.5),
                int(b * a + 0.5),
                int(a * 255 + 0.5),
            )


def draw_usage(draw, pad, y, iw, big_h, f_big, f_small, rise, drop, val, total, tag):
    arrow = "▲" if rise else "▼" if drop else ""
    c = C_RISE if rise else C_DROP if drop else C_TEXT
    head = f"{arrow}{val:.1f}"
    ty = y + max(0, (big_h - f_big.size) // 2 - 2)
    draw.text((pad, ty), head, font=f_big, fill=c)
    x = pad + text_w(draw, f_big, head)
    rest = f" / {total:.1f} GB"
    draw.text((x, ty), rest, font=f_big, fill=C_TEXT)
    pct = f"{val * 100.0 / total:.0f}%"
    pw = text_w(draw, f_big, pct)
    draw.text((pad + iw - pw, ty), pct, font=f_big, fill=C_TEXT)
    if tag:
        lab = tag + "  "
        tw = text_w(draw, f_small, lab)
        sty = y + max(0, (big_h - f_small.size) // 2)
        draw.text((pad + iw - pw - tw, sty), lab, font=f_small, fill=C_DIM)


def render(kind: str, dpi: int) -> Image.Image:
    pad = muldiv(14, dpi)
    lh = muldiv(20, dpi)
    big_h = muldiv(32, dpi)
    d_h = muldiv(22, dpi)
    w = muldiv(330, dpi)
    show_top = kind == "rise"
    lines = 3 if show_top else 2
    h = pad - muldiv(2, dpi) + big_h + big_h + d_h + muldiv(10, dpi) + lines * lh + pad - muldiv(4, dpi)

    im = Image.new("RGBA", (w, h), C_BG)
    draw = ImageDraw.Draw(im)
    f_small = font(FONT_REG, muldiv(13, dpi))
    f_head = font(FONT_BOLD, muldiv(15, dpi))
    f_big = font(FONT_BOLD, muldiv(22, dpi))

    if kind == "rise":
        used, comb, total = 13.0, 14.7, 32.0
        steps = [("+65", C_RISE), ("+220", C_RISE), ("+105", C_RISE), ("+225", C_RISE), ("+110", C_RISE)]
        used_rise, used_drop = True, False
        comb_rise, comb_drop = True, False
        comp_delta, comp_rise = "▲70 MB", True
        comp_body = "圧縮: 889 MB（元 約 2.52 GB）"
        commit = "コミット: 19.1 / 40.0 GB"
        top_delta, top_name = "+525 MB", "増加最大: chrome.exe"
    else:
        used, comb, total = 12.1, 13.8, 32.0
        steps = [("-200", C_DROP), ("-195", C_DROP), ("-210", C_DROP), ("-188", C_DROP), ("-205", C_DROP)]
        used_rise, used_drop = False, True
        comb_rise, comb_drop = False, True
        comp_delta, comp_rise = "±0 MB", False
        comp_body = "圧縮: 889 MB（元 約 2.52 GB）"
        commit = "コミット: 16.9 / 40.0 GB"
        top_delta, top_name = None, None

    iw = w - 2 * pad
    y = pad - muldiv(2, dpi)
    draw_usage(draw, pad, y, iw, big_h, f_big, f_small, used_rise, used_drop, used, total, None)
    y += big_h
    draw_usage(draw, pad, y, iw, big_h, f_big, f_small, comb_rise, comb_drop, comb, total, "含圧縮")
    y += big_h

    unit_w = text_w(draw, f_small, "MB")
    gap = muldiv(6, dpi)
    cw = (iw - unit_w - gap) // 5
    ty = y + max(0, (d_h - f_head.size) // 2 - 1)
    for col, (lab, colc) in enumerate(steps):
        draw.text((pad + col * cw, ty), lab, font=f_head, fill=colc)
    draw.text((pad + 5 * cw + gap, y + max(0, (d_h - f_small.size) // 2)), "MB", font=f_small, fill=C_DIM)
    y += d_h + muldiv(10, dpi)

    slot = max(text_w(draw, f_small, "▲888 MB"), text_w(draw, f_small, "-8.88 GB")) + muldiv(10, dpi)
    sy = y + max(0, (lh - f_small.size) // 2)
    dcol = C_RISE if comp_delta.startswith("▲") or comp_delta.startswith("+") else C_DIM
    draw.text((pad, sy), comp_delta, font=f_small, fill=dcol)
    draw.text((pad + slot, sy), comp_body, font=f_small, fill=C_COMP if comp_rise else C_TEXT)
    y += lh
    sy = y + max(0, (lh - f_small.size) // 2)
    draw.text((pad + slot, sy), commit, font=f_small, fill=C_DIM)
    y += lh
    if top_delta:
        sy = y + max(0, (lh - f_small.size) // 2)
        draw.text((pad, sy), top_delta, font=f_small, fill=C_DIM)
        draw.text((pad + slot, sy), top_name, font=f_small, fill=C_DIM)

    apply_shape(im, muldiv(8, dpi), dpi / 96.0)
    return im


def on_canvas(balloon: Image.Image, dpi: int) -> Image.Image:
    m = muldiv(45, dpi)
    top = muldiv(50, dpi)
    bot = muldiv(40, dpi)
    canvas = Image.new("RGB", (balloon.width + 2 * m, balloon.height + top + bot), (0, 0, 0))
    canvas.paste(balloon, (m, top), balloon)
    return canvas


def main() -> None:
    here = Path(__file__).resolve().parents[1]
    docs = here / "docs"
    out = Path("/workspace/out/ram-rise-balloon-0.3.0")
    out.mkdir(parents=True, exist_ok=True)
    for kind in ("rise", "drop"):
        for dpi, suffix in ((96, ""), (144, "-144dpi")):
            im = render(kind, dpi)
            framed = on_canvas(im, dpi)
            name = f"balloon-{kind}{suffix}.png"
            framed.save(out / name, optimize=True)
            if kind == "rise":
                docs_name = "screenshot.png" if dpi == 96 else "screenshot-144dpi.png"
                framed.save(docs / docs_name, optimize=True)
            if kind == "drop" and dpi == 96:
                framed.save(docs / "screenshot-drop.png", optimize=True)
            print("wrote", out / name, framed.size)
    print("docs updated")


if __name__ == "__main__":
    main()
