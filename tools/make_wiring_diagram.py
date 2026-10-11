#!/usr/bin/env python3
"""Generate docs/wiring.svg, the candle wiring schematic.

Run:  python3 tools/make_wiring_diagram.py && rsvg-convert -w 2400 docs/wiring.svg -o docs/wiring.png
"""
from pathlib import Path

W, H = 2350, 960
out = []


def add(s):
    out.append(s)


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def line(x1, y1, x2, y2, cls="wire"):
    add(f'<line class="{cls}" x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}"/>')


def text(x, y, s, cls="label", anchor="start"):
    add(f'<text class="{cls}" x="{x}" y="{y}" text-anchor="{anchor}">{esc(s)}</text>')


def dot(x, y):
    add(f'<circle class="dot" cx="{x}" cy="{y}" r="3.5"/>')


def resistor_v(x, y1, y2, label, side="right"):
    """Vertical resistor between y1 and y2 (wire stubs included), label beside it."""
    mid = (y1 + y2) / 2
    h = 34
    line(x, y1, x, mid - h / 2)
    add(f'<rect class="part" x="{x - 7}" y="{mid - h / 2}" width="14" height="{h}"/>')
    line(x, mid + h / 2, x, y2)
    if label:
        if side == "right":
            text(x + 12, mid + 4, label)
        else:
            text(x - 12, mid + 4, label, anchor="end")


def resistor_h(x1, x2, y, label, above=True):
    mid = (x1 + x2) / 2
    w = 34
    line(x1, y, mid - w / 2, y)
    add(f'<rect class="part" x="{mid - w / 2}" y="{y - 7}" width="{w}" height="14"/>')
    line(mid + w / 2, y, x2, y)
    text(mid, y - 12 if above else y + 22, label, anchor="middle")


def capacitor_v(x, y1, y2, label):
    mid = (y1 + y2) / 2
    line(x, y1, x, mid - 5)
    line(x - 12, mid - 5, x + 12, mid - 5, "part")
    line(x - 12, mid + 5, x + 12, mid + 5, "part")
    line(x, mid + 5, x, y2)
    text(x + 16, mid + 4, label)


def led_v(x, y1, y2, color, label=None, arrows=True):
    """LED with the anode at the top (y1) and the cathode at the bottom (y2)."""
    mid = (y1 + y2) / 2
    s = 13
    line(x, y1, x, mid - s)
    add(f'<polygon class="led" style="fill:{color}" points="{x - s},{mid - s} {x + s},{mid - s} {x},{mid + s}"/>')
    line(x - s, mid + s, x + s, mid + s, "part")
    line(x, mid + s, x, y2)
    if arrows:
        for dy in (-6, 4):
            ax, ay = x + s + 4, mid + dy
            line(ax, ay, ax + 12, ay - 10, "arrow")
            add(f'<polygon class="arrowhead" points="{ax + 12},{ay - 10} {ax + 5},{ay - 9} {ax + 11},{ay - 3}"/>')
    if label:
        text(x + 32, mid + 4, label)


def npn(x, y):
    """2N3904: collector at (x, y-24), emitter at (x, y+24), base lead ending at (x-40, y)."""
    bx = x - 14
    add(f'<circle class="part" cx="{x - 4}" cy="{y}" r="22"/>')
    line(bx, y - 13, bx, y + 13, "thick")
    line(x - 40, y, bx, y)
    line(bx, y - 6, x, y - 24)
    line(bx, y + 6, x, y + 24)
    add(f'<polygon class="arrowhead" points="{x},{y + 24} {x - 10},{y + 20} {x - 5},{y + 13}"/>')
    text(x + 8, y - 30, "C", "small")
    text(x + 8, y + 40, "E", "small")
    text(x - 36, y - 10, "B", "small")


def pnp(x, y):
    """2N3906 high side: emitter at (x, y-24) toward 3V3, collector at (x, y+24), base lead ending at (x-40, y)."""
    bx = x - 14
    add(f'<circle class="part" cx="{x - 4}" cy="{y}" r="22"/>')
    line(bx, y - 13, bx, y + 13, "thick")
    line(x - 40, y, bx, y)
    line(bx, y - 6, x, y - 24)
    line(bx, y + 6, x, y + 24)
    # emitter arrow points into the base bar
    add(f'<polygon class="arrowhead" points="{bx},{y - 6} {bx + 10},{y - 10} {bx + 5},{y - 17}"/>')
    text(x + 8, y - 30, "E", "small")
    text(x + 8, y + 40, "C", "small")
    text(x - 36, y - 10, "B", "small")


def net_flag(x, y, name, direction="left"):
    """A labelled net tag whose tip is at (x, y)."""
    w = 9 * len(name) + 20
    if direction == "left":
        pts = f"{x},{y} {x - 10},{y - 10} {x - w},{y - 10} {x - w},{y + 10} {x - 10},{y + 10}"
        add(f'<polygon class="flag" points="{pts}"/>')
        text(x - 14, y + 4, name, "net", anchor="end")
    else:
        pts = f"{x},{y} {x + 10},{y - 10} {x + w},{y - 10} {x + w},{y + 10} {x + 10},{y + 10}"
        add(f'<polygon class="flag" points="{pts}"/>')
        text(x + 14, y + 4, name, "net")
    return w


# ---------------------------------------------------------------- header
add(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">')
add("""<style>
  text { font-family: "DejaVu Sans", Arial, sans-serif; font-size: 13px; fill: #1d1d1d; }
  .title { font-size: 24px; font-weight: bold; }
  .sub { font-size: 14px; fill: #555; }
  .h2 { font-size: 15px; font-weight: bold; }
  .net { font-weight: bold; font-size: 12px; }
  .small { font-size: 11px; fill: #444; }
  .wire { stroke: #202020; stroke-width: 1.8; fill: none; }
  .rail { stroke: #202020; stroke-width: 2.6; fill: none; }
  .thick { stroke: #202020; stroke-width: 3; }
  .part { stroke: #202020; stroke-width: 1.8; fill: #ffffff; }
  .led { stroke: #202020; stroke-width: 1.5; }
  .arrow { stroke: #202020; stroke-width: 1.2; }
  .arrowhead { fill: #202020; }
  .dot { fill: #202020; }
  .flag { fill: #fff3c4; stroke: #a07800; stroke-width: 1.2; }
  .board { fill: #c62828; stroke: #7a1515; stroke-width: 2; rx: 12; }
  .boardtext { fill: #ffffff; font-size: 12px; }
  .pin { fill: #e0e0e0; stroke: #555; stroke-width: 1; }
  .pinused { fill: #ffd54f; stroke: #7a5a00; stroke-width: 1.6; }
  .box { fill: #fafafa; stroke: #9e9e9e; stroke-width: 1.2; rx: 8; }
</style>""")
add(f'<rect width="{W}" height="{H}" fill="#ffffff"/>')
text(30, 42, "OpenWick wiring", "title")
text(30, 66, "SparkFun Pro nRF52840 Mini. Flames: 2N3904 low-side switches. RGB core (common cathode): 2N3906 high-side "
     "switches. All LEDs fed from the board's 3V3 pin. Yellow tags are net names: same name = same wire.", "sub")

# ---------------------------------------------------------------- board
bx0, by0, bw, bh = 170, 150, 250, 400
add(f'<rect class="board" x="{bx0}" y="{by0}" width="{bw}" height="{bh}"/>')
text(bx0 + bw / 2, by0 + 22, "SparkFun Pro nRF52840 Mini", "boardtext", "middle")
text(bx0 + bw / 2, by0 + 38, "top view, USB and JST at the top", "boardtext", "middle")
text(bx0 + bw / 2, by0 + 56, "BAT+ / BAT- pads sit beside the JST", "boardtext", "middle")

left_pins = ["17", "15", "11", "8", "19", "20", "21", "22", "23", "9", "10"]
right_pins = ["VIN", "GND", "RST", "3V3", "4", "5", "28", "29", "30", "31", "3", "2"]
used = {
    "17": "rgb_b", "15": "flame3", "19": "flame0", "20": "flame1", "21": "flame2",
    "22": "rgb_r", "23": "rgb_g", "GND": "GND", "3V3": "3V3", "28": "vbatt",
}
pin_y0 = by0 + 90
for i, p in enumerate(left_pins):
    y = pin_y0 + i * 28
    cls = "pinused" if p in used else "pin"
    add(f'<circle class="{cls}" cx="{bx0 + 16}" cy="{y}" r="7"/>')
    text(bx0 + 30, y + 4, p, "boardtext")
    if p in used:
        line(bx0 + 9, y, bx0 - 2, y)
        net_flag(bx0 - 2, y, used[p], "left")
for i, p in enumerate(right_pins):
    y = pin_y0 - 14 + i * 28
    cls = "pinused" if p in used else "pin"
    add(f'<circle class="{cls}" cx="{bx0 + bw - 16}" cy="{y}" r="7"/>')
    text(bx0 + bw - 30, y + 4, p, "boardtext", "end")
    if p in used:
        line(bx0 + bw - 9, y, bx0 + bw + 2, y)
        net_flag(bx0 + bw + 2, y, used[p], "right")
text(bx0 - 100, by0 + bh + 24, "Yellow pins are the only ones we use. Pin numbers are the header labels (P0.xx).", "small")

# ---------------------------------------------------------------- driver stages
RAIL_Y, GND_Y = 170, 520
X0, STEP = 700, 200
cols = [X0 + i * STEP + (100 if i >= 4 else 0) for i in range(7)]
line(cols[0] - 150, RAIL_Y, cols[-1] + 60, RAIL_Y, "rail")
line(cols[0] - 150, GND_Y, cols[-1] + 60, GND_Y, "rail")
net_flag(cols[0] - 152, RAIL_Y, "3V3", "left")
net_flag(cols[0] - 152, GND_Y, "GND", "left")
text(cols[-1] + 68, RAIL_Y + 4, "3V3 pin (cut by the power switch)", "small")
text(cols[-1] + 68, GND_Y + 4, "GND pin", "small")

stages = [
    ("flame0", "amber 5 mm", "#ffb300", "100 R"),
    ("flame1", "yellow 5 mm", "#ffe600", "100 R"),
    ("flame2", "red 5 mm", "#e53935", "100 R"),
    ("flame3", "mini LED cluster", None, "220 R"),
    ("rgb_r", "RGB red", "#e53935", "100 R"),
    ("rgb_g", "RGB green", "#43a047", "20 R"),
    ("rgb_b", "RGB blue", "#1e88e5", "20 R"),
]

# RGB package outline first, so the elements are drawn on top of it
_r, _b = cols[4], cols[6]
add(f'<rect class="box" x="{_r - 48}" y="{330 - 8}" width="{_b - _r + 96}" height="{400 - 330 + 16}"/>')
COLLECTOR_Y = 360
TR_Y = COLLECTOR_Y + 24
PNP_Y = 230
for i, (net, what, color, rval) in enumerate(stages):
    col = cols[i]
    if i < 4:
        npn(col, TR_Y)
        line(col, TR_Y + 24, col, GND_Y)
        dot(col, GND_Y)
        base_x = col - 40
        dot(base_x, TR_Y)
        resistor_h(base_x - 62, base_x, TR_Y, "1 k")
        net_flag(base_x - 64, TR_Y, net, "left")
        resistor_v(base_x, TR_Y, GND_Y, "100 k", side="left")
        dot(base_x, GND_Y)
        text(col + 8, TR_Y + 70, "2N3904", "small")
    else:
        pnp(col, PNP_Y)
        dot(col, RAIL_Y)
        line(col, RAIL_Y, col, PNP_Y - 24)
        base_x = col - 40
        dot(base_x, PNP_Y)
        resistor_h(base_x - 62, base_x, PNP_Y, "1 k")
        net_flag(base_x - 64, PNP_Y, net, "left")
        resistor_v(base_x, RAIL_Y, PNP_Y, "10 k", side="left")
        dot(base_x, RAIL_Y)
        text(col + 8, PNP_Y - 46, "2N3906", "small")

    if i < 3:
        dot(col, RAIL_Y)
        resistor_v(col, RAIL_Y, 250, rval)
        led_v(col, 250, 320, color, what)
        line(col, 320, col, COLLECTOR_Y)
    elif i == 3:
        xs = [col - 36, col, col + 36]
        for k, x in enumerate(xs):
            dot(x, RAIL_Y)
            resistor_v(x, RAIL_Y, 250, rval if k == 2 else "")
            led_v(x, 250, 320, ["#ffe600", "#e53935", "#ffe600"][k], arrows=False)
            dot(x, 320)
        line(xs[0], 320, xs[-1], 320)
        line(col, 320, col, COLLECTOR_Y)
        text(xs[-1] + 10, 324, "3 to 6 mini LEDs,", "small")
        text(xs[-1] + 10, 338, "one 220 R each", "small")
    else:
        # collector -> series resistor -> element anode; cathodes join below
        resistor_v(col, PNP_Y + 24, 330, rval)
        led_v(col, 330, 400, color, what.split()[-1][0].upper())
        line(col, 400, col, 430)
        dot(col, 430)

# RGB LED package: common cathode bar under the three elements, one wire to GND
r, g, b = cols[4], cols[5], cols[6]
box_x0, box_x1 = r - 48, b + 48
line(r - 40, 430, b, 430)
dot(r - 40, 430)
line(r - 40, 430, r - 40, GND_Y)
dot(r - 40, GND_Y)
text(r - 30, 452, "10 mm diffused RGB LED, common cathode (4 legs, longest = common)", "h2")
text(r - 30, 472, "common cathode straight to GND; the PNPs switch the anodes", "small")

# ---------------------------------------------------------------- battery divider
dx, dy = 110, 690
text(dx - 20, dy - 60, "Battery sense (pin 28 / AIN4), optional", "h2")
w = net_flag(dx, dy, "BAT+", "right")
text(dx, dy + 28, "pad beside the JST", "small")
line(dx + w, dy, dx + 90, dy)
resistor_h(dx + 90, dx + 170, dy, "1 M")
dot(dx + 190, dy)
line(dx + 170, dy, dx + 210, dy)
resistor_h(dx + 210, dx + 290, dy, "1 M")
line(dx + 290, dy, dx + 310, dy)
net_flag(dx + 310, dy, "GND", "right")
line(dx + 190, dy, dx + 190, dy - 40)
net_flag(dx + 190, dy - 40, "vbatt", "right")
capacitor_v(dx + 190, dy, dy + 60, "100 nF")
line(dx + 190, dy + 60, dx + 190, dy + 80)
net_flag(dx + 190, dy + 80, "GND", "right")
text(dx - 20, dy + 130, "Reads half the cell voltage; the firmware doubles it. Without it the", "small")
text(dx - 20, dy + 146, "firmware reports \"battery not connected\" and simply skips the cutoff.", "small")

# ---------------------------------------------------------------- common-anode alternative
ax, ay = 620, 620
add(f'<rect class="box" x="{ax - 20}" y="{ay - 30}" width="600" height="230"/>')
text(ax - 8, ay - 8, "If you ever swap in a common-ANODE RGB LED", "h2")
text(ax - 8, ay + 12, "wire its three elements exactly like the flame channels (2N3904 low side, common leg to 3V3)", "small")
text(ax - 8, ay + 28, "and set the three rgb channels back to PWM_POLARITY_NORMAL in app.overlay.", "small")
text(ax - 8, ay + 60, "Why the PNP for common cathode: an NPN above the LED can only reach the pin voltage", "small")
text(ax - 8, ay + 76, "minus 0.7 V, about 2.6 V, which lights red but not green or blue. The PNP's emitter sits", "small")
text(ax - 8, ay + 92, "on 3V3, so the element gets the full rail minus about 0.15 V, the same as the flames.", "small")
text(ax - 8, ay + 124, "PNP logic is inverted: pin LOW = element on. The firmware handles that (inverted PWM),", "small")
text(ax - 8, ay + 140, "and the 10 k pull-up keeps the core dark while the pins float during reset or bootloader.", "small")
text(ax - 8, ay + 172, "Direct drive from the pins was the fallback: no parts, but 5 mA max per pin and almost no", "small")
text(ax - 8, ay + 188, "headroom for green and blue, so a dim, red-tinted core.", "small")

# ---------------------------------------------------------------- notes
nx, ny = 1270, 605
add(f'<rect class="box" x="{nx - 20}" y="{ny - 15}" width="640" height="310"/>')
text(nx - 8, ny + 8, "Build notes", "h2")
notes = [
    "2N3904 and 2N3906: flat face toward you, legs E B C from left to right.",
    "Flame channels (NPN): 1 k pin to base, 100 k base to GND, emitter to GND,",
    "   LED string from 3V3 into the collector. Pin high = LED on.",
    "RGB channels (PNP): emitter to 3V3, 1 k pin to base, 10 k base to 3V3,",
    "   collector through the resistor into the element anode. Pin low = on.",
    "One series resistor per LED, never shared between LEDs:",
    "   5 mm amber / yellow / red: 100 R (about 11 mA each)",
    "   mini LEDs in the cluster: 220 R each (about 5 mA each, 3 to 6 LEDs)",
    "   RGB red element: 100 R.  RGB green and blue elements: 20 R (10 R if too dim)",
    "All LEDs hang off the board's 3V3 pin, so the power switch cuts them too.",
    "Everything on at full brightness is about 120 mA; the 3V3 regulator gives 600 mA.",
    "The pins are plain outputs: a miswired channel just stays dark. Only connecting",
    "   3V3, VIN or a battery pad straight to a signal pin could hurt the board.",
]
for k, n in enumerate(notes):
    text(nx - 8, ny + 34 + k * 20, n, "small" if n.startswith("   ") else "label")

add(f'<text class="small" x="{W - 20}" y="{H - 14}" text-anchor="end">generated by tools/make_wiring_diagram.py</text>')
add("</svg>")
Path("docs/wiring.svg").write_text("\n".join(out) + "\n")
print("wrote docs/wiring.svg")
