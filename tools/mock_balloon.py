#!/usr/bin/env python3
"""Layout mockups of the RamRiseBalloon popup (not a live Wine capture).
Colour bands stay in sync with judge.h.
"""
from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

C_BG = (0x20, 0x22, 0x27, 255)
C_BORDER = (0x44, 0x4A, 0x55, 255)
C_TEXT = (0xEC, 0xEE, 0xF1, 255)
C_NAME = (0xFF, 0xFF, 0xFF, 255)
C_DIM = (0x9A, 0xA1, 0xAC, 255)
C_RISE = (0xFF, 0x8A, 0x3D, 255)
C_DROP = (0x3D, 0xC6, 0xE6, 255)
C_COMP = (0xFF, 0xC1, 0x4D, 255)

# judge.h (0xRRGGBB) -> RGBA
def _rgba(rgb: int):
    return ((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, 255)

COL_WHITE = _rgba(0xECEEF1)
COL_GREEN = _rgba(0x3CE63C)
COL_YELLOW = _rgba(0xF0E628)
COL_ORANGE = _rgba(0xFF9A20)
COL_RED = _rgba(0xFF3A3A)
COL_MAGENTA = _rgba(0xFF4AE6)
COL_PURPLE = _rgba(0xC800FF)
COL_CYAN = _rgba(0xB0F2FF)
COL_BLUE_LIGHT = _rgba(0x6AB4FF)
COL_BLUE = _rgba(0x4A7CFF)
COL_BLUE_DEEP = _rgba(0x6A66FF)

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


def percent_color(pct: float):
    p = int(pct + 0.5)
    if p >= 90:
        return COL_PURPLE
    if p >= 85:
        return COL_MAGENTA
    if p >= 80:
        return COL_RED
    if p >= 70:
        return COL_ORANGE
    if p >= 60:
        return COL_YELLOW
    if p >= 50:
        return COL_GREEN
    return COL_WHITE


def draw_usage(draw, pad, y, iw, big_h, f_big, f_small, rise, drop, val, total, tag):
    arrow = "▲" if rise else "▼" if drop else ""
    c = C_RISE if rise else C_DROP if drop else C_TEXT
    head = f"{arrow}{val:.1f}"
    ty = y + max(0, (big_h - f_big.size) // 2 - 2)
    draw.text((pad, ty), head, font=f_big, fill=c)
    x = pad + text_w(draw, f_big, head)
    rest = f" / {total:.1f} GB"
    draw.text((x, ty), rest, font=f_big, fill=C_TEXT)
    pctv = val * 100.0 / total if total else 0
    pct = f"{pctv:.0f}%"
    pw = text_w(draw, f_big, pct)
    draw.text((pad + iw - pw, ty), pct, font=f_big, fill=percent_color(pctv))
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
    show_top = kind != "drop"
    lines = 3 if show_top else 2
    h = pad - muldiv(2, dpi) + big_h + big_h + d_h + muldiv(10, dpi) + lines * lh + pad - muldiv(4, dpi)

    im = Image.new("RGBA", (w, h), C_BG)
    draw = ImageDraw.Draw(im)
    f_small = font(FONT_REG, muldiv(13, dpi))
    f_head = font(FONT_BOLD, muldiv(15, dpi))
    f_big = font(FONT_BOLD, muldiv(22, dpi))

    if kind == "rise":
        used, comb, utotal, ctotal = 13.0, 14.7, 32.0, 33.7
        steps = [("+180", COL_PURPLE), ("+70", COL_RED), ("+30", COL_ORANGE), ("+12", COL_YELLOW), ("+4", COL_GREEN)]
        used_rise, used_drop = True, False
        comb_rise, comb_drop = True, False
        comp_delta, comp_rise = "▲70 MB", True
        comp_body = "圧縮: 889 MB（元 約 2.52 GB）"
        commit = "コミット: 19.1 / 40.0 GB"
        top_delta, top_name = "+525 MB", "chrome.exe"
    elif kind == "drop":
        used, comb, utotal, ctotal = 12.1, 13.8, 32.0, 33.7
        steps = [("-200", COL_BLUE_DEEP), ("-80", COL_BLUE), ("-40", COL_BLUE), ("-18", COL_BLUE_LIGHT), ("-5", COL_CYAN)]
        used_rise, used_drop = False, True
        comb_rise, comb_drop = False, True
        comp_delta, comp_rise = "±0 MB", False
        comp_body = "圧縮: 889 MB（元 約 2.52 GB）"
        commit = "コミット: 16.9 / 40.0 GB"
        top_delta, top_name = None, None
    else:
        used, comb, utotal, ctotal = 29.1, 30.5, 32.0, 33.4
        steps = [("+120", COL_MAGENTA), ("+55", COL_RED), ("0", COL_WHITE), ("+8", COL_GREEN), ("+22", COL_YELLOW)]
        used_rise, used_drop = True, False
        comb_rise, comb_drop = True, False
        comp_delta, comp_rise = "▲40 MB", True
        comp_body = "圧縮: 1.10 GB（元 約 3.20 GB）"
        commit = "コミット: 34.2 / 40.0 GB"
        top_delta, top_name = "+410 MB", "chrome.exe"

    iw = w - 2 * pad
    y = pad - muldiv(2, dpi)
    draw_usage(draw, pad, y, iw, big_h, f_big, f_small, used_rise, used_drop, used, utotal, None)
    y += big_h
    draw_usage(draw, pad, y, iw, big_h, f_big, f_small, comb_rise, comb_drop, comb, ctotal, "含圧縮")
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
        lab = "増加最大: "
        draw.text((pad + slot, sy), lab, font=f_small, fill=C_DIM)
        ny = y + max(0, (lh - f_head.size) // 2)
        draw.text((pad + slot + text_w(draw, f_small, lab), ny), top_name, font=f_head, fill=C_NAME)

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
    out = Path("/workspace/out/ram-rise-balloon-0.3.3")
    out.mkdir(parents=True, exist_ok=True)
    docs_map = {
        ("rise", 96): "screenshot.png",
        ("rise", 144): "screenshot-144dpi.png",
        ("drop", 96): "screenshot-drop.png",
        ("high", 96): "screenshot-high.png",
    }
    for kind in ("rise", "drop", "high"):
        for dpi, suffix in ((96, ""), (144, "-144dpi")):
            im = render(kind, dpi)
            framed = on_canvas(im, dpi)
            name = f"balloon-{kind}{suffix}.png"
            framed.save(out / name, optimize=True)
            docs_name = docs_map.get((kind, dpi))
            if docs_name:
                framed.save(docs / docs_name, optimize=True)
            print("wrote", out / name, framed.size)
    print("docs updated")


if __name__ == "__main__":
    main()
