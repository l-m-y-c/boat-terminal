# Wiring diagram: boat-terminal N2K bus power, off-the-shelf parts only.
# Regenerate with: pip install schemdraw && python3 schematic.py
# Writes schematic.svg and schematic.png next to this script, on a white background.
import os
import schemdraw
import schemdraw.elements as elm
schemdraw.config(fontsize=11)
d = schemdraw.Drawing(show=False)
def box(x1, y1, x2, y2, title, fs=10):
    for a, b in [((x1, y1), (x2, y1)), ((x2, y1), (x2, y2)), ((x2, y2), (x1, y2)), ((x1, y2), (x1, y1))]:
        d.add(elm.Line().at(a).to(b))
    d.add(elm.Label().at(((x1+x2)/2, y1-0.15)).label(title, fontsize=fs, valign='top', halign='center'))
def txt(p, s, fs=9, ha='left'):
    d.add(elm.Label().at(p).label(s, fontsize=fs, halign=ha))
def W(a, b): d.add(elm.Line().at(a).to(b))
def dot(p): d.add(elm.Dot().at(p))
G = -5.0
# J1 drop
box(-5.5, 1.4, -1, -10.6, 'N2K Micro-C drop\n(bare-wire end,\nfrom backbone tee)')
for y, n in [(0, 'NET-S  red'), (G, 'NET-C  black'), (-7.5, 'CAN-H  white'), (-8.5, 'CAN-L  blue'), (-9.6, 'Shield  bare')]:
    txt((-1.15, y), n, 10, 'right')
# fuse holder
W((-1, 0), (0.5, 0))
d.add(elm.Fuse().at((0.5, 0)).to((4.5, 0)))
txt((2.5, 0.9), 'F1: Blue Sea 5065 waterproof inline ATO/ATC holder\n+ 1 A ATO blade (Littelfuse 0287001.PXCN)', 9, 'center')
txt((2.5, -0.75), 'crimp/solder splice\n12 AWG pigtail <-> drop wire', 8, 'center')
W((4.5, 0), (12, 0))
W((-1, G), (12, G))
d.add(elm.Ground().at((5.5, G)))
txt((-0.8, G-0.45), 'NET-C = GND (the only ground)', 9)
# optional parts soldered across Pololu VIN/GND pins
dot((8, 0)); dot((8, G)); dot((10.3, 0)); dot((10.3, G))
d.add(elm.DiodeTVS().at((8, 0)).to((8, G)))
txt((7.55, -1.25), 'D1 OPTIONAL\nP6KE27CA\nbidir TVS\n23.1 V Vwm\n37.5 V Vc', 8.5, 'right')
d.add(elm.Capacitor(polar=True).at((10.3, 0)).to((10.3, G)))
txt((10.1, -3.6), 'C1 OPTIONAL\n100 µF 50 V\nEEU-FC1H101', 8.5, 'right')
txt((9.2, 1.0), 'leads soldered straight onto\nPololu VIN / GND pins', 8, 'center')
# Pololu
box(12, 1.4, 17.5, -6.2, 'U1  Pololu D36V28F5\n(#3782)', 10)
for y, n in [(0, 'VIN'), (-2.0, 'EN'), (G, 'GND')]: txt((12.15, y), n, 10)
for y, n in [(0, 'VOUT'), (-2.0, 'PG'), (G, 'GND')]: txt((17.35, y), n, 10, 'right')
txt((14.75, -3.3), '5.3-50 V in, 5 V ±4 %\n3.2 A, rev. prot. 40 V\nsoft-start', 8.5, 'center')
txt((12.15, -2.6), '(EN, PG: leave open)', 8)
# output pigtail
W((17.5, 0), (22, 0)); W((17.5, G), (22, G))
txt((19.75, 0.35), '+5 V  red', 9, 'center'); txt((19.75, G+0.35), 'GND  black', 9, 'center')
box(22, 1.4, 26.8, -6.2, 'J2  USB-C male\npower pigtail\n(2-wire, 22 AWG)', 9.5)
txt((24.4, -3.2), 'VBUS + GND only\nno CC / data wires', 8.5, 'center')
d.add(elm.Arrow().at((26.8, -2.4)).to((29, -2.4)))
txt((27.9, -2.0), 'plug', 9, 'center')
# Waveshare
box(29, 1.4, 37.5, -10.6, 'Waveshare\nESP32-S3-Touch-LCD-7\n(boat-terminal)', 10)
txt((29.15, -2.4), 'socket "USB": VBUS -> 5 V rail\ndirect; CC1/CC2 = 5.1k Rd\n(no PD chip, no Rp needed)\nsocket "UART": leave empty\nbattery header: leave empty', 8.5)
W((-1, -7.5), (29, -7.5)); W((-1, -8.5), (29, -8.5))
txt((29.15, -8.0), 'CAN header PH2.0 H / L\n(TJA1051) jumper 13 OFF', 8.5)
txt((14, -7.15), 'CAN-H / CAN-L straight to the CAN header, keep twisted, no parts', 9, 'center')
W((-1, -9.6), (1.0, -9.6)); txt((1.15, -9.6), 'shield/drain: cut back + insulate, not connected (N2K grounds it only at the power tap)', 9)
txt((16, -11.7), 'Single ground: NET-C = Pololu GND = USB-C GND = board GND = CAN reference.  No other wired connection to the board while it is on the bus.\n'
    'Bus current at 9 V, 5 V / 0.7 A out: about 0.43 A incl. fuse + 6 m drop losses (Pololu curve, η≈94 %); 0.46 A if η is only 87 %  ->  inside 10 LEN (0.50 A).', 9.5, 'center')
txt((16, 2.6), 'boat-terminal N2K bus power - wiring (off-the-shelf parts)   draft 2026-10-18', 13, 'center')
HERE = os.path.dirname(os.path.abspath(__file__))
d.save(os.path.join(HERE, 'schematic.svg'))
d.save(os.path.join(HERE, 'schematic.png'), dpi=230)
# Give both files a solid white background so they read in dark mode.
import re
svg_path = os.path.join(HERE, 'schematic.svg')
svg = open(svg_path, encoding='utf-8').read()
svg = re.sub(r'(<svg[^>]*>)', r'\1\n <rect width="100%" height="100%" fill="#ffffff"/>', svg, count=1)
open(svg_path, 'w', encoding='utf-8').write(svg)
from PIL import Image
png_path = os.path.join(HERE, 'schematic.png')
im = Image.open(png_path).convert('RGBA')
bg = Image.new('RGBA', im.size, (255, 255, 255, 255))
Image.alpha_composite(bg, im).convert('RGB').save(png_path)
