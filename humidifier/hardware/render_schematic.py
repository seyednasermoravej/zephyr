#!/usr/bin/env python3
"""Render the proposed two-sheet circuit as SVG, PNG, and a vector PDF.

Requires matplotlib. This is a conceptual schematic, not a validated PCB netlist.
Run from any directory; output files are written beside this script.
"""
from pathlib import Path
import math
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle, Circle, Polygon, Arc
from matplotlib.backends.backend_pdf import PdfPages

OUT = Path(__file__).resolve().parent
INK, SIGNAL, RETURN, MUTED = '#20364b', '#087c83', '#ad501f', '#536678'
BG, PALE, WARN = '#ffffff', '#eff7f8', '#fff4db'
plt.rcParams.update({'font.family': 'DejaVu Sans', 'svg.fonttype': 'none'})


def sheet(title, subtitle, number):
    fig, ax = plt.subplots(figsize=(18, 11))
    fig.subplots_adjust(left=.025, right=.975, top=.98, bottom=.025)
    ax.set(xlim=(0, 180), ylim=(0, 110), aspect='equal')
    ax.axis('off')
    text(ax, 2, 106, title, 22, weight='bold', va='top')
    text(ax, 2, 99, subtitle, 11, color=MUTED, va='top')
    wire(ax, [(2, 94), (178, 94)], color='#cbd5df', lw=1)
    text(ax, 2, 1.3, 'REV A · 12 OCT 2026 · CONCEPT: validate component ratings, tuning and waveforms before PCB release', 9, color=MUTED)
    text(ax, 178, 1.3, f'{number} / 2', 10, ha='right', color=MUTED)
    return fig, ax


def text(ax, x, y, s, size=11, ha='left', va='center', color=INK, weight='normal'):
    return ax.text(x, y, s, fontsize=size, ha=ha, va=va, color=color,
                   weight=weight, linespacing=1.4)


def wire(ax, pts, color=INK, lw=1.7, dash=False):
    ax.plot(*zip(*pts), color=color, lw=lw, solid_capstyle='round',
            linestyle='--' if dash else '-')


def dot(ax, x, y, color=INK):
    ax.add_patch(Circle((x, y), .48, color=color, zorder=4))


def box(ax, x, y, w, h, label='', size=11, fill=PALE, edge=SIGNAL):
    ax.add_patch(Rectangle((x, y), w, h, facecolor=fill, edgecolor=edge, lw=1.5))
    if label:
        text(ax, x+w/2, y+h/2, label, size, ha='center')


def ground(ax, x, y, label=None):
    wire(ax, [(x, y+2), (x, y)])
    for k, width in enumerate((3, 2, 1)):
        wire(ax, [(x-width, y-k*.8), (x+width, y-k*.8)])
    if label:
        text(ax, x, y-3.5, label, 9, ha='center')


def resistor(ax, x1, y1, x2, y2, label=None, color=INK):
    dx, dy = x2-x1, y2-y1
    length = math.hypot(dx, dy)
    ux, uy = dx/length, dy/length
    nx, ny = -uy, ux
    pts=[(x1,y1)]
    for t, off in [(0.23,0),(.3,1),(.4,-1),(.5,1),(.6,-1),(.7,1),(.77,0),(1,0)]:
        pts.append((x1+t*dx+off*nx, y1+t*dy+off*ny))
    wire(ax, pts, color=color)
    if label:
        if abs(dy)>abs(dx): text(ax, x1+3, (y1+y2)/2, label, 10)
        else: text(ax, (x1+x2)/2, y1+3, label, 10, ha='center')


def capacitor_v(ax, x, top, bottom, label):
    mid=(top+bottom)/2
    wire(ax, [(x, top), (x, mid+1)])
    wire(ax, [(x-3, mid+1), (x+3, mid+1)])
    wire(ax, [(x-3, mid-1), (x+3, mid-1)])
    wire(ax, [(x, mid-1), (x, bottom)])
    text(ax, x+4, mid, label, 10)


def inductor_v(ax, x, top, bottom, label):
    wire(ax, [(x, top), (x, top-2)])
    h=(top-bottom-4)/4
    for k in range(4):
        cy=top-2-h*(k+.5)
        ax.add_patch(Arc((x,cy), 4,h, theta1=-90, theta2=90, color=INK,lw=1.7))
    wire(ax, [(x,bottom+2),(x,bottom)])
    text(ax,x+4,(top+bottom)/2,label,11)


def piezo_v(ax,x,top,bottom,label):
    mid=(top+bottom)/2
    wire(ax,[(x,top),(x,mid+2.5)])
    wire(ax,[(x-3,mid+2.5),(x+3,mid+2.5)])
    box(ax,x-4,mid-1.2,8,2.4,fill=BG,edge=INK)
    wire(ax,[(x-3,mid-2.5),(x+3,mid-2.5)])
    wire(ax,[(x,mid-2.5),(x,bottom)])
    text(ax,x+5,mid,label,12,weight='bold')


def nmos(ax, cx, cy, transform=None, label='Q'):
    """N-MOS symbol, ports local D=(2,8), S=(2,-8), G=(-8,0).

    Channel and separate gate are shown; diode orientation is called out on sheet 2.
    """
    def p(x,y):
        if transform: x,y=transform(x,y)
        return cx+x,cy+y
    for pts in [[(2,8),(2,3),(0,3)],[(0,-3),(2,-3),(2,-8)],
                [(-8,0),(-2,0)],[(-2,-3),(-2,3)]]:
        wire(ax,[p(*v) for v in pts])
    for y in [-3,-.7,1.6]: wire(ax,[p(0,y),p(0,y+1.4)],lw=2.5)
    wire(ax,[p(2,-3),p(2,0),p(.5,0)])
    ax.annotate('',xy=p(.15,0),xytext=p(2,0),arrowprops={'arrowstyle':'->','color':INK,'lw':1.2})
    return {name:p(*v) for name,v in {'D':(2,8),'S':(2,-8),'G':(-8,0)}.items()}


def overview():
    fig,ax=sheet('ESP32 / 24 V / three-piezo atomizer',
                 'Sheet 1 — shared drive stage, independent AC selectors, and average input-current feedback',1)
    text(ax,4,89,'+24 V',13,weight='bold')
    wire(ax,[(4,85),(17,85)])
    box(ax,17,83.5,8,3,fill=BG,edge=INK)
    text(ax,21,89,'F1',10,ha='center')
    wire(ax,[(25,85),(60,85)])
    capacitor_v(ax,36,85,76,'CIN\nbulk + ceramic')
    ground(ax,36,74,'GND')
    dot(ax,36,85)
    inductor_v(ax,60,85,68,'L1: 4.7 µH*')
    dot(ax,60,68)
    text(ax,55,71,'SW',11,ha='right',weight='bold')
    wire(ax,[(60,68),(159,68)])

    q=nmos(ax,58,47)
    wire(ax,[(60,68),q['D']])
    wire(ax,[q['S'],(60,24)],color=RETURN)
    text(ax,64,48,'QMAIN\nN-MOSFET\nVDS / Qg / RDS(on): TBD',10)
    text(ax,61.5,57,'D',9)
    text(ax,61.5,36,'S',9)
    wire(ax,[(60,24),(159,24)],color=RETURN)
    text(ax,112,20.5,'RET — joined power returns; not logic GND',11,ha='center',color=RETURN,weight='bold')

    for x,k,gpio in [(99,0,22),(129,1,23),(159,2,21)]:
        dot(ax,x,68)
        capacitor_v(ax,x,68,57,f'C{k}: 100 nF*')
        piezo_v(ax,x,57,44,f'P{k}')
        box(ax,x-9,31,18,13,f'ACSEL{k}\n2 × N-MOS\nsee sheet 2',10)
        text(ax,x-12,40,f'SEL{k}',9,ha='right',color=SIGNAL)
        text(ax,x-12,35,f'GPIO{gpio}',9,ha='right',color=SIGNAL)
        wire(ax,[(x-12,37),(x-9,37)],color=SIGNAL)
        wire(ax,[(x,31),(x,24)],color=RETURN)
        dot(ax,x,24,RETURN)

    # Main driver uses an explicitly separate input and output reference.
    box(ax,8,37,30,23,fill=PALE)
    text(ax,23,57,'U1: fast gate driver',11,ha='center',weight='bold')
    text(ax,16,48,'3.3 V\nlogic\ninput',10,ha='center')
    text(ax,31,48,'floating\noutput',10,ha='center')
    wire(ax,[(23,39),(23,54)],color=SIGNAL,dash=True,lw=1)
    wire(ax,[(3,47),(8,47)],color=SIGNAL)
    text(ax,3,51,'PWM_GATED',9,color=SIGNAL)
    resistor(ax,38,47,50,47,'RG*')
    dot(ax,48,47)
    resistor(ax,48,47,48,36)
    text(ax,44,39,'RGS*',9,ha='right')
    wire(ax,[(48,36),(48,24),(60,24)],color=RETURN)
    wire(ax,[(31,37),(31,33)],color=RETURN)
    text(ax,31,30.5,'RET',10,ha='center',color=RETURN)
    wire(ax,[(15,37),(15,34)])
    ground(ax,15,32)
    wire(ax,[(15,60),(15,63)])
    text(ax,15,65,'3.3 V',9,ha='center')
    wire(ax,[(31,60),(31,63)],color=SIGNAL)
    text(ax,31,65,'VDRV*',9,ha='center',color=SIGNAL)

    # Shunt and two Kelvin sense nets; no wire shorts RET to GND.
    dot(ax,60,24,RETURN)
    resistor(ax,60,24,60,11,'RS*\nKelvin shunt',color=RETURN)
    wire(ax,[(60,11),(60,9)])
    ground(ax,60,7,'GND')
    dot(ax,60,22,RETURN)
    wire(ax,[(60,22),(73,22)],color=SIGNAL)
    text(ax,73,22,'K+',9,color=SIGNAL)
    dot(ax,60,11)
    wire(ax,[(60,11),(73,11)],color=SIGNAL)
    text(ax,73,11,'K−',9,color=SIGNAL)
    box(ax,88,6,31,11,'U2: sense amplifier\nG, VZERO: TBD',10)
    wire(ax,[(81,14),(88,14)],color=SIGNAL)
    text(ax,80,14,'K+',9,ha='right',color=SIGNAL)
    wire(ax,[(81,9),(88,9)],color=SIGNAL)
    text(ax,80,9,'K−',9,ha='right',color=SIGNAL)
    wire(ax,[(119,11.5),(125,11.5)],color=SIGNAL)
    box(ax,125,6,19,11,'RC filter\n+ input limits',10)
    wire(ax,[(144,11.5),(149,11.5)],color=SIGNAL)
    box(ax,149,6,24,11,'ESP32 ADC1\nGPIO: TBD',10)
    text(ax,130.5,4,'VADC = VZERO + G × RS × IAVG  (after settling)',9,ha='center')

    box(ax,3,5,43,21,fill=PALE)
    text(ax,24.5,23,'Main-drive logic',11,ha='center',weight='bold')
    text(ax,24.5,18,'GPIO19: burst-modulated carrier',9.5,ha='center')
    text(ax,24.5,12.5,'PWM_GATED = GPIO19 & ARM & FAULT_OK',9,ha='center')
    text(ax,24.5,8,'GPIO19 / ARM: 10 kΩ to GND; default OFF',8.5,ha='center')

    box(ax,83,77,91,13,fill=WARN,edge='#cf9d3d')
    text(ax,86,86,'* Provisional: L/C, MOSFET ratings, drive voltage and shunt sizing.',10)
    text(ax,86,81,'No branch may bypass RS. CIN negative stays on GND below RS.',10)
    return fig


def details():
    fig,ax=sheet('Selector drive and current-feedback details',
                 'Sheet 2 — repeat the selector cell three times; component choices remain to be finalized',2)
    box(ax,3,48,105,42,fill=BG,edge='#ccd6df')
    text(ax,6,87,'ACSELk: two opposing N-MOSFETs, common sources',13,weight='bold')
    # Rotate/mirror the standard symbol so the two drains face outward.
    left=nmos(ax,36,71,transform=lambda x,y:(-y,x))
    right=nmos(ax,74,71,transform=lambda x,y:(y,x))
    wire(ax,[(9,73),left['D']])
    text(ax,9,78,'Piezo k −',11)
    wire(ax,[left['S'],right['S']])
    dot(ax,55,73)
    wire(ax,[right['D'],(101,73)],color=RETURN)
    text(ax,101,78,'RET',11,ha='right',color=RETURN)
    text(ax,36,81,'QkA',10,ha='center')
    text(ax,74,81,'QkB',10,ha='center')
    text(ax,55,77,'SREFk',10,ha='center',color=SIGNAL,weight='bold')
    text(ax,25,70,'D',9);text(ax,46,70,'S',9)
    text(ax,62,70,'S',9);text(ax,83,70,'D',9)
    resistor(ax,36,63,36,56)
    resistor(ax,74,63,74,56)
    wire(ax,[(36,63),(47,63)],color=SIGNAL)
    resistor(ax,47,63,47,73)
    wire(ax,[(74,63),(63,63)],color=SIGNAL)
    resistor(ax,63,63,63,73)
    for x,y in [(36,63),(74,63),(47,73),(63,73)]: dot(ax,x,y)
    text(ax,50,66.5,'RGS*\neach',8.5)
    wire(ax,[(36,56),(74,56)],color=SIGNAL)
    text(ax,55,53,'GATEk — one small gate resistor per FET',9,ha='center',color=SIGNAL)
    text(ax,55,50,'Each body diode points from the joined sources toward its outer drain.',9,ha='center')

    # The DC-capable floating driver is represented with a clear isolation barrier.
    box(ax,3,8,105,35,fill=PALE)
    text(ax,6,39.5,'Uk: isolated gate driver + floating DC bias',13,weight='bold')
    box(ax,37,17,31,17,'Input  ||  Output\nGND  ||  SREFk',10,fill=BG)
    wire(ax,[(52,18),(52,33)],color=SIGNAL,dash=True,lw=1)
    wire(ax,[(9,27),(37,27)],color=SIGNAL)
    text(ax,10,30,'SELk / GPIO22, 23 or 21',9,color=SIGNAL)
    resistor(ax,16,27,16,18)
    ground(ax,16,16)
    text(ax,20,21,'10 kΩ\ninput pull-down',9)
    wire(ax,[(68,28),(84,28)],color=SIGNAL)
    text(ax,85,28,'GATEk',10,color=SIGNAL)
    wire(ax,[(61,17),(61,13)],color=SIGNAL)
    text(ax,61,10.5,'SREFk ONLY',10,ha='center',color=SIGNAL,weight='bold')
    wire(ax,[(61,34),(61,36)],color=SIGNAL)
    text(ax,76,35,'VBIASk relative to SREFk',9,color=SIGNAL)
    text(ax,6,11,'Gate pull-downs and local decoupling return to SREFk, never to GND.',9)

    box(ax,113,54,64,36,fill=BG,edge='#ccd6df')
    text(ax,116,86,'Average current + separate fault path',12,weight='bold')
    text(ax,116,79,'K+ / K− are Kelvin connections across RS.',10)
    text(ax,116,72,'ADC: sense amplifier → low-pass → ADC1\n3.3 V supply; ground / VZERO on logic GND.\nScale and limit output to the configured ADC range.',10)
    text(ax,116,60,'FAULT: fast differential sense → comparator\n→ latch → FAULT_OK; latch starts in INHIBIT.\nARM is LOW until one selector is ready; GPIO TBD.',10)

    box(ax,113,8,64,40,fill=WARN,edge='#cf9d3d')
    text(ax,116,44,'Connection and validation notes',12,weight='bold')
    text(ax,116,37,'1. SREF0, SREF1 and SREF2 are separate floating nodes.\n    Each selector needs its own DC-capable bias/driver.',9.5)
    text(ax,116,29,'2. U1 output return and local bypass capacitors go to RET.\n    Its logic side stays on GND; isolated bias avoids a bypass.',9.5)
    text(ax,116,21,'3. RS measures combined return / input-inductor current.\n    It does not measure piezo RMS or QMAIN peak separately.',9.5)
    text(ax,116,13,'4. Keep the chosen selector ON during bursts. Change it\n    only after drive stops and measured ring-down ends.',9.5)
    return fig


def main():
    figures=[('humidifier_reference',overview()),('selector_detail',details())]
    with PdfPages(OUT/'humidifier_reference.pdf') as pdf:
        for name,fig in figures:
            fig.savefig(OUT/f'{name}.svg',facecolor=BG)
            fig.savefig(OUT/f'{name}.png',dpi=150,facecolor=BG)
            pdf.savefig(fig,facecolor=BG)
            plt.close(fig)


if __name__=='__main__':
    main()
