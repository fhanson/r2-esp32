#!/usr/bin/env python3
"""Draws R2-AU's emotion icons as SVG: the droid's dome in its own colours
(white body, aqua-teal panels, gold dome), cartoon style with bold outlines.

The PSI under the eye takes the emotion's colour, and marks above the dome
show the emotion. Run it from anywhere; it writes the SVGs next to itself.
    python3 web/emoji/make_emoji.py
"""
from pathlib import Path

OUT = Path(__file__).resolve().parent

INK = "#1b2530"       # outlines
WHITE = "#f7f9fa"     # body
TEAL = "#4cb8cf"      # panel paint (sampled from the photo)
TEAL_DK = "#2f93aa"
GOLD = "#d4ad62"      # dome band gold, a touch brighter for small sizes
GOLD_DK = "#b08a45"
LENS = "#16202a"
STEEL = "#c3ccd3"

EMOTIONS = {
    #          PSI colour, PSI light, dome tilt (deg), dome drop (px)
    "neutral": ("#4c8fe0", "#cfe3ff", 0, 0),
    "happy":   ("#2fb25c", "#c9f5d6", -7, 0),
    "sad":     ("#2f6fd0", "#cfe0fb", 4, 3),
    "mad":     ("#e0392d", "#ffd2cc", 6, 1),
    "scared":  ("#f2c200", "#fff3b0", 0, 0),
}

SW = 4.5  # outline width at 128 px


def droid(psi, psi_light, tilt, drop):
    # A tall half-ellipse dome. Its bottom runs down past the body's top edge,
    # so a tilted dome never shows a gap.
    dome = "M20 84 A44 52 0 0 1 108 84 V91 H20 Z"
    return f'''
  <g transform="translate(6.4 12.8) scale(0.9)">
  <defs><clipPath id="dome"><path d="{dome}"/></clipPath></defs>
  <!-- body, as wide as the dome -->
  <path d="M20 136 V86 H108 V136" fill="{WHITE}" stroke="{INK}" stroke-width="{SW}" stroke-linejoin="round"/>
  <rect x="34" y="96" width="60" height="8" rx="3" fill="{TEAL}" stroke="{INK}" stroke-width="3"/>
  <rect x="34" y="110" width="27" height="8" rx="3" fill="{TEAL}" stroke="{INK}" stroke-width="3"/>
  <rect x="67" y="110" width="27" height="8" rx="3" fill="{TEAL}" stroke="{INK}" stroke-width="3"/>
  <!-- shoulders: tall caps beside the body, rounded at the top, as on the
       real droid; a silver hub shows between each cap and the body -->
  <rect x="13" y="94" width="10" height="34" rx="2.5" fill="{STEEL}" stroke="{INK}" stroke-width="3"/>
  <rect x="105" y="94" width="10" height="34" rx="2.5" fill="{STEEL}" stroke="{INK}" stroke-width="3"/>
  <path d="M-3 136 V98 Q-3 88 6 88 H7 Q16 88 16 98 V136 Z" fill="{WHITE}" stroke="{INK}" stroke-width="{SW}" stroke-linejoin="round"/>
  <path d="M112 136 V98 Q112 88 121 88 H122 Q131 88 131 98 V136 Z" fill="{WHITE}" stroke="{INK}" stroke-width="{SW}" stroke-linejoin="round"/>
  <path d="M11 95 V136 M117 95 V136" stroke="{STEEL}" stroke-width="3"/>
  <!-- dome -->
  <g transform="translate(0 {drop}) rotate({tilt} 64 88)">
    <path d="{dome}" fill="{GOLD}"/>
    <g clip-path="url(#dome)">
      <rect x="16" y="66" width="16" height="9" fill="{TEAL}" stroke="{INK}" stroke-width="2.5"/>
      <rect x="36" y="66" width="18" height="9" fill="{TEAL}" stroke="{INK}" stroke-width="2.5"/>
      <rect x="74" y="66" width="18" height="9" fill="{TEAL}" stroke="{INK}" stroke-width="2.5"/>
      <rect x="96" y="66" width="16" height="9" fill="{TEAL}" stroke="{INK}" stroke-width="2.5"/>
      <rect x="16" y="78" width="96" height="16" fill="{GOLD_DK}" stroke="{INK}" stroke-width="2.5"/>
      <path d="M36 50 Q42 38 54 34" fill="none" stroke="#fff3d6" stroke-width="3.5" stroke-linecap="round" opacity=".8"/>
    </g>
    <path d="{dome}" fill="none" stroke="{INK}" stroke-width="{SW}" stroke-linejoin="round"/>
    <!-- eye: lens in a teal surround -->
    <rect x="40" y="42" width="25" height="22" rx="5" fill="{TEAL}" stroke="{INK}" stroke-width="3.5"/>
    <circle cx="52.5" cy="53" r="8" fill="{LENS}" stroke="{INK}" stroke-width="3"/>
    <circle cx="49.5" cy="50" r="2.5" fill="#ffffff"/>
    <!-- PSI: the emotion light -->
    <circle cx="78" cy="56" r="10" fill="{psi_light}" opacity=".6"/>
    <circle cx="78" cy="56" r="6.5" fill="{psi}" stroke="{INK}" stroke-width="3"/>
    <circle cx="76" cy="54" r="2" fill="{psi_light}"/>
    <!-- holoprojector -->
    <rect x="66" y="35" width="10" height="9" rx="3" fill="{STEEL}" stroke="{INK}" stroke-width="3"/>
    <circle cx="71" cy="39.5" r="1.8" fill="{INK}"/>
  </g>
  </g>'''


def marks(name, psi):
    o = f'stroke="{INK}" stroke-width="3" stroke-linejoin="round" stroke-linecap="round"'
    if name == "happy":
        star = lambda x, y, r: (f'<path d="M{x} {y-r} Q{x+r*0.18} {y-r*0.18} {x+r} {y} Q{x+r*0.18} {y+r*0.18} '
                                f'{x} {y+r} Q{x-r*0.18} {y+r*0.18} {x-r} {y} Q{x-r*0.18} {y-r*0.18} {x} {y-r} Z" '
                                f'fill="{psi}" {o}/>')
        heart = (f'<path d="M64 26 C56 20 52 12 58 8 C61 6 64 9 64 11 C64 9 67 6 70 8 C76 12 72 20 64 26 Z" '
                 f'fill="{psi}" {o}/>')
        return star(36, 16, 9) + star(94, 14, 7) + heart
    if name == "sad":
        drop = lambda x, y, s: (f'<path d="M{x} {y-9*s} C{x+6*s} {y-2*s} {x+6*s} {y+2*s} {x+5*s} {y+4*s} '
                                f'C{x+3*s} {y+8*s} {x-3*s} {y+8*s} {x-5*s} {y+4*s} C{x-6*s} {y+2*s} {x-6*s} {y-2*s} {x} {y-9*s} Z" '
                                f'fill="{psi}" {o}/>')
        gloom = "".join(f'<path d="M{x} 4 q-3 4 0 8 q3 4 0 8" fill="none" stroke="{psi}" stroke-width="3.5" stroke-linecap="round"/>'
                        for x in (50, 64, 78))
        return gloom + drop(34, 18, 1) + drop(96, 22, 0.8)
    if name == "mad":
        # anger mark: four bent strokes around a centre
        cx, cy = 92, 16
        arms = []
        for dx, dy in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            x1, y1 = cx + dx * 3, cy + dy * 11
            x2, y2 = cx + dx * 11, cy + dy * 3
            arms.append(f'<path d="M{x1} {y1} Q{cx + dx * 3} {cy + dy * 3} {x2} {y2}" fill="none" '
                        f'stroke="{INK}" stroke-width="7.5" stroke-linecap="round"/>'
                        f'<path d="M{x1} {y1} Q{cx + dx * 3} {cy + dy * 3} {x2} {y2}" fill="none" '
                        f'stroke="{psi}" stroke-width="3.5" stroke-linecap="round"/>')
        spikes = "".join(
            f'<path d="M{x1} {y1} L{x2} {y2}" fill="none" stroke="{INK}" stroke-width="7.5" stroke-linecap="round"/>'
            f'<path d="M{x1} {y1} L{x2} {y2}" fill="none" stroke="{psi}" stroke-width="3.5" stroke-linecap="round"/>'
            for x1, y1, x2, y2 in ((30, 26, 22, 16), (40, 20, 37, 7), (52, 19, 54, 6)))
        return "".join(arms) + spikes
    if name == "scared":
        bang = (f'<path d="M60 3 H68 L66 20 H62 Z" fill="{psi}" {o}/>'
                f'<circle cx="64" cy="26" r="3.6" fill="{psi}" {o}/>')
        zig = lambda x, s: (f'<path d="M{x} 6 l{6*s} 5 l{-6*s} 4 l{6*s} 5" fill="none" stroke="{INK}" stroke-width="7" stroke-linecap="round" stroke-linejoin="round"/>'
                            f'<path d="M{x} 6 l{6*s} 5 l{-6*s} 4 l{6*s} 5" fill="none" stroke="{psi}" stroke-width="3.2" stroke-linecap="round" stroke-linejoin="round"/>')
        sweat = (f'<path d="M104 28 C108 34 109 37 108 39 C107 42 101 42 100 39 C99 37 100 34 104 28 Z" '
                 f'fill="#bfe6f2" {o}/>')
        shake = (f'<path d="M14 44 v14 M8 48 v8 M114 44 v14 M120 48 v8" fill="none" stroke="{INK}" '
                 f'stroke-width="3.5" stroke-linecap="round"/>')
        return zig(36, 1) + zig(92, -1) + bang + sweat + shake
    return ""


def svg(name):
    psi, light, tilt, drop = EMOTIONS[name]
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 128" width="128" height="128">'
            f'<title>R2-AU {name}</title>{droid(psi, light, tilt, drop)}{marks(name, psi)}</svg>\n')


if __name__ == "__main__":
    for name in EMOTIONS:
        (OUT / f"{name}.svg").write_text(svg(name))
        print("wrote", OUT / f"{name}.svg")


# ---------------------------------------------------------------------------
# Music tile icons, same cartoon style. Original designs that suggest each
# song without copying any Star Wars logo or character.
#   march   - a heavy steel cog with a red core (Imperial March)
#   duel    - two crossed energy blades, red and blue (Duel of the Fates)
#   cantina - a gold saxophone (Cantina)
#   theme   - twin suns setting over the dunes (Star Wars theme)

def outlined(d, colour, width, ink=6):
    """A thick stroked path with a dark outline around it."""
    return (f'<path d="{d}" fill="none" stroke="{INK}" stroke-width="{width + ink}" stroke-linecap="round" stroke-linejoin="round"/>'
            f'<path d="{d}" fill="none" stroke="{colour}" stroke-width="{width}" stroke-linecap="round" stroke-linejoin="round"/>')


def icon_march():
    import math
    cx, cy, r_out, r_in, teeth = 64, 64, 54, 42, 10
    pts = []
    for i in range(teeth * 2):
        a0 = math.pi * 2 * i / (teeth * 2) - math.pi / 2
        r = r_out if i % 2 == 0 else r_in
        for da in (-0.13, 0.13):
            a = a0 + da
            pts.append(f"{cx + r * math.cos(a):.1f} {cy + r * math.sin(a):.1f}")
    gear = "M" + " L".join(pts) + " Z"
    return (f'<path d="{gear}" fill="#6f7b87" stroke="{INK}" stroke-width="{SW + 1}" stroke-linejoin="round"/>'
            f'<circle cx="64" cy="64" r="30" fill="#4b5560" stroke="{INK}" stroke-width="4"/>'
            f'<circle cx="64" cy="64" r="17" fill="#d23a2e" stroke="{INK}" stroke-width="4"/>'
            f'<circle cx="64" cy="64" r="6" fill="{INK}"/>'
            f'<path d="M40 40 A34 34 0 0 1 64 30" fill="none" stroke="#b7c1ca" stroke-width="4" stroke-linecap="round"/>')


def icon_duel():
    def blade(x1, y1, x2, y2, colour, glow):
        hx, hy = x1 + (x1 - x2) * 0.28, y1 + (y1 - y2) * 0.28  # hilt end
        return (f'<path d="M{x1} {y1} L{x2} {y2}" stroke="{glow}" stroke-width="22" stroke-linecap="round" opacity=".55"/>'
                + outlined(f"M{x1} {y1} L{x2} {y2}", colour, 9, 5)
                + f'<path d="M{x1} {y1} L{x2} {y2}" stroke="#ffffff" stroke-width="3" stroke-linecap="round" opacity=".85"/>'
                + outlined(f"M{hx:.1f} {hy:.1f} L{x1} {y1}", STEEL, 11, 5)
                + f'<path d="M{(hx + x1) / 2:.1f} {(hy + y1) / 2:.1f} l0.01 0" stroke="{INK}" stroke-width="5" stroke-linecap="round"/>')
    return blade(34, 96, 104, 18, "#ff4a3d", "#ffb3ab") + blade(94, 96, 24, 18, "#4aa3ff", "#b8dcff")


def icon_cantina():
    body = "M44 14 C44 40 46 70 50 88 C54 106 76 108 84 94 L96 70"
    return (f'<path d="M44 14 L30 8" stroke="{INK}" stroke-width="9" stroke-linecap="round"/>'
            f'<path d="M30 8 l-6 -2" stroke="{INK}" stroke-width="6" stroke-linecap="round"/>'
            + outlined(body, GOLD, 16, 7)
            + f'<path d="M48 22 C48 44 50 70 53 86" fill="none" stroke="#fff3d6" stroke-width="4" stroke-linecap="round" opacity=".8"/>'
            f'<ellipse cx="100" cy="64" rx="16" ry="9" transform="rotate(-30 100 64)" fill="{GOLD_DK}" stroke="{INK}" stroke-width="5"/>'
            f'<ellipse cx="100" cy="64" rx="9" ry="4.5" transform="rotate(-30 100 64)" fill="{INK}"/>'
            + "".join(f'<circle cx="{x}" cy="{y}" r="4.2" fill="{STEEL}" stroke="{INK}" stroke-width="2.5"/>'
                      for x, y in ((39, 40), (40, 54), (41, 68), (44, 82)))
            + f'<path d="M108 30 q6 -8 0 -16 M116 40 q10 -12 0 -26" fill="none" stroke="{TEAL}" stroke-width="5" stroke-linecap="round"/>')


def icon_theme():
    return ('<defs><clipPath id="badge"><circle cx="64" cy="64" r="54"/></clipPath></defs>'
            '<circle cx="64" cy="64" r="54" fill="#ffd9a0"/>'
            '<g clip-path="url(#badge)">'
            '<circle cx="50" cy="66" r="15" fill="#f28c28"/><circle cx="80" cy="56" r="10" fill="#f9c440"/>'
            f'<path d="M4 84 Q34 70 64 82 T124 78 V130 H4 Z" fill="{GOLD}" stroke="{INK}" stroke-width="4"/>'
            f'<path d="M4 100 Q40 88 76 100 T124 96 V130 H4 Z" fill="{GOLD_DK}" stroke="{INK}" stroke-width="4"/>'
            '</g>'
            + "".join(f'<path d="M{x} {y - s} L{x + s * .3} {y - s * .3} L{x + s} {y} L{x + s * .3} {y + s * .3} L{x} {y + s} '
                      f'L{x - s * .3} {y + s * .3} L{x - s} {y} L{x - s * .3} {y - s * .3} Z" fill="#ffffff" stroke="{INK}" stroke-width="2"/>'
                      for x, y, s in ((36, 30, 6), (90, 26, 5), (100, 44, 3.5)))
            + f'<circle cx="64" cy="64" r="54" fill="none" stroke="{INK}" stroke-width="{SW + 1}"/>')


MUSIC = {"march": icon_march, "duel": icon_duel, "cantina": icon_cantina, "theme": icon_theme}


def music_svg(name):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 128" width="128" height="128">'
            f'<title>R2-AU {name}</title>{MUSIC[name]()}</svg>\n')


if __name__ == "__main__":
    for name in MUSIC:
        (OUT / f"music-{name}.svg").write_text(music_svg(name))
        print("wrote", OUT / f"music-{name}.svg")


# ---------------------------------------------------------------------------
# Sound tile icons, same style.

RED, PINK, GREEN, BLUE, HOLO = "#e0392d", "#ff6f9c", "#2fb25c", "#2f6fd0", "#5cc8f0"


def bubble(x, y, w, h, fill, tail="left"):
    tx = x + w * 0.25 if tail == "left" else x + w * 0.75
    d = (f"M{x + 10} {y} H{x + w - 10} Q{x + w} {y} {x + w} {y + 10} V{y + h - 10} Q{x + w} {y + h} {x + w - 10} {y + h} "
         f"H{tx + 8} L{tx - 6} {y + h + 14} L{tx - 2} {y + h} H{x + 10} Q{x} {y + h} {x} {y + h - 10} V{y + 10} Q{x} {y} {x + 10} {y} Z")
    return f'<path d="{d}" fill="{fill}" stroke="{INK}" stroke-width="{SW}" stroke-linejoin="round"/>'


def die(x, y, s, fill, pips, rot=0):
    c = s / 2
    spots = {1: [(c, c)], 2: [(s*.28, s*.28), (s*.72, s*.72)], 3: [(s*.26, s*.26), (c, c), (s*.74, s*.74)],
             4: [(s*.28, s*.28), (s*.72, s*.28), (s*.28, s*.72), (s*.72, s*.72)],
             5: [(s*.26, s*.26), (s*.74, s*.26), (c, c), (s*.26, s*.74), (s*.74, s*.74)]}[pips]
    return (f'<g transform="translate({x} {y}) rotate({rot} {c} {c})">'
            f'<rect width="{s}" height="{s}" rx="{s*.2}" fill="{fill}" stroke="{INK}" stroke-width="{SW}"/>'
            + "".join(f'<circle cx="{px:.1f}" cy="{py:.1f}" r="{s*.085:.1f}" fill="{INK}"/>' for px, py in spots) + '</g>')


def sparkle(x, y, s, fill):
    return (f'<path d="M{x} {y-s} L{x+s*.3} {y-s*.3} L{x+s} {y} L{x+s*.3} {y+s*.3} L{x} {y+s} L{x-s*.3} {y+s*.3} '
            f'L{x-s} {y} L{x-s*.3} {y-s*.3} Z" fill="{fill}" stroke="{INK}" stroke-width="3" stroke-linejoin="round"/>')


def sfx_happy():   # two chattering bubbles, one smiling
    return (bubble(10, 16, 70, 50, "#ffffff") + bubble(46, 52, 72, 48, TEAL, "right")
            + f'<path d="M66 72 q16 14 32 0" fill="none" stroke="{INK}" stroke-width="5" stroke-linecap="round"/>'
            f'<path d="M70 66h.01M94 66h.01" stroke="{INK}" stroke-width="7" stroke-linecap="round"/>'
            f'<path d="M24 40 h42 M24 52 h28" stroke="{STEEL}" stroke-width="6" stroke-linecap="round"/>')


def sfx_whistles():  # swirling whistle lines with notes
    return (outlined("M10 84 C30 40 50 110 70 64 S108 40 118 60", TEAL, 9)
            + f'<path d="M30 30 V12 l16 -4 V26" fill="none" stroke="{INK}" stroke-width="5" stroke-linejoin="round"/>'
            f'<circle cx="25" cy="31" r="7" fill="{GOLD}" stroke="{INK}" stroke-width="4"/><circle cx="41" cy="27" r="7" fill="{GOLD}" stroke="{INK}" stroke-width="4"/>'
            + sparkle(98, 92, 9, GOLD) + sparkle(104, 24, 6, "#ffffff"))


def sfx_dice3():
    return die(28, 28, 72, TEAL, 3, -8)


def sfx_dice4():
    return die(28, 28, 72, GOLD, 4, 8)


def sfx_anything():
    return die(10, 44, 62, "#ffffff", 5, -14) + die(58, 18, 58, TEAL, 2, 16) + sparkle(104, 96, 9, GOLD)


def sfx_alarm():   # red beacon with rays
    rays = "".join(f'<path d="{d}" stroke="{RED}" stroke-width="7" stroke-linecap="round"/>'
                   for d in ("M18 40 L6 32", "M110 40 L122 32", "M28 18 L20 6", "M100 18 L108 6", "M64 10 V0"))
    return (rays + f'<path d="M30 96 V62 a34 34 0 0 1 68 0 V96 Z" fill="{RED}" stroke="{INK}" stroke-width="{SW}" stroke-linejoin="round"/>'
            f'<path d="M44 62 a20 20 0 0 1 20 -20" fill="none" stroke="#ffd2cc" stroke-width="7" stroke-linecap="round"/>'
            f'<rect x="20" y="94" width="88" height="20" rx="5" fill="{STEEL}" stroke="{INK}" stroke-width="{SW}"/>')


def sfx_chuckle():  # grinning bubble and a little puff
    return (bubble(8, 14, 84, 62, "#ffffff")
            + f'<path d="M26 40 q24 26 48 0 Z" fill="{INK}"/><path d="M34 50 q16 10 32 0" fill="none" stroke="{RED}" stroke-width="6" stroke-linecap="round"/>'
            f'<path d="M28 32 q6 -6 12 0 M60 32 q6 -6 12 0" fill="none" stroke="{INK}" stroke-width="5" stroke-linecap="round"/>'
            f'<path d="M86 112 a10 10 0 0 1 -2 -19 a12 12 0 0 1 22 -6 a10 10 0 0 1 14 12 a9 9 0 0 1 -4 13 Z" fill="#b9e8a6" stroke="{INK}" stroke-width="4" stroke-linejoin="round"/>')


def sfx_doot():    # a sports whistle with doot notes
    return (f'<rect x="54" y="48" width="62" height="24" rx="5" fill="{GOLD}" stroke="{INK}" stroke-width="{SW}"/>'
            f'<rect x="96" y="44" width="10" height="8" rx="2" fill="{GOLD_DK}" stroke="{INK}" stroke-width="3"/>'
            f'<circle cx="44" cy="76" r="32" fill="{GOLD}" stroke="{INK}" stroke-width="{SW}"/>'
            f'<circle cx="44" cy="76" r="11" fill="{GOLD_DK}" stroke="{INK}" stroke-width="4"/>'
            f'<path d="M22 66 a24 24 0 0 1 14 -16" fill="none" stroke="#fff3d6" stroke-width="5" stroke-linecap="round"/>'
            f'<circle cx="84" cy="24" r="9" fill="{TEAL}" stroke="{INK}" stroke-width="4"/><circle cx="108" cy="16" r="7" fill="{TEAL}" stroke="{INK}" stroke-width="4"/>')


def sfx_wolf():    # a heart with a whistle swoosh
    heart = "M64 112 C24 86 8 60 22 38 C34 20 56 24 64 40 C72 24 94 20 106 38 C120 60 104 86 64 112 Z"
    return (f'<path d="{heart}" fill="{PINK}" stroke="{INK}" stroke-width="{SW}" stroke-linejoin="round"/>'
            f'<path d="M32 44 q6 -10 16 -8" fill="none" stroke="#ffd1df" stroke-width="6" stroke-linecap="round"/>'
            + outlined("M8 20 Q40 4 60 16 T120 10", TEAL, 6, 5))


def sfx_failure():  # cracked chip, sparks and smoke
    return (f'<path d="M56 30 a10 10 0 0 1 4 -18 a12 12 0 0 1 22 0 a10 10 0 0 1 4 18 Z" fill="#c9d1d8" stroke="{INK}" stroke-width="4" stroke-linejoin="round"/>'
            f'<rect x="26" y="42" width="76" height="66" rx="8" fill="#3b4652" stroke="{INK}" stroke-width="{SW}"/>'
            + "".join(f'<path d="M{x} 42 V32 M{x} 108 V118" stroke="{INK}" stroke-width="5" stroke-linecap="round"/>' for x in (44, 64, 84))
            + f'<path d="M58 44 L70 66 L56 78 L72 106" fill="none" stroke="#ffffff" stroke-width="5" stroke-linejoin="round"/>'
            + sparkle(20, 34, 10, "#f2c200") + sparkle(108, 36, 8, "#f2c200") + sparkle(112, 98, 7, RED))


def sfx_beep():    # a bubble of dots and dashes
    return (bubble(8, 18, 112, 66, TEAL)
            + f'<path d="M28 51 h.01 M52 51 h16 M86 51 h.01 M100 51 h.01" stroke="#ffffff" stroke-width="11" stroke-linecap="round"/>'
            f'<path d="M28 51 h.01 M52 51 h16 M86 51 h.01 M100 51 h.01" stroke="{INK}" stroke-width="3" stroke-linecap="round" opacity="0"/>')


def sfx_woo():     # a rising spiral swoop
    return (outlined("M14 108 C40 104 60 88 62 66 C64 46 44 40 38 54 C32 70 58 76 74 62 C92 46 98 24 112 14", GOLD, 9)
            + f'<path d="M104 10 L116 12 L112 24" fill="none" stroke="{INK}" stroke-width="6" stroke-linecap="round" stroke-linejoin="round"/>'
            + sparkle(26, 30, 9, TEAL) + sparkle(98, 88, 7, "#ffffff"))


def sfx_message():  # a hologram beam carrying a message
    return (f'<path d="M64 108 L18 30 H110 Z" fill="{HOLO}" opacity=".35"/>'
            f'<path d="M64 108 L18 30 M64 108 L110 30" stroke="{HOLO}" stroke-width="4" stroke-dasharray="6 6"/>'
            f'<rect x="30" y="12" width="68" height="46" rx="8" fill="#d9f4ff" stroke="{INK}" stroke-width="{SW}"/>'
            f'<path d="M30 16 L64 40 L98 16" fill="none" stroke="{INK}" stroke-width="4" stroke-linejoin="round"/>'
            + "".join(f'<path d="M36 {y} h56" stroke="{HOLO}" stroke-width="2.5" opacity=".8"/>' for y in (26, 36, 46))
            + f'<rect x="44" y="104" width="40" height="16" rx="5" fill="{STEEL}" stroke="{INK}" stroke-width="{SW}"/>'
            f'<circle cx="64" cy="106" r="5" fill="{HOLO}" stroke="{INK}" stroke-width="3"/>')


SFX = {"happy": sfx_happy, "whistles": sfx_whistles, "dice3": sfx_dice3, "dice4": sfx_dice4, "anything": sfx_anything,
       "alarm": sfx_alarm, "chuckle": sfx_chuckle, "doot": sfx_doot, "wolf": sfx_wolf, "failure": sfx_failure,
       "beep": sfx_beep, "woo": sfx_woo, "message": sfx_message}


if __name__ == "__main__":
    for name, fn in SFX.items():
        (OUT / f"sfx-{name}.svg").write_text(
            f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 128" width="128" height="128">'
            f'<title>R2-AU {name}</title>{fn()}</svg>\n')
        print("wrote", OUT / f"sfx-{name}.svg")
