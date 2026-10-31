"""Parametric printable spool station: an OpenAMS display (the 5" ES3C50P, the 4.3" ES3C43P or the 4" E32R40T),
a PN5180 or RC522 reader and a 5 kg bar load cell under a spool cradle, to stand beside an AMS.

The easy way to run it is the menu, `python make.py menuconfig` (see README.md). By hand,
with FreeCAD's command-line interpreter (FreeCAD 0.21+):

    freecadcmd -c "exec(open('build_spool_station.py').read())"

These choose what it builds:
    STATION_BOARD=es3c50p|es3c43p|e32r40t   the display (es3c50p is the default)
    STATION_READER=pn5180|rc522|both  the platform and fin cap for that reader, or one of each
    STATION_SIDE=right|left           which side of the AMS the station stands on
    STATION_RAKE=20                   the screen's lean back from vertical, 10 to 40 degrees
    STATION_BED=256                   the print bed, square, which the base must fit
    STATION_OUT=dir                   where the files go: out/<board>/<side>[-rake<N>]/
    STATION_CUTS=1, STATION_PARTS=1   cut meshes for the pictures, parts for the 3D model
It writes print-oriented STL and STEP files for each part, the platform's two-colour 3MF,
an assembly STEP with the board, load cell, reader, rollers and a 200 mm spool, an exploded
one, and the meshes the pictures are rendered from. Every part is checked against simple
models of what it holds before export; the last line says whether the checks passed.

Layout, seen from the front: the screen on a face leaning back RAKE degrees under a
rounded crown, a cradle behind it on the load cell with two rollers the spool turns on,
and on the cradle's outer side a fin that holds the reader against the spool's flange,
near its rim, where the tag is. The model is built as the right-hand
station: the fin on the right, the AMS in the notch on the left. The board is mounted
rotated 180 degrees from LCDWiki's drawing (the firmware's BOARD_ROTATED_180), so its
USB-C and its P3..P6 headers face right and up, toward the fin and the USB opening in the
right wall. The left-hand station is that model mirrored, except the board, which can't
be: it is mounted the right way up, so its USB-C and headers face the fin again, on the
left and at the bottom. In the build's frame that board is upside down (BOARD_UP = -1).

Coordinates: x to the right, y away from the viewer, z up; the base's front-left
bottom corner is the origin. Bezel features are placed in the bezel's own frame:
u along x, s up the slope from its lower front edge, w into the case from its face.
"""

import json
import math
import os

import FreeCAD as App
import Mesh
import MeshPart
import Part

V = App.Vector
HERE = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else os.getcwd()


def choice(name, default, allowed):
    value = os.environ.get(name, default)
    if value not in allowed:
        raise SystemExit(f"{name} is one of {', '.join(allowed)}, not {value!r}")
    return value


# --- The display boards: the three the firmware builds for (platform/esp32, MMU_BOARD) ---
# (a, b) from the installed board's bottom-left corner, seen from the front, its USB-C on
# the right edge (a = the PCB's width), toward the fin and the USB opening in the right wall.
#   pcb: width, height, thickness; holes: the four mounting holes, M3
#   glass: the LCM and touch glass's outline, (a0, b0, a1, b1); glass_t: their thickness
#   face_to_pcb: the glass's front to the PCB's front face; va: the visible area
#   back_parts: the tallest part behind the PCB; usb: the USB-C's b on the right edge, and
#     its receptacle's centre behind the PCB's back face
#   edge_plugs: a of the 1.25 mm headers on the top edge whose plugs go in sideways, up
#     the slope; side_plugs: b of those on the right edge, by the USB-C, whose plugs go in
#     sideways toward the right wall; back_plugs: (a0, a1, b0, b1) of those whose plugs go
#     in from behind, the ones the reader and scale use
#   sd: a of the microSD slot on the top edge, None where the card must stay out
#   touch_flex: where the touch panel's flex leaves the glass, None if behind it
#   side: the case's width beyond the PCB, each side (the board sits centred)
#   s0: the board's bottom edge, up the slope from the face's lower edge, per side (see
#     BOARD_UP): the plugs it has on its top edge set it, since the crown over the bezel
#     must cover them
BOARDS = {
    # LCDWiki ES3C50P, 5inch_ESP32S3_Size.pdf V1.0 (+/-0.2). The drawing's front view is
    # turned 180 degrees: its right edge (3.45 to the holes) is our left.
    "es3c50p": dict(
        title='5" ES3C50P (ESP32-S3, 800x480 RGB panel, capacitive touch)',
        pcb=(137.0, 84.0, 1.6),
        holes=[(3.45, 3.55), (133.48, 3.55), (3.45, 80.50), (133.48, 80.50)],   # 130.03 x 76.95
        glass=(8.15, 4.05, 128.85, 79.95),          # 120.7 x 75.9
        glass_t=4.2,                                # touch 1.0 + LCM 3.2
        face_to_pcb=5.70,
        va=(13.86, 6.81, 123.06, 72.86),            # 109.2 x 66.05
        back_parts=6.45,
        usb=(42.0, 1.65),                           # centred
        edge_plugs=[71.70, 86.56, 101.42, 114.50, 125.26],   # P3..P6, and P1
        side_plugs=[],
        back_plugs=[],
        sd=53.40,
        touch_flex=(2.15, 6.55, 13.5, 12.15),      # beside the glass, bottom-left
        side=9.5,
        s0=dict(right=8.0, left=4.0),
    ),
    # LCDWiki ES3C43P, 4.3inch_ESP32S3_IPS-模型.pdf V1.0 (+/-0.2), turned 180 degrees like the
    # 5". The board on the bench matches it (the glass 8.6 in from the short edges and 3 from
    # the long ones, the active area 4.5 under the glass's top edge, 7.9 from the glass's
    # front to the PCB's back); the drawing in the specification PDF doesn't. Its 1.25 mm headers are side entry: P3 (SPI), P4 (UART1)
    # and P5 (Expand) on the top edge, and P1 (UART0, the PN5180's 5 V) on the right edge
    # above the USB-C, whose plug needs the wider sides. The touch flex goes through a slot
    # in the PCB, behind the glass.
    "es3c43p": dict(
        title='4.3" ES3C43P (ESP32-S3, 800x480 RGB panel, capacitive touch)',
        pcb=(123.0, 74.0, 1.6),
        holes=[(3.5, 3.5), (119.5, 3.5), (3.5, 70.5), (119.5, 70.5)],   # 116 x 67
        glass=(8.43, 3.13, 114.47, 70.87),          # 106.04 x 67.74
        glass_t=4.78,                               # touch 1.78 + LCM 3.0
        face_to_pcb=6.27,                           # with 1.5 of tape behind the LCM
        va=(13.43, 7.22, 109.47, 61.58),            # 96.04 x 54.36
        back_parts=3.4,
        usb=(33.72, 1.65),
        edge_plugs=[73.52, 92.57, 107.05],          # P3, P4, P5
        side_plugs=[50.62],                         # P1
        back_plugs=[],
        sd=51.20,
        touch_flex=None,
        side=12.5,
        s0=dict(right=8.0, left=8.0),
    ),
    # LCDWiki E32R40T, E32R40T_Size.pdf V1.0 and its STEP model (E32R40T_3D.zip). The panel
    # is portrait and driven landscape: a runs from the ESP32 module's end (its antenna
    # overhangs a notch in the PCB there) to the USB-C's. The LCM is as wide as the PCB. The
    # reader's SPI, its power and reset (I2C) and its BUSY or IRQ (the IO35/IO39 input) are
    # vertical 1.25 mm headers along the top edge; the speaker, UART and battery headers
    # stay empty, and so does the microSD slot, which shares the reader's SPI.
    "e32r40t": dict(
        title='4" E32R40T (ESP32, 480x320 SPI panel, resistive touch)',
        pcb=(111.11, 60.88, 1.6),
        holes=[(3.5, 3.8), (107.61, 3.8), (3.5, 57.08), (107.61, 57.08)],      # 104.11 x 53.28
        glass=(8.27, 0.0, 102.84, 60.88),           # the LCM, 94.57 x 60.88; the touch glass is 0.2 inside it
        glass_t=3.55,                               # touch 1.05 + LCM 2.5
        face_to_pcb=4.05,                           # with 0.5 of tape behind the LCM
        va=(10.27, 2.00, 95.49, 58.88),             # the touch panel's, 85.22 x 56.88
        back_parts=5.1,                             # 5.09 on the drawing
        usb=(30.44, 1.09),                          # centred
        edge_plugs=[],
        side_plugs=[],
        back_plugs=[(14.62, 21.37, 56.17, 60.37),   # I2C: 3.3 V, GND, RST
                    (37.62, 44.37, 56.17, 60.37),   # SPI
                    (79.55, 83.80, 56.18, 60.38)],  # IO35/IO39
        sd=None,
        touch_flex=None,
        side=9.5,
        s0=dict(right=8.0, left=8.0),
    ),
}
BOARD = choice("STATION_BOARD", "es3c50p", BOARDS)
SIDE = choice("STATION_SIDE", "right", ("left", "right"))   # which side of the AMS the station stands on
RAKE = float(os.environ.get("STATION_RAKE", "20"))         # the screen, leaning back from vertical
if not 10 <= RAKE <= 40:
    raise SystemExit(f"STATION_RAKE is 10 to 40 degrees from vertical, not {RAKE:g}")
OUT = os.environ.get("STATION_OUT", os.path.join(HERE, "out", BOARD, SIDE if RAKE == 20 else f"{SIDE}-rake{RAKE:g}"))

B = BOARDS[BOARD]
PCB_W, PCB_H, PCB_T = B["pcb"]
PCB_HOLES = B["holes"]
GLASS, GLASS_T, FACE_TO_PCB, VA = B["glass"], B["glass_t"], B["face_to_pcb"], B["va"]
BACK_PARTS = B["back_parts"]
USB_B, USB_W = B["usb"]
EDGE_PLUGS, SIDE_PLUGS, BACK_PLUGS = B["edge_plugs"], B["side_plugs"], B["back_plugs"]
SD_A, TOUCH_FLEX = B["sd"], B["touch_flex"]
BOARD_UP = 1 if SIDE == "right" else -1   # -1: the board's top edge is down the slope (see above)
BOARD_S0 = B["s0"][SIDE]
PLUG_BACK = 10.0          # a 1.25 mm plug in a vertical header, and its wires' turn, behind the PCB

# --- Bezel -------------------------------------------------------------------------
W = PCB_W + 2 * B["side"]   # case width; the board sits centred
Z_F = 14.0                # height of the face's lower front edge
HEADROOM = 2.0            # from the board, its plugs and its microSD card to the pod roof's inside
T_PLATE = 2.5
CROWN_MIN, CROWN_MAX = 6.0, 30.0   # the crown over the bezel (see fit_board())
FIT_CLR = 0.4             # the glass and the bezel's rim to the pod roof's inside, the plugs' room to its floor
WIN_GROW = 0.3            # window over the visible area, per side
WIN_CHAMFER = 1.5         # 45 degrees round the window, on the face
BOSS_D = 5.8              # board bosses; 6.0 would touch the touch flex
BOSS_FLOAT = 0.05         # glass floats this far behind the face when the PCB is screwed down
M3_PILOT = 2.5            # thread-forming M3
M3_CLEAR = 3.4
CSK_D = 6.4               # M3 countersunk (ISO 10642) head, 90 degrees
# The M3 screws, for the assembly and the exploded view: (length, head across, head height).
# The bezel's are countersunk (ISO 10642; the length takes in the head), the board's socket
# heads (ISO 4762), both thread-forming into the plastic.
M3_CSK = (10.0, 6.0, 1.5)
M3_SOCKET = (6.0, 5.5, 3.0)
RIM_T, RIM_H, RIM_GAP = 1.2, 3.0, 0.3   # glass-locating rim on the bezel's back

# --- Base ----------------------------------------------------------------------------
WALL = 2.5
FLOOR = 3.0
DECK = 2.5                # pod roof behind the face, under the cradle's arc
POD_AXIS = 92.9           # the screen pod's back, in front of the cradle, from the spool's axis
POD_CLR = 2.0             # behind the board and its plugs, to the pod's back wall in front of the AMS
R_CORNER = 8.0            # vertical corners of base, bezel and platform
COL_W = 8.5               # bezel screw columns, inside each side wall
COL_PAD = 4.0             # column round its screw's pilot, in y
SCREW_S_MIN = 5.0         # the lower bezel screws at the lowest, up the slope (see lowest_screw_s()); the
                          # upper ones sit just under the bezel's top
SCREW_X = 5.5             # the bezel screws, in from each side
SCREW_EDGE = 1.2          # bezel left between a countersink and the outline's rounded front corner
PILOT_DEPTH = 10.0
FOOT_D, FOOT_DEPTH = 10.5, 1.0     # recesses for stick-on rubber feet
BED = float(os.environ.get("STATION_BED", "256"))   # the print bed, square; the base must fit it

# --- Load cell: the user's 5 kg bar, calipered 75.05 x 12.7 x 12.7 --------------------------
# Holes measured from an overhead photo, scaled by the calipered size: 5.5 and 15.1 mm from
# the cable end (M4), 5.8 and 16.0 from the other (M4, maybe M5); the slots take the
# difference. The strain gauges' potting runs from 25 to 43 mm from the cable end, so the
# pads stop short of it.
CELL_L, CELL_S = 75.05, 12.7
CELL_HOLES = (5.6, 15.6)  # from each end, on the centre line
CELL_PAD = 23.0           # mounting block length at each end
POTTING = (15.0, 25.3, 42.9)   # the gauges' potting: across (about 15, the user's measure), from and to the cable end
# Both ends take M4 or M5 (with a washer for M4), so the cell fits either way round.
FIXED_SCREW = (5.5, 9.5, 3.0)   # clearance, counterbore d, counterbore depth (from below)
FREE_SCREW = (5.5, 9.5, 4.0)    # clearance, counterbore d, plastic left under the head
SLOT = 1.5                # hole slots +/- along y, for other hole pitches
BRIDGE_LAYER = 0.2        # the layer height the counterbores' bridging steps are made for
POCKET_FLAT = 3.0         # the bridge a plug's pocket in the crown starts with, front to back
POCKET_SIDE = 1.0         # a pocket's sides to its plug or card (their outlines have room of their own)
BOSS_H = 5.0              # spacer under the fixed end
CELL_CLEAR = 3.0          # gap over the cell's middle and fixed end

# --- Platform and cradle ------------------------------------------------------------------
GAP_SIDE = 1.5            # platform over the base walls (also the overload stop)
GAP_FRONT = 2.0           # platform to the pod's back wall
PLAT_MIN = 6.0            # platform thickness at the cradle's lowest point
R_CRADLE = 130.0          # cradle arc, the shape the pod's top follows
SPOOL_R, SPOOL_W = 100.0, 67.0     # Bambu Lab 1 kg spool
SPOOL_W_MAX = 68.0        # the widest spool the rollers carry; the AMS 2 Pro takes 50..68 mm
# The spool turns on two rollers across the cradle, ROLLER_Y in front of and behind its
# axis. Each roller is one printed piece with an M5 pilot hole in each end. At each end an
# M5 screw passes through a 605 bearing and a washer into the roller: its head, outside the
# bearing, holds the bearing's inner ring, and the washer keeps the outer ring off the
# roller's end. The bearings' outer rings drop into seats in the side rails, the screw heads
# into shallow slots behind them: button heads, so most of the rail stays. The rollers stand in troughs in the cradle and lift the
# spool off it, as low as the load cell under the platform lets them.
BEARING = (14.0, 5.0, 5.0)          # 605: outside, bore, width
BEARING_STEP = os.path.join(HERE, "parts", "605ZZ.step")                 # if there; else built from BEARING
SCREW = (20.0, 9.5, 2.75)           # M5 x 20 button head, ISO 7380: under the head, head across, head height
SCREW_STEP = os.path.join(HERE, "parts", "M5_ISO7380_button.step")       # k2-mirror, FreeCAD parts library, CC-BY 3.0
WASHER = (9.0, 5.3, 1.0)            # M5, ISO 7092: across, hole, thickness
WASHER_STEP = os.path.join(HERE, "parts", "M5_ISO7092_washer.step")      # nachotineo, FreeCAD parts library, CC-BY 3.0
M5_PILOT = 4.2            # the screw threads itself into the roller
ROLLER_D = 24.0           # flat ends
END_CHAMFER = 0.4         # on both edges of each end: no elephant's foot, and a lead-in for the screw
ROLLER_Y = 55.0
SEAT_CLR = 0.2            # round the bearing and the screw head in their seats
TROUGH_CLR = 1.0          # round the rollers
TROUGH_FLOOR = 1.5        # under a trough, over the load cell's clearance
# The capture: a collar on each roller, with 45-degree sides, just inside the spool's flange
# on the reader's side, so that flange runs between it and the fin, by the reader.
COLLAR_H = 2.5
FLANGE_T, FLANGE_GAP = 4.0, 0.5    # the spool's flange at its rim, and its gap to the fin
CAPTURE_CLR = 1.5         # the flange's room between the collar and the fin
# Catches, so a spool can't roll off: a rail along each side of the cradle, rising
# RAIL_OVER above the spool's lowest point (the fin stands in the outer one; the inner one,
# RAIL_T thick, holds a bearing and a screw head), and behind the rear roller a lip whose inside hugs the
# spool, CATCH_GAP off it, up to CATCH_ANGLE from its bottom. All are on the platform, so a
# spool leaning on them is still weighed.
RAIL_T, RAIL_OVER = 12.0, 14.0
TIP_R = 8.0               # round over the rails' ends
CATCH_GAP = 3.0
CATCH_ANGLE = 45.0
CATCH_T = 4.0             # the lip's top, behind the hug

# --- Beside an AMS 2 Pro -------------------------------------------------------------------
# The station stands on the AMS's right, its screen pod reaching across in front of the
# AMS's front-right corner: the cradle, and the base under it, end CRADLE_W from the fin,
# and the AMS stands in the notch. Its shape is from Bambu Lab's front and side product
# views, scaled to the listed 372 x 280 x 226 mm: flat front and right side; the shell on
# an 11.7 mm base set in 7 mm at the front and 22 mm at the sides; the lid's dome 50 mm
# back from the front. The vertical corner radius, about 10 mm, is an estimate; the notch's
# inner corner is rounded to match it, AMS_GAP off it.
CRADLE_W = 85.0           # cradle beside the fin: the rollers carry spools up to SPOOL_W_MAX
AMS_GAP = 0.8             # to the AMS's front and side
PLAT_INSET = 2.0          # platform's left edge in from the base's: the AMS meets the base, not the scale
AMS = dict(w=372.0, d=280.0, h=226.0, base_h=11.7, base_in_front=7.0, base_in_side=22.0,
           base_in_back=24.0, shell_top=120.6, corner_r=10.0, dome_r=105.0, dome_y=156.7)

# --- Readers ------------------------------------------------------------------------------
# The fin stands in the outer rail, centred under the spool's axis. The reader stands in a
# slot in it, header down, its back toward the spool: the back rests on a rib at each end,
# STANDOFF off the fin's inner wall, which clears the header's solder joints; its lower
# corners rest on ledges, and between them a pocket open at the bottom takes the Dupont
# plugs and lets the wires straight down into the base. The antenna's centre is under the
# spool's axis, TAG_R below it, where the tag passes. A cap closes the slot from the top and
# holds the board down. The boards' antennas sit at different heights on them, so the
# platform is built for each reader; only the slot and the cap differ.
# Measured from an overhead photo of both boards, scaled by the RC522's calipered size.
# w x l: the board, l from the header edge; ant: the antenna loop's centre from the header
# edge; header: its centre from the board's left edge (component side up, header at the
# top) and its pin count, 2.54 mm pitch; front: the tallest part on the component side.
READERS = {
    "pn5180": dict(w=38.9, l=69.1, t=1.6, ant=48.3, header=(20.0, 13), front=2.8),
    "rc522": dict(w=38.8, l=59.6, t=1.6, ant=38.5, header=(18.9, 8), front=4.6),   # the crystal
}
# The reader the platform and cap are made for, or both (one of each per reader); the slot
# takes either, so a platform made for one is the same but for its ledges.
READER_CHOICE = choice("STATION_READER", "both", tuple(READERS) + ("both",))
READER = "pn5180" if READER_CHOICE == "both" else READER_CHOICE   # the one in the assembly and the pictures
STANDOFF = 2.2            # the board's back off the wall: the header's solder joints
RIB_W = 2.0
SLOT_CLR = 0.4            # board to the slot's ends, and front parts to its outer wall
PLUG_L = 20.0             # Dupont housings, 14 mm, and the wires' turn
PLUG_CLR = 0.4
FIN_IN = 1.2              # spool-side wall
FIN_OUT = 2.5
FIN_END = 6.0             # end walls past the slot
FIN_R = 6.0               # fin top corners
FIN_FILLET = 6.0          # round in the corners where the fin rises from the rail
CAP_MIN = 5.0             # cap over the boards
CAP_GAP = 0.15
TAG_R = 52.0              # the tag's centre from the spool's axis: just outside a refill's cardboard core (from a photo)
# Contactless mark on the fin's outside, a flush inlay in a second colour: depth into the
# wall (three 0.4 mm lines, as it sits on a vertical face), stroke, arc radii.
NFC_MARK = (1.2, 1.6, (3.0, 6.5, 10.0, 13.5))

# --- Derived ------------------------------------------------------------------------
SLOPE = 90.0 - RAKE                      # the face, from horizontal
TH = math.radians(SLOPE)
PLATE_PL = App.Placement(V(0, 0, Z_F), App.Rotation(V(1, 0, 0), SLOPE))   # bezel frame -> case
U0 = (W - PCB_W) / 2      # board's left edge
SLOT_T = STANDOFF + max(r["t"] + r["front"] for r in READERS.values()) + SLOT_CLR
BOARD_W = max(r["w"] for r in READERS.values())
T_FIN = FIN_IN + SLOT_T + FIN_OUT
SLOT_X = (W - T_FIN + FIN_IN, W - FIN_OUT)
X_BACK = SLOT_X[0] + STANDOFF             # the reader's back
X_CUT = W - T_FIN - CRADLE_W              # left edge of the base behind the pod; the AMS's right side


def p1_z(y):
    """The base's slope surface, where the bezel's back rests."""
    return Z_F - T_PLATE / math.cos(TH) + y * math.tan(TH)


def p1_y(z):
    return (z - Z_F + T_PLATE / math.cos(TH)) / math.tan(TH)


Z_CELL0 = BOSS_H + FLOOR                  # cell bottom (the boss stands on the floor)
Z_PB = Z_CELL0 + CELL_S                   # platform bottom = cell top at the free end
Z_RWALL = Z_PB - GAP_SIDE                 # base walls under the platform
Z_CB = Z_PB + PLAT_MIN                    # cradle bottom
R_ROLL = ROLLER_D / 2
Z_ROLL = Z_PB + CELL_CLEAR + TROUGH_FLOOR + TROUGH_CLR + R_ROLL   # the rollers' axis
SPOOL_Z = Z_ROLL + math.sqrt((SPOOL_R + R_ROLL) ** 2 - ROLLER_Y ** 2)
RAIL_H = SPOOL_Z - SPOOL_R - Z_CB + RAIL_OVER      # the rails, over the cradle
X_C = W - T_FIN - SPOOL_W / 2             # load cell under the spool, which leans on the fin
PCB_W_FRONT = T_PLATE + FACE_TO_PCB + BOSS_FLOAT   # depth of the PCB's front face


# --- Helpers ------------------------------------------------------------------------
def box(x0, x1, y0, y1, z0, z1):
    xa, xb = sorted((x0, x1))
    ya, yb = sorted((y0, y1))
    za, zb = sorted((z0, z1))
    return Part.makeBox(xb - xa, yb - ya, zb - za, V(xa, ya, za))


def poly_face(pts):
    return Part.Face(Part.makePolygon([V(*p) for p in pts] + [V(*pts[0])]))


def yz_prism(pts, x0, x1):
    """Polygon (y, z) extruded along x."""
    return poly_face([(x0, y, z) for y, z in pts]).extrude(V(x1 - x0, 0, 0))


def rrect_face(x0, y0, x1, y1, r, z=0.0, facet=False):
    """Rounded rectangle in a plane z = const. facet: the two corners on y0's side meet it in
    a 45-degree flat, in place of the round's last 45 degrees (see outline())."""
    r = min(r, (x1 - x0) / 2 - 1e-3, (y1 - y0) / 2 - 1e-3)

    def at(cx, cy, a):
        return V(cx + r * math.cos(math.radians(a)), cy + r * math.sin(math.radians(a)), z)
    arcs = [((x1 - r, y0 + r), -45 if facet else -90, 0), ((x1 - r, y1 - r), 0, 90),
            ((x0 + r, y1 - r), 90, 180), ((x0 + r, y0 + r), 180, 225 if facet else 270)]
    ends = [(at(cx, cy, a), at(cx, cy, b)) for (cx, cy), a, b in arcs]
    edges = []
    for i, ((cx, cy), a, b) in enumerate(arcs):
        edges.append(Part.Arc(at(cx, cy, a), at(cx, cy, (a + b) / 2), at(cx, cy, b)).toShape())
        if i < 3:
            edges.append(Part.LineSegment(ends[i][1], ends[i + 1][0]).toShape())
    front = [ends[3][1], ends[0][0]]
    if facet:
        k = (2 - math.sqrt(2)) * r                  # where a facet meets the side
        front[1:1] = [V(x0 + k, y0, z), V(x1 - k, y0, z)]
    edges += [Part.LineSegment(p, q).toShape() for p, q in zip(front, front[1:])]
    return Part.Face(Part.Wire(edges))


def rrect_prism(x0, x1, y0, y1, z0, z1, r, facet=False):
    return rrect_face(x0, y0, x1, y1, r, z0, facet).extrude(V(0, 0, z1 - z0))


def outline(z0, z1, inset=0.0):
    """The base's footprint, shrunk by `inset`: the pod across the full width, only as deep
    as the board needs in front of the AMS, and the cradle beside the fin. The AMS stands in
    the notch on the left. The front corners end in a 45-degree flat: the bezel, face down,
    has their lowest bit, where the rest of the round would lean out over the bed."""
    r = R_CORNER - inset
    pod = rrect_prism(inset, W - inset, inset, Y_NOTCH - inset, z0, z1, r, facet=True)
    shape = pod.fuse(rrect_prism(X_CUT + inset, W - inset, inset, Y_END - inset, z0, z1, r, facet=True))
    rc = AMS["corner_r"] + AMS_GAP                 # the notch's corner, round the AMS's
    cx, cy = X_CUT - rc, Y_NOTCH + rc
    fill = box(cx, X_CUT + inset + 0.01, Y_NOTCH - inset - 0.01, cy, z0, z1).cut(
        Part.makeCylinder(rc + inset, z1 - z0 + 2, V(cx, cy, z0 - 1)))
    return shape.fuse(fill).removeSplitter()


def cyl(x, y, z0, z1, d):
    return Part.makeCylinder(d / 2, z1 - z0, V(x, y, z0))


def slot_y(x, y, z0, z1, d, half):
    """Vertical slot, +/- half along y."""
    s = cyl(x, y - half, z0, z1, d).fuse(cyl(x, y + half, z0, z1, d))
    return s.fuse(box(x - d / 2, x + d / 2, y - half, y + half, z0, z1)).removeSplitter()


def on_plate(shape):
    s = shape.copy()
    s.transformShape(PLATE_PL.toMatrix())
    return s


def plate_box(u0, u1, s0, s1, w0, w1):
    """Box in the bezel frame; depth w into the case."""
    return on_plate(box(u0, u1, s0, s1, -w1, -w0))


def plate_cyl(u, s, w0, w1, d):
    return on_plate(Part.makeCylinder(d / 2, w1 - w0, V(u, s, -w1)))


def teardrop_face(r, x=0.0, y=0.0):
    """A hole's section in the plane z = 0: round, with a 45-degree point toward +y, so with
    +y up its top prints without support."""
    k = r / math.sqrt(2)
    t1, t2, tip = V(x - k, y + k, 0), V(x + k, y + k, 0), V(x, y + r * math.sqrt(2), 0)
    return Part.Face(Part.Wire([Part.Arc(t2, V(x, y - r, 0), t1).toShape(),
                                Part.LineSegment(t1, tip).toShape(), Part.LineSegment(tip, t2).toShape()]))


def plate_teardrop(u, s, w0, w1, d):
    """plate_cyl pointed up the face: up, seen along a hole square to the face, is up it."""
    f = teardrop_face(d / 2, u, s)
    f.translate(V(0, 0, -w1))
    return on_plate(f.extrude(V(0, 0, w1 - w0)))


def plate_frustum(u0, u1, s0, s1, grow, w_face, w_back):
    """Rectangular hole that widens by `grow` per side toward the face."""
    def rect(g, z):
        return Part.makePolygon([V(u0 - g, s0 - g, z), V(u1 + g, s0 - g, z), V(u1 + g, s1 + g, z),
                                 V(u0 - g, s1 + g, z), V(u0 - g, s0 - g, z)])
    return on_plate(Part.makeLoft([rect(0, -w_back), rect(grow, -w_face)], True))


def board_uv(a, b):
    """A point of the installed board, in the bezel frame."""
    return U0 + a, BOARD_S0 + (b if BOARD_UP > 0 else PCB_H - b)


def board_rect(a0, b0, a1, b1):
    """A rectangle of the installed board, as (u0, u1, s0, s1) in the bezel frame."""
    u0, s0 = board_uv(a0, b0)
    u1, s1 = board_uv(a1, b1)
    return min(u0, u1), max(u0, u1), min(s0, s1), max(s0, s1)


# --- What the case holds ----------------------------------------------------------------
def board_parts():
    """PCB, glass and the space behind the PCB, in the case."""
    u0, u1, s0, s1 = board_rect(0, 0, PCB_W, PCB_H)
    pcb = plate_box(u0, u1, s0, s1, PCB_W_FRONT, PCB_W_FRONT + PCB_T)
    for a, b in PCB_HOLES:
        u, s = board_uv(a, b)
        pcb = pcb.cut(plate_cyl(u, s, 0, 30, 3.2))
    u0, u1, s0, s1 = board_rect(*GLASS)
    glass = plate_box(u0, u1, s0, s1, T_PLATE + BOSS_FLOAT, T_PLATE + BOSS_FLOAT + GLASS_T)
    u0, u1, s0, s1 = board_rect(1, 1, PCB_W - 1, PCB_H - 1)
    back = plate_box(u0, u1, s0, s1, PCB_W_FRONT + PCB_T, PCB_W_FRONT + PCB_T + BACK_PARTS)
    return {"PCB": pcb, "glass": glass, "parts behind the PCB": back}


def access_envelopes():
    """Plugs and wires that must reach the board."""
    env = {}
    wb = PCB_W_FRONT + PCB_T
    up = BOARD_UP
    for a in EDGE_PLUGS:
        u, s = board_uv(a, PCB_H)
        env[f"header plug at {a:.1f}"] = plate_box(u - 3.5, u + 3.5, s - up, s + 9 * up, wb, wb + 5.5)
    for b in SIDE_PLUGS:
        u, s = board_uv(PCB_W, b)
        env[f"side plug at {b:.1f}"] = plate_box(u - 1, u + 9, s - 3.5, s + 3.5, wb, wb + 5.5)
    for a0, a1, b0, b1 in BACK_PLUGS:
        u0, u1, s0, s1 = board_rect(a0 - 0.5, b0 - 0.5, a1 + 0.5, b1 + 0.5)
        env[f"header plug at {(a0 + a1) / 2:.1f}"] = plate_box(u0, u1, s0, s1, wb, wb + PLUG_BACK)
    if SD_A is not None:
        u, s = board_uv(SD_A, PCB_H)
        env["microSD card"] = plate_box(u - 6, u + 6, s - 2 * up, s + 4 * up, wb, wb + 2.0)
    u, s = board_uv(PCB_W, USB_B)
    w = wb + USB_W
    env["USB-C plug overmold"] = plate_box(u + 0.5, u + 40, s - 6.2, s + 6.2, w - 3.2, w + 3.2)
    return env


# --- The face and the crown ---------------------------------------------------------------
# The bezel's border is as wide above the screen's window as below it. Above the bezel the
# base rounds over in a crown, tangent to the face, that runs on into the cradle's arc, so
# the screen's top follows the arc with no step; the spool stays inside the arc's circle,
# clear of the pod. Inside, the pod's ceiling behind the face is the 45-degree plane through
# the face's top, rising toward it; the bezel's top end lies in that plane too. So the base
# prints all of it forward from behind, with no edge that starts in the air (a gable here
# met the bezel's top end in a V whose bottom edge spanned the pod). Behind it the roof
# follows the arc's inside. What reaches above the plane only locally, the plugs and the
# microSD card, gets a pocket as wide as it under a 45-degree gable whose ridge is DECK
# under the crown's top. The pocket starts with a flat at the height of the bezel's top
# end, POCKET_FLAT deep, so its front prints as a short bridge between its sides; the
# gable grows back from the top of a wall behind the flat. The crown is the smallest whose
# gable clears them by HEADROOM.
def face_pt(s, w=0.0):
    """A point s up the face and w into the case, as (y, z)."""
    return s * math.cos(TH) + w * math.sin(TH), Z_F + s * math.sin(TH) - w * math.cos(TH)


def window_s():
    _, _, s0, s1 = board_rect(VA[0] - WIN_GROW, VA[1] - WIN_GROW, VA[2] + WIN_GROW, VA[3] + WIN_GROW)
    return s0, s1


S_TOP = sum(window_s())                   # the bezel's top: its border above the window, as below it


def bezel_top():
    """The bezel's top end, (y, z), from the face back down at 45 degrees to the slope: the
    base's crown sits on it, and prints over it without support."""
    a = face_pt(S_TOP)
    t = T_PLATE / (math.sin(TH) + math.cos(TH))
    return [a, (a[0] + t, a[1] - t)]


def outline_x(y):
    """The outline's front-left corner at y from the front, as the x of its edge: the round,
    and the 45-degree flat it ends in at the front (see outline())."""
    r = R_CORNER
    if y >= r:
        return 0.0
    if y < r - r / math.sqrt(2):
        return (2 - math.sqrt(2)) * r - y
    return r - math.sqrt(r * r - (r - y) ** 2)


def lowest_screw_s():
    """The lower bezel screws' place up the face: SCREW_S_MIN, or higher where the
    outline's front corner would come within SCREW_EDGE of the countersink. The more
    upright the face, the further up it the corner's round reaches."""
    rr = CSK_D / 2 + SCREW_EDGE
    s = SCREW_S_MIN
    while any(SCREW_X + rr * math.cos(math.radians(a)) < outline_x((s + rr * math.sin(math.radians(a))) * math.cos(TH))
              for a in range(0, 360, 5)):
        s += 0.25
    return s


SCREW_S0 = lowest_screw_s()
SCREW_S = (SCREW_S0, S_TOP - CSK_D / 2 - 1.5)


def crown(r):
    """The crown of radius r: its centre, the cradle arc's centre y it makes, and its
    tangent points on the face and on the arc, all (y, z)."""
    zc = Z_CB + R_CRADLE
    c = face_pt(S_TOP, r)
    yc = c[0] + math.sqrt((R_CRADLE + r) ** 2 - (zc - c[1]) ** 2)
    k = R_CRADLE / (R_CRADLE + r)
    return c, yc, face_pt(S_TOP), (yc + (c[0] - yc) * k, zc + (c[1] - zc) * k)


def gable(r):
    """The ridge of the pockets' gable under a crown of radius r, (y, z)."""
    c = face_pt(S_TOP, r)
    return c[0], c[1] + r - DECK


def pocket_front(r):
    """A pocket's front under a crown of radius r: (z, y0, y1) of the flat it starts with,
    from the bezel's top end back to its wall, or to the gable if that is further back."""
    y0, z = bezel_top()[1]
    gy, gz = gable(r)
    return z, y0, max(y0 + POCKET_FLAT, z - (gz - gy))


def ceiling_c():
    """The pod's ceiling behind the face: the plane y + z = this, through the face's top."""
    return sum(face_pt(S_TOP))


def plane_room(s0, s1, w0, w1, need):
    """How far a rectangle behind the face is under the ceiling plane, past `need`."""
    return min((ceiling_c() - sum(face_pt(s, w))) / math.sqrt(2) for s in (s0, s1) for w in (w0, w1)) - need


def behind_face():
    """(s0, s1, w0, w1, need, local, u0, u1) of what the roof must clear, and by how much:
    the board, its plugs and its microSD card by HEADROOM; the glass and the bezel's rim
    round it, which sit just behind the face, by FIT_CLR. Local: narrow enough for a pocket
    of its own (the plugs and the card), not across the board."""
    inv = PLATE_PL.inverse().toMatrix()
    rects = []
    for name, shape in list(board_parts().items()) + list(access_envelopes().items()):
        s = shape.copy()
        s.transformShape(inv)
        bb = s.BoundBox
        rects.append((bb.YMin, bb.YMax, -bb.ZMax, -bb.ZMin, FIT_CLR if name == "glass" else HEADROOM,
                      name not in ("PCB", "glass", "parts behind the PCB"), bb.XMin, bb.XMax))
    u0, u1, g0, g1 = board_rect(GLASS[0] - RIM_GAP - RIM_T, GLASS[1] - RIM_GAP - RIM_T,
                                GLASS[2] + RIM_GAP + RIM_T, GLASS[3] + RIM_GAP + RIM_T)
    rects.append((g0, g1, T_PLATE, T_PLATE + RIM_H, FIT_CLR, False, u0, u1))
    return rects


def roof_room(r, rects):
    """The least room between those and the roof's inside, under a crown of radius r, past
    what each needs: the ceiling plane, or for what is local, the plane or its pocket (the
    gable behind the pocket's wall, or the flat in front of it); the arc grown by DECK in
    front of its 45-degree point, and the 45-degree ceiling behind it (see make_base)."""
    _, yc, _, _ = crown(r)
    zc = Z_CB + R_CRADLE
    gy, gz = gable(r)
    zf, _, yw = pocket_front(r)
    c = ceiling_c()
    k = yc + zc - math.sqrt(2) * (R_CRADLE + DECK)
    room = float("inf")
    for s0, s1, w0, w1, need, local, _, _ in rects:
        for i in range(41):
            t = i / 40
            for s, w in ((s0 + t * (s1 - s0), w0), (s0 + t * (s1 - s0), w1), (s0, w0 + t * (w1 - w0)), (s1, w0 + t * (w1 - w0))):
                y, z = face_pt(s, w)
                inside = (c - y - z) / math.sqrt(2)
                if local:
                    inside = max(inside, min((gz - gy + y - z) / math.sqrt(2), (gz + gy - y - z) / math.sqrt(2), y - yw),
                                 min(zf - z, yw - y))
                room = min(room, inside - need)
                if y - yc <= z - zc:
                    room = min(room, math.hypot(y - yc, z - zc) - R_CRADLE - DECK - need)
                else:
                    room = min(room, (k - y - z) / math.sqrt(2) - need)
    return room


def floor_room(rects):
    """The least room between the board, its plugs and its card and the pod's floor and
    front wall, past FIT_CLR: plugs on the board's lower edge reach down the slope."""
    room = float("inf")
    for s0, s1, w0, w1, need, *_ in rects:
        if need == HEADROOM:
            for s in (s0, s1):
                for w in (w0, w1):
                    y, z = face_pt(s, w)
                    room = min(room, z - FLOOR - FIT_CLR, y - WALL - FIT_CLR)
    return room


def fit_board():
    """The crown: the smallest whose roof clears what is behind the face. When no crown up
    to CROWN_MAX does (the ceiling plane under the bezel's top is too low for the board),
    or the board's lower plugs reach the floor, the board moves up the face from its
    BOARD_S0 half a millimetre at a time, and the bezel's border grows with it."""
    global BOARD_S0, S_TOP, SCREW_S
    start = BOARD_S0
    while BOARD_S0 <= start + 20:
        S_TOP = sum(window_s())
        SCREW_S = (SCREW_S0, S_TOP - CSK_D / 2 - 1.5)
        rects = behind_face()
        if floor_room(rects) >= 0 and min(plane_room(*q[:5]) for q in rects if not q[5]) >= 0:
            r = CROWN_MIN
            while r <= CROWN_MAX:
                if roof_room(r, rects) >= 0:
                    return r
                r += 0.25
        BOARD_S0 += 0.5
    raise SystemExit(f"rake {RAKE:g}: the board doesn't fit behind the face; a tilt nearer 20 degrees fits")


CROWN_R = fit_board()
CROWN_C, Y_C, CROWN_A, CROWN_B = crown(CROWN_R)
Y_POD = Y_C - POD_AXIS                    # the pod's back, so the cradle keeps its place behind it
Y_P0 = Y_POD + GAP_FRONT


def face_tip():
    """How far up the face, carried on, it would meet the cradle's arc: the corner the crown rounds."""
    zc = Z_CB + R_CRADLE
    b = Y_C * math.cos(TH) + (zc - Z_F) * math.sin(TH)
    d = b * b - Y_C ** 2 - (zc - Z_F) ** 2 + R_CRADLE ** 2
    if d < 0:
        raise SystemExit(f"rake {RAKE:g}: the face, carried on up, passes in front of the cradle's arc; a tilt nearer 20 degrees fits")
    return b - math.sqrt(d)


L_PLATE = face_tip()
Z_TOP = face_pt(L_PLATE)[1]
# The platform ends behind the rear catch's top.
Y_END = Y_C + (SPOOL_R + CATCH_GAP) * math.sin(math.radians(CATCH_ANGLE)) + CATCH_T


# In front of the AMS the pod ends just behind what the board holds there.
def notch_y():
    reach = 0.0
    keep = box(-1, X_CUT + WALL, -50, 300, -50, 300)
    for name, shape in list(board_parts().items()) + list(access_envelopes().items()):
        if name.startswith("USB"):
            continue
        s = shape.common(keep)
        if s.Volume > 1e-6:
            reach = max(reach, s.BoundBox.YMax)
    return math.ceil(reach + POD_CLR + WALL)


Y_NOTCH = notch_y()
# The load cell's free end, which carries the platform, centred under the spool's axis.
CELL_Y = Y_C - CELL_L / 2 + sum(CELL_HOLES) / 2
# The fin, centred under the spool's axis, and the reader standing in it (see READERS).
SLOT_W = BOARD_W + 2 * SLOT_CLR
Y_FIN = (Y_C - SLOT_W / 2 - FIN_END, Y_C + SLOT_W / 2 + FIN_END)
Y_TAG = Y_C                               # the antenna's centre, under the spool's axis
Z_TAG = SPOOL_Z - TAG_R
FIN_TOP = Z_TAG + max(r["l"] - r["ant"] for r in READERS.values()) + CAP_MIN


# --- Base ----------------------------------------------------------------------------
def make_base():
    # Up the front to the bezel's lower end, along it, up the slope behind the bezel, over its
    # top end to the face and up it to where it would meet the arc: the crown rounds that.
    pod = yz_prism([(0, 0)] + bezel_end() + bezel_top()[::-1] + [face_pt(L_PLATE), (Y_POD, Z_TOP), (Y_POD, 0)], 0, W)
    rear = box(0, W, Y_POD - 1, Y_END, 0, Z_RWALL)
    base = pod.fuse(rear).common(outline(0, Z_TOP + 1))
    # Cavities: the pod (open where the slope is), and the rear tub. The pod's roof is
    # DECK thick under the arc. Its inside follows the arc in front of the arc's 45-degree
    # point, where the arc is steeper than 45 degrees, and behind it is the 45-degree plane
    # z + y = k touching the arc there, so it all prints without support.
    y_w = Y_POD - WALL
    zc = Z_CB + R_CRADLE
    k = Y_C + zc - math.sqrt(2) * (R_CRADLE + DECK)
    # All of it inside the walls, behind the bezel, and under the ceiling plane, with a
    # pocket under the gable in the crown round each plug or card that reaches above it.
    big = 400.0
    c = ceiling_c()
    under = yz_prism([(-big, c + big), (big, c - big), (big, -big), (-big, -big)], -1, W + 1)
    gy, gz = gable(CROWN_R)
    rise = yz_prism([(gy - big, gz - big), (gy, gz), (gy + big, gz - big)], -1, W + 1)
    # A pocket: the flat it starts with, and behind the flat's wall, the gable.
    zf, _, yw = pocket_front(CROWN_R)
    pocket = rise.common(box(-big, big, yw, big, -big, big)).fuse(box(-big, big, -big, yw, -big, zf))
    for s0, s1, w0, w1, need, local, u0, u1 in behind_face():
        if local and plane_room(s0, s1, w0, w1, need) < 0:
            under = under.fuse(pocket.common(box(u0 - POCKET_SIDE, u1 + POCKET_SIDE, -big, big, -big, big)))
    behind = yz_prism([(p1_y(-big), -big), (p1_y(big), big), (big, big), (big, -big)], -1, W + 1)
    inner = outline(FLOOR, Z_TOP + 2, WALL).common(under).common(behind)
    base = base.cut(yz_prism([(WALL, FLOOR), (WALL, k - WALL), (y_w, k - y_w), (y_w, FLOOR)], WALL, W - WALL).common(inner))
    d = zc - Y_C                         # the 45-degree line through the arc's centre: z = y + d
    zt = max(Z_TOP, y_w + d) + 1
    front = yz_prism([(WALL, WALL + d), (y_w, y_w + d), (y_w, zt), (WALL, zt)], WALL, W - WALL)
    base = base.cut(front.common(box(WALL, W - WALL, WALL, y_w, FLOOR, zt)).common(inner).cut(cradle_cut(R_CRADLE + DECK)))
    base = base.cut(outline(FLOOR, Z_RWALL + 1, WALL).common(box(0, W, Y_POD, Y_END, FLOOR, Z_RWALL + 1)))
    # Columns for the bezel screws, round each screw's pilot, clipped to the outside shape.
    # Where the USB-C opening passes through one, it reaches in front of the opening: cut
    # off at the opening's pointed top, the column's front would start in the air.
    outer = pod.common(outline(0, Z_TOP + 1))
    u_usb, s_usb = board_uv(PCB_W, USB_B)
    usb = usb_opening(u_usb, s_usb, PCB_W_FRONT + PCB_T + USB_W)
    ub = usb.BoundBox
    for s in SCREW_S:
        y0 = s * math.cos(TH) + T_PLATE * math.sin(TH) - COL_PAD
        y1 = s * math.cos(TH) + (T_PLATE + PILOT_DEPTH) * math.sin(TH) + COL_PAD
        y0 = 0 if y0 < WALL else y0
        for x0, x1 in ((0, COL_W), (W - COL_W, W)):
            y = min(y0, ub.YMin - 1) if x1 > ub.XMin and y0 < ub.YMax and y1 > ub.YMin else y0
            base = base.fuse(box(x0, x1, max(y, 0), min(y1, Y_POD - WALL), 0, Z_TOP).common(outer))
    # A plug on the board's right edge (the 4.3" board's UART0, by the USB-C) reaches into the
    # column beside it: a notch for it, its roof rising 45 degrees to the pod so it prints.
    for name, env in access_envelopes().items():
        if name.startswith("side plug"):
            eb = env.BoundBox
            x0 = W - COL_W - 0.5
            pts = [(x0, eb.YMin, eb.ZMin), (eb.XMax, eb.YMin, eb.ZMin), (eb.XMax, eb.YMin, eb.ZMax),
                   (x0, eb.YMin, eb.ZMax + eb.XMax - x0)]
            base = base.cut(poly_face(pts).extrude(V(0, eb.YMax - eb.YMin, 0)))
    base = base.cut(cradle_cut(R_CRADLE)).cut(crown_cut())
    # Room round the bezel's board bosses: the upper ones reach into the base under the crown.
    # These and the screws' pilots run nearly level, so they come to a point at the top.
    for a, b in PCB_HOLES:
        u, s = board_uv(a, b)
        base = base.cut(plate_teardrop(u, s, T_PLATE - 0.01, PCB_W_FRONT + 0.5, BOSS_D + 1.0))
    for s in SCREW_S:
        for u in (SCREW_X, W - SCREW_X):
            base = base.cut(plate_teardrop(u, s, T_PLATE - 0.01, T_PLATE + PILOT_DEPTH, M3_PILOT))
    # Pod back wall: wires and the load cell's cable pass under the platform, through
    # openings peaked at 45 degrees so their tops print without bridging.
    for x in (X_C - 25, X_C, X_C + 25):
        pts = [(x - 11, FLOOR), (x + 11, FLOOR), (x + 11, 8), (x, 19), (x - 11, 8)]
        base = base.cut(poly_face([(px, Y_POD - WALL - 1, pz) for px, pz in pts]).extrude(V(0, WALL + 2, 0)))
    # USB-C opening in the right wall, at the receptacle.
    base = base.cut(usb)
    # Load cell fixed end: a block on the floor, M5 from below.
    yf = CELL_Y - CELL_L / 2
    base = base.fuse(box(X_C - 10, X_C + 10, yf - 1, yf + CELL_PAD, 0, Z_CELL0))
    for h in CELL_HOLES:
        y = yf + h
        base = base.cut(slot_y(X_C, y, -1, Z_CELL0 + 1, FIXED_SCREW[0], SLOT))
        base = base.cut(slot_y(X_C, y, -1, FIXED_SCREW[2], FIXED_SCREW[1], SLOT))
    # Over the counterbores, a layer bridges across them (along x) round a channel as long as
    # each screw's slot, and the next bridges the channels (along y) round the slots.
    d, dh, top = FIXED_SCREW
    bore = None
    for h in CELL_HOLES:
        b = slot_y(X_C, yf + h, top - 0.01, top + BRIDGE_LAYER, dh, SLOT)
        bore = b if bore is None else bore.fuse(b)
    for h in CELL_HOLES:
        y0, y1 = yf + h - SLOT - d / 2, yf + h + SLOT + d / 2
        base = base.cut(bore.common(box(X_C - dh, X_C + dh, y0, y1, top - 0.01, top + BRIDGE_LAYER)))
        base = base.cut(box(X_C - d / 2, X_C + d / 2, y0, y1, top, top + 2 * BRIDGE_LAYER))
    # NAU7802 pad: a low rim for foam tape, on the pod's floor under the board's back.
    nx, ny = 45.0, Y_NOTCH - WALL - 14.5
    rim = box(nx - 16.2, nx + 16.2, ny - 13.2, ny + 13.2, FLOOR, FLOOR + 2).cut(
        box(nx - 15, nx + 15, ny - 12, ny + 12, FLOOR - 1, FLOOR + 3))
    base = base.fuse(rim)
    for x, y in ((12, 12), (W - 12, 12), (12, Y_NOTCH - 12), (X_CUT + 12, Y_END - 12), (W - 12, Y_END - 12)):
        base = base.cut(cyl(x, y, -1, FOOT_DEPTH, FOOT_D))
    return base.removeSplitter()


def usb_opening(u, s, w, length=15.0, width=9.5):
    """Stadium through the right wall: long side up the slope, like the receptacle. Its top
    end comes to a 45-degree point, so the wall over it prints without support."""
    r = width / 2
    prof = Part.Face(Part.Wire([
        Part.LineSegment(V(0, s - length / 2 + r, -w - r), V(0, s + length / 2 - r, -w - r)).toShape(),
        Part.Arc(V(0, s + length / 2 - r, -w - r), V(0, s + length / 2, -w), V(0, s + length / 2 - r, -w + r)).toShape(),
        Part.LineSegment(V(0, s + length / 2 - r, -w + r), V(0, s - length / 2 + r, -w + r)).toShape(),
        Part.Arc(V(0, s - length / 2 + r, -w + r), V(0, s - length / 2, -w), V(0, s - length / 2 + r, -w - r)).toShape(),
    ]))
    up = (math.sin(TH), math.cos(TH))           # up, in the bezel frame's (s, -w)
    c = (s + length / 2 - r, -w)
    side = [(c[0] + r * (up[0] * math.cos(a) - up[1] * math.sin(a)), c[1] + r * (up[0] * math.sin(a) + up[1] * math.cos(a)))
            for a in (math.pi / 4, -math.pi / 4)]
    tip = (c[0] + r * math.sqrt(2) * up[0], c[1] + r * math.sqrt(2) * up[1])
    point = Part.Face(Part.makePolygon([V(0, *p) for p in (side[0], tip, side[1], c, side[0])]))
    hole = None
    for f in (prof, point):
        f.translate(V(u - 1, 0, 0))
        f = f.extrude(V(W - u + 5, 0, 0))
        hole = f if hole is None else hole.fuse(f).removeSplitter()
    return on_plate(hole)


# --- Bezel -----------------------------------------------------------------------------
def make_bezel():
    # Up to S_TOP: the base's crown carries on from there.
    plate = yz_prism([(0, Z_F)] + bezel_top() + bezel_end()[:0:-1], 0, W)
    # The base's round corners; the face's edges stay square, since it prints face down.
    plate = plate.common(outline(0, Z_TOP + 1))
    # Window over the visible area, chamfered on the face.
    u0, u1, s0, s1 = board_rect(VA[0] - WIN_GROW, VA[1] - WIN_GROW, VA[2] + WIN_GROW, VA[3] + WIN_GROW)
    plate = plate.cut(plate_frustum(u0, u1, s0, s1, WIN_CHAMFER + 0.01, -0.01, WIN_CHAMFER))
    plate = plate.cut(plate_box(u0, u1, s0, s1, -1, T_PLATE + 1))
    # Glass-locating rim, open at the touch flex.
    g0u, g1u, g0s, g1s = board_rect(GLASS[0] - RIM_GAP, GLASS[1] - RIM_GAP, GLASS[2] + RIM_GAP, GLASS[3] + RIM_GAP)
    rim = plate_box(g0u - RIM_T, g1u + RIM_T, g0s - RIM_T, g1s + RIM_T, T_PLATE - 0.01, T_PLATE + RIM_H).cut(
        plate_box(g0u, g1u, g0s, g1s, 0, T_PLATE + RIM_H + 1))
    if TOUCH_FLEX is not None:
        fu0, fu1, fs0, fs1 = board_rect(TOUCH_FLEX[0] - 0.5, TOUCH_FLEX[1] - 0.5, TOUCH_FLEX[2] + 0.5, TOUCH_FLEX[3] + 0.5)
        rim = rim.cut(plate_box(fu0, fu1, fs0, fs1, 0, T_PLATE + RIM_H + 1))
    plate = plate.fuse(rim)
    # Board bosses: the PCB's front face rests on them; M3 x 6 from behind.
    for a, b in PCB_HOLES:
        u, s = board_uv(a, b)
        plate = plate.fuse(plate_cyl(u, s, T_PLATE - 0.01, PCB_W_FRONT, BOSS_D))
        plate = plate.cut(plate_cyl(u, s, PCB_W_FRONT - 6.0, PCB_W_FRONT + 0.1, M3_PILOT))
    # Countersunk M3 into the base's columns.
    for s in SCREW_S:
        for u in (SCREW_X, W - SCREW_X):
            plate = plate.cut(plate_cyl(u, s, -1, T_PLATE + 1, M3_CLEAR))
            depth = (CSK_D - M3_CLEAR) / 2
            cone = Part.makeCone(M3_CLEAR / 2, CSK_D / 2 + 0.5, depth + 0.5, V(u, s, -depth), V(0, 0, 1))
            plate = plate.cut(on_plate(cone))
    return plate.removeSplitter()


def corner_round(a, t, b, c, r, x0, x1):
    """What to cut from a corner, seen along x, to round it: the corner t, the round's
    tangent points a and b, and its centre c, all (y, z)."""
    return yz_prism([a, t, b, c], x0, x1).cut(Part.makeCylinder(r, x1 - x0 + 2, V(x0 - 1, c[0], c[1]), V(1, 0, 0)))


def crown_cut():
    """What the crown rounds off the corner where the face, carried on, meets the arc."""
    return corner_round(CROWN_A, face_pt(L_PLATE), CROWN_B, CROWN_C, CROWN_R, -1, W + 1)


def rail_tips(xa, xb):
    """The rounds over a rail's ends, from x xa to xb: TIP_R, tangent to the end and to the
    rail's top, which runs RAIL_H over the cradle."""
    rr, zc, r = R_CRADLE - RAIL_H, Z_CB + R_CRADLE, TIP_R
    cut = None
    for y_end, sgn in ((Y_P0, 1), (Y_END, -1)):
        cy = y_end + sgn * r
        cz = zc - math.sqrt((rr + r) ** 2 - (cy - Y_C) ** 2)
        k = rr / (rr + r)
        t = (y_end, zc - math.sqrt(rr ** 2 - (y_end - Y_C) ** 2))
        c = corner_round((y_end, cz), t, (Y_C + (cy - Y_C) * k, zc + (cz - zc) * k), (cy, cz), r, xa, xb)
        cut = c if cut is None else cut.fuse(c)
    return cut


def fin_fillets():
    """The rounds, FIN_FILLET, in the corners where the fin rises from the outer rail's top,
    which runs RAIL_H over the cradle. Each piece reaches a little into the rail, so it fuses
    without a sliver; the bearings' seats, cut after, keep their way open through it."""
    rr, zc, r = R_CRADLE - RAIL_H, Z_CB + R_CRADLE, FIN_FILLET
    out = None
    for y_wall, sgn in ((Y_FIN[0], -1), (Y_FIN[1], 1)):
        cy = y_wall + sgn * r
        cz = zc - math.sqrt((rr - r) ** 2 - (cy - Y_C) ** 2)
        k = (rr + 3.0) / (rr - r)
        low = (y_wall, zc - math.sqrt(rr ** 2 - (y_wall - Y_C) ** 2) - 3.0)
        piece = yz_prism([low, (y_wall, cz), (cy, cz), (Y_C + (cy - Y_C) * k, zc + (cz - zc) * k)], W - T_FIN, W)
        piece = piece.cut(Part.makeCylinder(r, T_FIN + 2, V(W - T_FIN - 1, cy, cz), V(1, 0, 0)))
        out = piece if out is None else out.fuse(piece)
    return out


def bezel_end():
    """The bezel's lower end, (y, z) from the face's lower edge: square to the face, so it
    stands upright when the bezel prints face down. The base's front runs up to that edge."""
    return [(0, Z_F), (T_PLATE * math.sin(TH), Z_F - T_PLATE * math.cos(TH))]


# --- Platform -------------------------------------------------------------------------------
def cradle_cut(r):
    """Everything inside the cradle circle of radius r."""
    return Part.makeCylinder(r, W + 20, V(-10, Y_C, Z_CB + R_CRADLE), V(1, 0, 0))


def board_z(reader):
    """The reader's header edge and its top edge."""
    r = READERS[reader]
    return Z_TAG - r["ant"], Z_TAG - r["ant"] + r["l"]


def pocket_w(reader):
    """The plug pocket between the ledges: the header's plugs, either way round."""
    r = READERS[reader]
    a, pins = r["header"]
    return 2 * (abs(a - r["w"] / 2) + (pins - 1) * 2.54 / 2 + 1.27 + PLUG_CLR)


def make_platform(reader):
    top = Z_CB + R_CRADLE
    x0 = X_CUT + PLAT_INSET
    foot = rrect_prism(x0, W, Y_P0 - R_CORNER, Y_END, Z_PB, top, R_CORNER).common(box(0, W, Y_P0, Y_END, Z_PB, top))
    plat = foot.cut(cradle_cut(R_CRADLE))
    # The catches: side rails, and the lip behind the rear roller.
    band = cradle_cut(R_CRADLE).cut(cradle_cut(R_CRADLE - RAIL_H))
    for xa, xb in ((x0, x0 + RAIL_T), (W - T_FIN, W)):
        plat = plat.fuse(band.common(foot).common(box(xa, xb, Y_P0, Y_END, Z_PB, top)))
    hug = Part.makeCylinder(SPOOL_R + CATCH_GAP, W + 20, V(-10, Y_C, SPOOL_Z), V(1, 0, 0))
    z_lip = SPOOL_Z - (SPOOL_R + CATCH_GAP) * math.cos(math.radians(CATCH_ANGLE))
    y_lip = Y_C + ROLLER_Y + R_ROLL + TROUGH_CLR
    plat = plat.fuse(foot.common(box(x0, W, y_lip, Y_END, Z_PB, z_lip)).cut(hug))
    for xa, xb in ((x0, x0 + RAIL_T), (W - T_FIN, W)):
        plat = plat.cut(rail_tips(xa - 1, xb + 1))
    # Fin on the right, rounded at the top.
    y0, y1 = Y_FIN
    prof = rrect_face(y0, Z_PB - 20, y1, FIN_TOP, FIN_R)       # in a z = 0 plane, (y, z) as (x, y)
    fin = prof.extrude(V(0, 0, T_FIN))
    fin.transformShape(App.Matrix(0, 0, 1, W - T_FIN, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1))
    fin = fin.common(box(W - T_FIN, W, y0, y1, Z_PB, FIN_TOP + 1))
    plat = plat.fuse(fin).fuse(fin_fillets())
    # Rollers: a trough each between the rails, room for the collar, and in each rail the
    # bearing's seat, open at the top, with a narrower slot behind it for the screw's head.
    xa, xb = roller_span()
    od, bore, width = BEARING
    for y in roller_ys():
        plat = plat.cut(box(xa, xb, y - R_ROLL - TROUGH_CLR, y + R_ROLL + TROUGH_CLR, Z_ROLL - R_ROLL - TROUGH_CLR, top))
        rc, (c0, c1) = R_ROLL + COLLAR_H + TROUGH_CLR, collar_x()
        plat = plat.cut(box(c0 - TROUGH_CLR, c1 + TROUGH_CLR, y - rc, y + rc, Z_ROLL - rc, top))
        for x, sgn in ((xa, -1), (xb, 1)):            # sgn: out into the rail
            step = x + sgn * (0.1 + width + SEAT_CLR)
            plat = plat.cut(u_slot(x - sgn * 0.01, step, y, od / 2 + SEAT_CLR, top))
            plat = plat.cut(u_slot(step, step + sgn * (SCREW[2] + 0.5), y, SCREW[1] / 2 + 0.5, top))
    # Solid, printed with sparse infill: hollows under the curved cradle left its skin as a
    # shallow overhang, and saved no plastic over 15 % infill.
    yf = CELL_Y + CELL_L / 2 - CELL_PAD                # free end's pad starts here
    # Clearance over the cell's middle and fixed end.
    hw = max(CELL_S, POTTING[0]) / 2 + 2                # 2 mm round the cell and its potting
    plat = plat.cut(box(X_C - hw, X_C + hw, CELL_Y - CELL_L / 2 - 2, yf, Z_PB - 1, Z_PB + CELL_CLEAR))
    # Free end: M4 from the top, counterbored into the cradle.
    for h in CELL_HOLES:
        y = CELL_Y + CELL_L / 2 - h
        plat = plat.cut(slot_y(X_C, y, Z_PB - 1, top, FREE_SCREW[0], SLOT))
        plat = plat.cut(slot_y(X_C, y, Z_PB + FREE_SCREW[2], top, FREE_SCREW[1], SLOT))
    plat = plat.cut(reader_slot(reader))
    plat = plat.cut(nfc_mark())
    return plat.removeSplitter()


def reader_slot(reader):
    """The slot from the fin's top, less the ribs, and the plug pocket below the board,
    between the ledges, open through the platform's bottom."""
    zb, _ = board_z(reader)
    hw = SLOT_W / 2
    slot = box(SLOT_X[0], SLOT_X[1], Y_C - hw, Y_C + hw, zb, FIN_TOP + 1)
    for sgn in (-1, 1):
        slot = slot.cut(box(SLOT_X[0] - 1, X_BACK, Y_C + sgn * hw, Y_C + sgn * (hw - RIB_W), zb - 1, FIN_TOP + 2))
    pw = pocket_w(reader) / 2
    return slot.fuse(box(SLOT_X[0], SLOT_X[1], Y_C - pw, Y_C + pw, Z_PB - 1, zb + 0.01)).removeSplitter()


def nfc_mark():
    """Contactless mark, four arcs opening toward the back, on the fin's outside: the cut,
    which runs 1 mm past the face. The arcs' upper ends come to a point (+y here is up), so
    the engraving's top prints without support."""
    depth, stroke, radii = NFC_MARK
    cut = None
    for r in radii:
        ring = Part.makeCylinder(r + stroke / 2, depth + 1, V(0, 0, 0)).cut(Part.makeCylinder(r - stroke / 2, depth + 1, V(0, 0, 0)))
        wedge = Part.Face(Part.makePolygon([V(0, 0, -1), V(30, -30 * math.tan(math.radians(40)), -1),
                                            V(30, 30 * math.tan(math.radians(40)), -1), V(0, 0, -1)])).extrude(V(0, 0, depth + 3))
        arc = ring.common(wedge)
        a = math.radians(40)
        arc = arc.fuse(Part.makeCylinder(stroke / 2, depth + 1, V(r * math.cos(a), -r * math.sin(a), 0)))
        arc = arc.fuse(teardrop_face(stroke / 2, r * math.cos(a), r * math.sin(a)).extrude(V(0, 0, depth + 1)))
        cut = arc if cut is None else cut.fuse(arc)
    cut.translate(V(-radii[-1] * (1 + math.cos(math.radians(40))) / 2, 0, 0))   # centred
    # (x, y, z) -> (W - depth .. W, Y_TAG + x, Z_TAG + y)
    cut.transformShape(App.Matrix(0, 0, 1, W - depth, 1, 0, 0, Y_TAG, 0, 1, 0, Z_TAG, 0, 0, 0, 1))
    return cut


def make_mark():
    """The mark's inlay: exactly fills the engraving, flush with the fin."""
    return nfc_mark().common(box(W - NFC_MARK[0], W, Y_TAG - 30, Y_TAG + 30, Z_TAG - 30, Z_TAG + 30))


def make_cap(reader):
    """Plug for the reader's slot: flush with the fin's top, holding the board down. Prints
    as it stands."""
    g = CAP_GAP
    _, zt = board_z(reader)
    hw = SLOT_W / 2
    cap = box(SLOT_X[0] + g, SLOT_X[1] - g, Y_C - hw + g, Y_C + hw - g, zt, FIN_TOP)
    for sgn in (-1, 1):                                # round the ribs
        cap = cap.cut(box(SLOT_X[0], X_BACK + g, Y_C + sgn * hw, Y_C + sgn * (hw - RIB_W - g), zt - 1, FIN_TOP + 1))
    # Grip notch at the back end, on the outside.
    return cap.cut(box(SLOT_X[1] - 2.0, SLOT_X[1], Y_C + hw - 8, Y_C + hw + 1, FIN_TOP - 2.5, FIN_TOP + 1))


def roller_span():
    """The rails' inner faces, between which the rollers turn."""
    return X_CUT + PLAT_INSET + RAIL_T, W - T_FIN


def roller_ends():
    """The roller's ends: at each, a washer between it and the bearing, whose inner face is
    0.1 inside the rail's face."""
    xa, xb = roller_span()
    g = WASHER[2] - 0.1
    return xa + g, xb - g


def roller_ys():
    return Y_C - ROLLER_Y, Y_C + ROLLER_Y


def collar_x():
    """The collar's foot on the roller, x from and to: inside the spool's reader-side flange."""
    x1 = W - T_FIN - FLANGE_GAP - FLANGE_T - CAPTURE_CLR
    return x1 - 2 * COLLAR_H - 1.0, x1


def u_slot(x0, x1, y, r, top):
    """A seat of radius r round the rollers' axis from x0 to x1, open at the top."""
    xa, xb = sorted((x0, x1))
    return Part.makeCylinder(r, xb - xa, V(xa, y, Z_ROLL), V(1, 0, 0)).fuse(box(xa, xb, y - r, y + r, Z_ROLL, top))


def make_roller():
    """A roller standing on its end, as printed, its reader end up: one piece, flat ends
    chamfered END_CHAMFER on both edges, an M5 pilot hole in each end, and the capture's
    collar near the top."""
    x0, x1 = roller_ends()
    length = x1 - x0
    depth = SCREW[0] - BEARING[2] - WASHER[2] + 1.5
    tube = Part.makeCylinder(R_ROLL, length)
    c0, c1 = (x - x0 for x in collar_x())
    h = COLLAR_H
    collar = Part.makeCone(R_ROLL, R_ROLL + h, h, V(0, 0, c0)).fuse(
        Part.makeCylinder(R_ROLL + h, c1 - c0 - 2 * h, V(0, 0, c0 + h))).fuse(
        Part.makeCone(R_ROLL + h, R_ROLL, h, V(0, 0, c1 - h)))
    tube = tube.fuse(collar)
    for z0 in (-0.01, length - depth):
        tube = tube.cut(Part.makeCylinder(M5_PILOT / 2, depth + 0.01, V(0, 0, z0)))
    tube = tube.removeSplitter()
    ends = [e for e in tube.Edges if e.Curve.__class__.__name__ == "Circle"
            and min(abs(e.Curve.Center.z), abs(e.Curve.Center.z - length)) < 1e-6
            and min(abs(e.Curve.Radius - R_ROLL), abs(e.Curve.Radius - M5_PILOT / 2)) < 1e-6]
    return tube.makeChamfer(END_CHAMFER, ends)


def bought(path, fallback):
    """A bought part's model from parts/, or a plain stand-in when it isn't there."""
    return Part.read(path) if os.path.exists(path) else fallback()


def bearing_model():
    """A 605ZZ, axis along x, centred at the origin: from parts/605ZZ.step, or built from its
    dimensions, an inner and an outer ring and a shield set in from each face."""
    od, bore, width = BEARING

    def ring(r0, r1, x0, x1):
        return Part.makeCylinder(r1, x1 - x0, V(x0, 0, 0), V(1, 0, 0)).cut(
            Part.makeCylinder(r0, x1 - x0 + 2, V(x0 - 1, 0, 0), V(1, 0, 0)))

    def built():
        h = width / 2
        rin, rout = bore / 2 + 0.24 * (od - bore) / 2, od / 2 - 0.27 * (od - bore) / 2   # the rings' shoulders
        parts = [ring(bore / 2, rin, -h, h), ring(rout, od / 2, -h, h)]
        for x in (-h + 0.2, h - 0.5):
            parts.append(ring(rin + 0.15, rout + 0.1, x, x + 0.3))                      # the shields
        return Part.makeCompound(parts)

    return bought(BEARING_STEP, built)


def screw_model(sgn):
    """An M5 screw on the rollers' axis, its head's underside at x = 0 and its shank toward
    sgn along x. The model's shank, down its z, is carried on to SCREW's length."""
    length, dk, k = SCREW
    s = bought(SCREW_STEP, lambda: Part.makeCylinder(dk / 2, k).fuse(Part.makeCylinder(2.5, length, V(0, 0, -length))))
    short = s.BoundBox.ZMin + length
    if short > 0.01:
        s = s.fuse(Part.makeCylinder(2.4, short + 0.5, V(0, 0, -length)))
    s.rotate(V(0, 0, 0), V(0, 1, 0), -90 * sgn)
    return s


def washer_model(sgn):
    """An M5 washer on the rollers' axis, from x = 0 a thickness toward sgn."""
    d, hole, t = WASHER
    s = bought(WASHER_STEP, lambda: Part.makeCylinder(d / 2, t).cut(Part.makeCylinder(hole / 2, t + 2, V(0, 0, -1))))
    s.rotate(V(0, 0, 0), V(0, 1, 0), 90 * sgn)          # the model lies in z = 0..t
    return s


def hex_socket(across, depth, centre, axis):
    """A hex key's socket, `depth` deep from `centre` along `axis`."""
    r = across / math.sqrt(3)
    pts = [V(r * math.cos(math.radians(60 * i)), r * math.sin(math.radians(60 * i)), 0) for i in range(7)]
    sock = Part.Face(Part.makePolygon(pts)).extrude(V(0, 0, depth))
    sock.Placement = App.Placement(centre, App.Rotation(V(0, 0, 1), axis))
    return sock


def bezel_screws():
    """The M3 x 10 countersunk screws through the bezel into the base's columns, in place,
    as (head, screw): the head seats in the countersink, where it is as wide as the head."""
    length, dk, k = M3_CSK
    seat = -(CSK_D - dk) / 2 + 0.05          # the head's top, in the bezel's frame (into the case is -z)
    out = []
    for s in SCREW_S:
        for u in (SCREW_X, W - SCREW_X):
            head = Part.makeCone(dk / 2, dk / 2 - k, k, V(u, s, seat), V(0, 0, -1))
            shank = Part.makeCylinder(1.5, length - k, V(u, s, seat - k), V(0, 0, -1))
            screw = head.fuse(shank).cut(hex_socket(2.0, 1.2, V(u, s, seat - 1.2), V(0, 0, 1)))
            out.append((on_plate(head), on_plate(screw.removeSplitter())))
    return out


def board_screws():
    """The M3 x 6 socket heads through the board into the bezel's bosses, from behind, in
    place, as (head, screw)."""
    length, dk, k = M3_SOCKET
    wb = PCB_W_FRONT + PCB_T                 # the PCB's back
    out = []
    for a, b in PCB_HOLES:
        u, s = board_uv(a, b)
        head = Part.makeCylinder(dk / 2, k, V(u, s, -wb - k))
        shank = Part.makeCylinder(1.5, length, V(u, s, -wb))
        screw = head.fuse(shank).cut(hex_socket(2.5, 1.5, V(u, s, -wb - k), V(0, 0, 1)))
        out.append((on_plate(head), on_plate(screw.removeSplitter())))
    return out


def roller_set(y, lift=0.0, spread=0.0):
    """One roller at y in place: its tube, and at each end a washer, a bearing and a screw.
    For the exploded view, lift raises the set and spread draws each end's parts out along
    the axis, in the order they go on: the washer, then the bearing, then the screw."""
    xa, xb = roller_span()
    x0, x1 = roller_ends()
    od, bore, width = BEARING
    z = Z_ROLL + lift
    tube = make_roller()
    tube.transformShape(App.Matrix(0, 0, 1, x0, 0, 1, 0, y, -1, 0, 0, z, 0, 0, 0, 1))
    parts = {"bearings": [], "washers": [], "screws": []}
    for face, out in ((xa, -1), (xb, 1)):               # out: away from the roller
        b = bearing_model()
        b.translate(V(face + out * (0.1 + width / 2 + 2 * spread), y, z))
        parts["bearings"].append(b)
        wsh = washer_model(-out)
        wsh.translate(V(face + out * (0.1 + spread), y, z))
        parts["washers"].append(wsh)
        s = screw_model(-out)
        s.translate(V(face + out * (0.1 + width + 3 * spread), y, z))
        parts["screws"].append(s)
    return tube, parts


def rollers_model():
    """Both rollers, with their bearings, washers and screws, in place."""
    shapes = []
    for y in roller_ys():
        tube, parts = roller_set(y)
        shapes += [tube] + parts["bearings"] + parts["washers"] + parts["screws"]
    return Part.makeCompound(shapes)


def cell_model():
    """The bar, its holes, and the potting over its gauges, which wraps it all round."""
    yf = CELL_Y - CELL_L / 2                  # the fixed end, where the cable leaves
    cell = box(X_C - CELL_S / 2, X_C + CELL_S / 2, yf, yf + CELL_L, Z_CELL0, Z_PB)
    g = (POTTING[0] - CELL_S) / 2
    cell = cell.fuse(box(X_C - POTTING[0] / 2, X_C + POTTING[0] / 2, yf + POTTING[1], yf + POTTING[2], Z_CELL0 - g, Z_PB + g))
    for h in CELL_HOLES:
        for y, d in ((yf + h, 3.4), (yf + CELL_L - h, 3.7)):
            cell = cell.cut(Part.makeCylinder(d / 2, CELL_S + 2, V(X_C, y, Z_CELL0 - 1)))
    return cell


def reader_model(reader):
    """The board, everything on its component side, the header's solder joints behind it,
    and its plugs below it."""
    r = READERS[reader]
    zb, zt = board_z(reader)
    a, pins = r["header"]
    yh = Y_C + r["w"] / 2 - a                 # seen from outside, header down, the left edge is at +y
    half = (pins - 1) * 2.54 / 2
    xf = X_BACK + r["t"]
    board = box(X_BACK, xf, Y_C - r["w"] / 2, Y_C + r["w"] / 2, zb, zt)
    board = board.fuse(box(xf, xf + r["front"], Y_C - r["w"] / 2, Y_C + r["w"] / 2, zb, zt))
    board = board.fuse(box(SLOT_X[0] + 0.3, X_BACK, yh - half - 1.0, yh + half + 1.0, zb, zb + 3.0))
    return board.fuse(box(xf, xf + 2.6, yh - half - 1.27, yh + half + 1.27, zb - PLUG_L, zb))


def ams_model():
    """The AMS 2 Pro in the notch, for the fit check and the pictures (see AMS)."""
    a = AMS
    x1, y0 = X_CUT - AMS_GAP, Y_NOTCH + AMS_GAP
    x0, y1 = x1 - a["w"], y0 + a["d"]
    shell = rrect_prism(x0, x1, y0, y1, a["base_h"], a["shell_top"], a["corner_r"])
    foot = box(x0 + a["base_in_side"], x1 - a["base_in_side"], y0 + a["base_in_front"], y1 - a["base_in_back"], 0, a["base_h"])
    dome = Part.makeCylinder(a["dome_r"], a["w"] - 6, V(x0 + 3, y0 + a["dome_y"], a["shell_top"]), V(1, 0, 0)).common(
        box(x0, x1, y0, y1, a["shell_top"], a["h"]))
    return shell.fuse(foot).fuse(dome)


def spool_model(gap=FLANGE_GAP):
    """A 200 mm spool resting in the cradle, its right flange against the fin."""
    x1 = W - T_FIN - gap
    flange = lambda x: Part.makeCylinder(SPOOL_R, 4.0, V(x, Y_C, SPOOL_Z), V(1, 0, 0))
    core = Part.makeCylinder(50.0, SPOOL_W - 8, V(x1 - SPOOL_W + 4, Y_C, SPOOL_Z), V(1, 0, 0))
    hole = Part.makeCylinder(27.5, SPOOL_W + 2, V(x1 - SPOOL_W - 1, Y_C, SPOOL_Z), V(1, 0, 0))
    spool = flange(x1 - SPOOL_W).fuse(flange(x1 - 4)).fuse(core).cut(hole)
    spool.translate(V(0, 0, 0.02))     # resting on, not in, the cradle
    return spool


# --- Exploded view ---------------------------------------------------------------------------
EXPLODE = dict(cell=15.0, platform=70.0, rollers=75.0, spread=12.0, reader=30.0, cap=25.0, board=60.0, bezel=110.0,
               bezel_screws=25.0, board_screws=28.0)   # in front of the bezel, behind the board


def guide(p0, p1, d=1.4, dash=5.0, gap=3.0):
    """A dashed line from p0 to p1, to draw a part's way in."""
    v = p1.sub(p0)
    length = v.Length
    n = v * (1.0 / length) if length else v              # Vector.normalize() works in place
    dashes, t = [], 0.0
    while t < length:
        seg = min(dash, length - t)
        dashes.append(Part.makeCylinder(d / 2, seg, p0.add(n * t), n))
        t += dash + gap
    return Part.makeCompound(dashes)


def explode_heights(reader):
    """How far the platform, the rollers, the reader and the cap rise in the exploded view."""
    e = EXPLODE
    zp = e["platform"]
    zb, _ = board_z(reader)
    zreader = zp + (FIN_TOP - zb) + e["reader"]       # its foot clear above the fin
    return zp, zp + e["rollers"], zreader, zreader + e["cap"]   # the cap above the reader


def pieces(reader, built):
    """The station's parts one by one, as (kind, the part in place, its move in the exploded
    view), in the build's frame: each roller, bearing, washer and screw on its own. The
    rollers come apart in full: at each end the washer, the bearing and the screw drawn out
    along the axis, and the set above the seats its bearings drop into. built: the base, the
    bezel, the platform, the cap and the mark, made already."""
    e = EXPLODE
    out = PLATE_PL.Rotation.multVec(V(0, 0, 1))          # out of the screen's face
    up = lambda dz: V(0, 0, dz)
    zp, zr, zreader, zcap = explode_heights(reader)
    held = board_parts()
    found = [("base", built["base"], V(0, 0, 0)), ("bezel", built["bezel"], out * e["bezel"]),
             ("pcb", held["PCB"], out * e["board"]), ("glass", held["glass"], out * e["board"]),
             ("cell", cell_model(), up(e["cell"])), ("platform", built["platform"], up(zp)),
             ("mark", built["mark"], up(zp)), ("reader", reader_model(reader), up(zreader)),
             ("cap", built["cap"], up(zcap))]
    found += [("fasteners", screw, out * (e["bezel"] + e["bezel_screws"])) for _, screw in bezel_screws()]
    found += [("fasteners", screw, out * (e["board"] - e["board_screws"])) for _, screw in board_screws()]
    for y in roller_ys():
        tube, kit = roller_set(y)
        apart, kit_apart = roller_set(y, lift=zr, spread=e["spread"])
        found.append(("rollers", tube, apart.BoundBox.Center - tube.BoundBox.Center))
        for k in kit:
            found += [(k, a, b.BoundBox.Center - a.BoundBox.Center) for a, b in zip(kit[k], kit_apart[k])]
    return found


def exploded(reader, built):
    """The station taken apart the way it goes together, as {name: shape} in the build's
    frame, and the guide lines that show each part's way in (see pieces())."""
    e = EXPLODE
    out = PLATE_PL.Rotation.multVec(V(0, 0, 1))
    zp, zr, zreader, zcap = explode_heights(reader)
    zb, zt = board_z(reader)
    parts = {}
    for kind, shape, move in pieces(reader, built):
        s = shape.copy()
        s.translate(move)
        parts.setdefault(kind, []).append(s)
    parts = {k: v[0] if len(v) == 1 else Part.makeCompound(v) for k, v in parts.items()}
    xa, xb = roller_span()
    width = BEARING[2]
    lines = []
    for y in roller_ys():
        z = Z_ROLL + zr
        reach = 0.1 + width + 3 * e["spread"] + SCREW[2] + 4
        lines.append(guide(V(xa - reach, y, z), V(xb + reach, y, z)))                 # along the axis
        for x in (xa - 0.1 - width / 2, xb + 0.1 + width / 2):                        # down into the bearings' seats
            lines.append(guide(V(x, y, z - BEARING[0] / 2), V(x, y, Z_ROLL + zp)))
    cx = (SLOT_X[0] + SLOT_X[1]) / 2
    lines.append(guide(V(cx, Y_C, zt + zcap), V(cx, Y_C, zb + zp)))                    # the reader and cap into the slot
    lines.append(guide(V(X_C, CELL_Y + CELL_L / 2 - sum(CELL_HOLES) / 2, Z_PB + e["cell"]),
                       V(X_C, CELL_Y + CELL_L / 2 - sum(CELL_HOLES) / 2, Z_PB + zp)))      # the platform onto the cell
    screen = []
    for s in SCREW_S:                                       # the bezel onto the pod, and its screws into the columns
        for u in (SCREW_X, W - SCREW_X):
            p = PLATE_PL.multVec(V(u, s, 0))
            screen.append(guide(p, p.add(out * (e["bezel"] + e["bezel_screws"] - M3_CSK[0]))))
    k = M3_SOCKET[2]
    for a, b in PCB_HOLES:                                  # the board's screws through it into the bosses
        u, s = board_uv(a, b)
        boss = PLATE_PL.multVec(V(u, s, -PCB_W_FRONT)).add(out * e["bezel"])
        head = PLATE_PL.multVec(V(u, s, -(PCB_W_FRONT + PCB_T + k))).add(out * (e["board"] - e["board_screws"]))
        screen.append(guide(head.add(out * -4.0), boss))
    parts["guides"] = Part.makeCompound(lines)
    parts["guides_screen"] = Part.makeCompound(screen)
    return parts


# --- Checks and export ----------------------------------------------------------------------
def overlap(a, b):
    return a.common(b).Volume if a.BoundBox.intersect(b.BoundBox) else 0.0


def check(case, readers):
    """case: the base, the bezel and the RF mark; readers: each reader's platform and cap."""
    ok = True
    held = dict(board_parts())
    held["load cell"] = cell_model()
    held["spool"] = spool_model()
    held["AMS 2 Pro"] = ams_model()
    held["rollers"] = rollers_model()
    held["bezel screws' heads"] = Part.makeCompound([h for h, _ in bezel_screws()])
    held["board screws' heads"] = Part.makeCompound([h for h, _ in board_screws()])
    first = next(iter(readers))
    for reader, (platform, cap) in readers.items():
        parts = dict(case, platform=platform, **{"fin cap": cap})
        tested = held if reader == first else {}       # the rest don't depend on the reader
        for hname, h in list(tested.items()) + [(f"{reader} reader", reader_model(reader))]:
            for pname, p in parts.items():
                v = overlap(h, p)
                if v > 1e-3:
                    print(f"[{hname} vs {pname}] INTERFERENCE {v:.3f} mm3")
                    ok = False
        if reader == first:
            for ename, e in access_envelopes().items():
                for pname, p in parts.items():
                    v = overlap(e, p)
                    if v > 1e-3:
                        bb = e.common(p).BoundBox
                        print(f"[access] {ename} blocked by {pname}: {v:.3f} mm3 at x {bb.XMin:.1f}..{bb.XMax:.1f}, "
                              f"y {bb.YMin:.1f}..{bb.YMax:.1f}, z {bb.ZMin:.1f}..{bb.ZMax:.1f}")
                        ok = False
        pairs = [("base", "bezel"), ("base", "platform"), ("bezel", "platform"), ("platform", "fin cap"), ("platform", "RF mark")]
        for a, b in pairs if reader == first else pairs[3:4]:
            v = overlap(parts[a], parts[b])
            print(f"{reader}: {a} vs {b} overlap: {v:.4f} mm3")
            ok &= v < 1e-3
        zb, zt = board_z(reader)
        ledge = (SLOT_W - pocket_w(reader)) / 2 - SLOT_CLR
        print(f"{reader}: board z {zb:.1f}..{zt:.1f}, antenna {SPOOL_Z - Z_TAG:.1f} mm under the spool's axis and "
              f"{FIN_IN + STANDOFF + READERS[reader]['t']:.1f} mm behind the fin's face; on {ledge:.1f} mm ledges, cap {FIN_TOP - zt:.1f} mm")
        ok &= ledge >= 1.0
    print("held parts and access:", "all clear" if ok else "see above")
    platform = readers[first][0]
    gaps = {
        "platform to base (must float)": platform.distToShape(case["base"])[0],
        "platform to bezel": platform.distToShape(case["bezel"])[0],
        "spool to base": held["spool"].distToShape(case["base"])[0],
        "spool to bezel": held["spool"].distToShape(case["bezel"])[0],
        "spool to cradle": held["spool"].distToShape(platform)[0],
        "spool to rollers (resting on them)": held["spool"].distToShape(held["rollers"])[0],
        "rollers to platform": held["rollers"].distToShape(platform)[0],
        "PCB to base": held["PCB"].distToShape(case["base"])[0],
        "parts behind the PCB to base": held["parts behind the PCB"].distToShape(case["base"])[0],
        "AMS 2 Pro to base": held["AMS 2 Pro"].distToShape(case["base"])[0],
        "AMS 2 Pro to bezel": held["AMS 2 Pro"].distToShape(case["bezel"])[0],
        "AMS 2 Pro to platform (must not touch the scale)": held["AMS 2 Pro"].distToShape(platform)[0],
        "load cell to base": held["load cell"].distToShape(case["base"])[0],
        "load cell to platform": held["load cell"].distToShape(platform)[0],
    }
    for k, v in gaps.items():
        print(f"gap, {k}: {v:.2f} mm")
    ok &= gaps["platform to base (must float)"] >= GAP_SIDE - 0.01
    ok &= gaps["spool to rollers (resting on them)"] < 0.05 and gaps["spool to cradle"] > 0.25   # 0.5 to the fin
    # The widest spool's far flange must land on the rollers too.
    xa, _ = roller_span()
    x0 = roller_ends()[0] + END_CHAMFER
    flange = W - T_FIN - FLANGE_GAP - SPOOL_W_MAX
    on = flange + FLANGE_T - x0
    print(f"a {SPOOL_W_MAX:.0f} mm spool's far flange on the rollers: {min(on, FLANGE_T):.1f} of {FLANGE_T:.0f} mm, "
          f"{flange - xa:.1f} mm from the rail; spool lifted {SPOOL_Z - SPOOL_R - Z_CB:.1f} mm off the cradle, "
          f"load cell {CELL_Y - Y_C:+.1f} mm from the spool's axis")
    ok &= on >= FLANGE_T - 0.01 and flange > xa
    room = collar_x()[1] - (W - T_FIN - FLANGE_GAP - FLANGE_T)
    print(f"the reader-side flange runs between the fin and the collars, {-room:.1f} mm clear of the collars")
    ok &= room < 0 and room > -(CAPTURE_CLR + 0.01)
    ok &= gaps["AMS 2 Pro to platform (must not touch the scale)"] >= AMS_GAP + PLAT_INSET - 0.01
    # The cell may touch the platform only at its free end's pad: nothing of the platform
    # within CELL_CLEAR above its middle and fixed end.
    yf = CELL_Y + CELL_L / 2 - CELL_PAD
    above = box(X_C - CELL_S / 2, X_C + CELL_S / 2, CELL_Y - CELL_L / 2, yf, Z_PB, Z_PB + CELL_CLEAR - 0.01)
    v = overlap(above, platform)
    print(f"{CELL_CLEAR} mm over the load cell's middle and fixed end: " + ("clear" if v < 1e-3 else f"BLOCKED {v:.3f} mm3"))
    ok &= v < 1e-3
    print(f"crown {CROWN_R:.2f} mm, its roof {roof_room(CROWN_R, behind_face()):.2f} mm past the room the board, its plugs "
          f"and the glass need; the board {BOARD_S0:.0f} mm up the face")
    print(f"base {W:.1f} x {Y_END:.1f} mm on a {BED:.0f} mm bed: " + ("fits" if Y_END <= BED else "TOO LONG"))
    ok &= Y_END <= BED
    return ok


def tight(shape):
    """The shape's own extent: BoundBox can be loose round a fillet's surfaces, which would
    lift a part off the bed."""
    bb = App.BoundBox()
    for p in shape.tessellate(0.01)[0]:       # optimalBoundingBox(True, ...) crashes FreeCAD 0.21
        bb.add(p)
    return bb


def placed(shape):
    """The build's frame to the station's: mirrored for the left-hand station."""
    return shape.mirror(V(W / 2, 0, 0), V(1, 0, 0)) if SIDE == "left" else shape


def write_3mf(path, parts, name):
    """One 3MF object made of several parts (Bambu Studio / Orca assign a
    filament per part)."""
    import zipfile
    from xml.sax.saxutils import quoteattr
    objs = []
    for i, (pname, shape) in enumerate(parts, start=1):
        mesh = MeshPart.meshFromShape(Shape=shape, LinearDeflection=0.01, AngularDeflection=0.2, Relative=False)
        pts, tris = mesh.Topology
        v = "".join(f'<vertex x="{p.x:.4f}" y="{p.y:.4f}" z="{p.z:.4f}"/>' for p in pts)
        t = "".join(f'<triangle v1="{a}" v2="{b}" v3="{c}"/>' for a, b, c in tris)
        objs.append(f'<object id="{i}" type="model" name={quoteattr(pname)}><mesh><vertices>{v}</vertices>'
                    f'<triangles>{t}</triangles></mesh></object>')
    comps = "".join(f'<component objectid="{i}"/>' for i in range(1, len(parts) + 1))
    top = len(parts) + 1
    model = ('<?xml version="1.0" encoding="UTF-8"?>'
             '<model unit="millimeter" xml:lang="en-US" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">'
             f'<resources>{"".join(objs)}<object id="{top}" type="model" name={quoteattr(name)}>'
             f'<components>{comps}</components></object></resources>'
             f'<build><item objectid="{top}"/></build></model>')
    ct = ('<?xml version="1.0" encoding="UTF-8"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
          '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
          '<Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/></Types>')
    rels = ('<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
            '<Relationship Target="/3D/3dmodel.model" Id="rel0" '
            'Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>')
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", ct)
        z.writestr("_rels/.rels", rels)
        z.writestr("3D/3dmodel.model", model)


def export_two_colour(platform, mark, reader):
    """Platform plus the mark's inlay in the platform's print orientation, sharing one shift.
    Returns the matrix from the mark's STL back to the station (see export())."""
    import Import
    shapes = [platform.copy(), mark.copy()]
    bb = tight(shapes[0])
    back = App.Matrix()
    back.move(V(bb.XMin, bb.YMin, bb.ZMin))
    for s in shapes:
        s.translate(V(-bb.XMin, -bb.YMin, -bb.ZMin))
    os.makedirs(OUT, exist_ok=True)
    name = f"spool_station_platform_{reader}_2colour"
    doc = App.newDocument("platform2c")
    o1 = doc.addObject("Part::Feature", "Platform")
    o1.Label, o1.Shape = "Platform", shapes[0]
    o2 = doc.addObject("Part::Feature", "RF_mark")
    o2.Label, o2.Shape = "RF mark", shapes[1]
    Import.export([o1, o2], os.path.join(OUT, name + ".step"))
    MeshPart.meshFromShape(Shape=shapes[1], LinearDeflection=0.01, AngularDeflection=0.2, Relative=False).write(
        os.path.join(OUT, "spool_station_platform_mark.stl"))
    write_3mf(os.path.join(OUT, name + ".3mf"), [("Platform", shapes[0]), ("RF mark", shapes[1])],
              f"Spool station platform, {reader}")
    App.closeDocument(doc.Name)
    mb, pb = tight(shapes[1]), tight(shapes[0])
    print(f"RF mark: {len(shapes[1].Solids)} bodies, {shapes[1].Volume:.1f} mm3, "
          f"x {mb.XMin:.2f}..{mb.XMax:.2f} in print orientation (platform x {pb.XMin:.2f}..{pb.XMax:.2f})")
    return back


def export(name, shape, placement=None):
    """The part as it prints, on the bed at the origin. Returns the matrix from its STL back
    to `shape`'s frame, for toolpaths.py, which puts the sliced part where it sits."""
    s = shape.copy()
    m = placement.toMatrix() if placement is not None else App.Matrix()
    s.transformShape(m)
    bb = tight(s)
    back = App.Matrix()
    back.move(V(bb.XMin, bb.YMin, bb.ZMin))
    s.translate(V(-bb.XMin, -bb.YMin, -bb.ZMin))
    os.makedirs(OUT, exist_ok=True)
    s.exportStep(os.path.join(OUT, name + ".step"))
    MeshPart.meshFromShape(Shape=s, LinearDeflection=0.02, AngularDeflection=0.25, Relative=False).write(
        os.path.join(OUT, name + ".stl"))
    bb = tight(s)
    print(f"{name}: {bb.XLength:.1f} x {bb.YLength:.1f} x {bb.ZLength:.1f} mm, "
          f"volume {s.Volume / 1000:.1f} cm3, valid {s.isValid()}, solids {len(s.Solids)}")
    return m.inverse().multiply(back)


def mesh_folder(d):
    """A folder for the pictures' meshes, without the sliced parts of an earlier build."""
    import shutil
    shutil.rmtree(os.path.join(d, "toolpaths"), ignore_errors=True)
    os.makedirs(d, exist_ok=True)
    return d


def write_mesh(path, shape, deflection=0.05, angular=0.4):
    MeshPart.meshFromShape(Shape=placed(shape), LinearDeflection=deflection, AngularDeflection=angular,
                           Relative=False).write(path)


base, bezel, mark = make_base(), make_bezel(), make_mark()
readers = {r: (make_platform(r), make_cap(r)) for r in (READERS if READER_CHOICE == "both" else (READER_CHOICE,))}
print(f"{B['title']}, {SIDE}-hand station, screen {RAKE:g} degrees from vertical: bezel {S_TOP:.1f} mm, its border "
      f"{window_s()[0]:.1f} mm below and above the window; pod top {CROWN_C[1] + CROWN_R:.1f}")
print(f"footprint {W:.1f} x {Y_END:.1f} mm, notch for the AMS left of x {X_CUT:.1f} behind y {Y_NOTCH:.1f}; cradle bottom {Z_CB:.1f}; "
      f"fin top {FIN_TOP:.1f}; spool axis y {Y_C:.1f} z {SPOOL_Z:.1f}; reader's antenna z {Z_TAG:.1f}")
ok = check({"base": base, "bezel": bezel, "RF mark": mark}, readers)

flip = App.Placement(V(0, 0, 0), App.Rotation(V(1, 0, 0), 180))
frames = {}                                              # each STL back to the station (toolpaths.py)
frames["base"] = export("spool_station_base", placed(base))
frames["bezel"] = export("spool_station_bezel", placed(bezel), flip.multiply(PLATE_PL.inverse()))   # face down
for reader, (platform, cap) in readers.items():
    frames["platform_" + reader] = export(f"spool_station_platform_{reader}", placed(platform))   # single colour: the mark stays engraved
    frames["mark"] = export_two_colour(placed(platform), placed(mark), reader)
    frames["cap_" + reader] = export(f"spool_station_fin_cap_{reader}", placed(cap))              # as it stands
frames["roller"] = export("spool_station_roller", make_roller())            # standing on its end; print two
with open(os.path.join(OUT, "build.json"), "w") as f:    # what these files are
    json.dump({"board": BOARD, "side": SIDE, "rake": RAKE, "readers": list(readers), "checks": "PASS" if ok else "FAIL",
               "footprint": [round(W, 1), round(Y_END, 1)], "height": round(CROWN_C[1] + CROWN_R, 1)}, f, indent=1)

held = board_parts()
platform, cap = readers[READER]
fasteners = Part.makeCompound([screw for _, screw in bezel_screws() + board_screws()])
assembly = [base, bezel, platform, cap, mark, held["PCB"], held["glass"], cell_model(), reader_model(READER), rollers_model(),
            fasteners, spool_model()]
Part.Compound([placed(s) for s in assembly]).exportStep(os.path.join(OUT, "spool_station_assembly.step"))
# Meshes for the pictures (render_blender.py), in the station's frame, and what the cameras
# need to know, in the build's.
mesh_dir = mesh_folder(os.path.join(OUT, "mesh"))
for name, shape in (("base", base), ("bezel", bezel), ("platform", platform), ("cap", cap), ("mark", mark), ("pcb", held["PCB"]),
                    ("glass", held["glass"]), ("cell", cell_model()), ("reader", reader_model(READER)), ("spool", spool_model()),
                    ("ams", ams_model()), ("rollers", rollers_model()), ("fasteners", fasteners)):
    write_mesh(os.path.join(mesh_dir, name + ".stl"), shape)
xa, xb = roller_span()
view = {"board": BOARD, "side": SIDE, "rake": RAKE, "reader": READER, "W": W, "Y_C": Y_C, "Y_END": Y_END, "Z_TOP": Z_TOP,
        "FIN_TOP": FIN_TOP, "Z_PB": Z_PB, "X_BACK": X_BACK, "T_FIN": T_FIN, "X_C": X_C,
        "AMS_X": X_CUT - AMS_GAP - AMS["w"] / 2, "AMS_Y": Y_NOTCH + AMS_GAP + AMS["d"] / 2}
with open(os.path.join(mesh_dir, "view.json"), "w") as f:
    json.dump(view, f)
built = dict(base=base, bezel=bezel, platform=platform, cap=cap, mark=mark)
boom = exploded(READER, built)
Part.Compound([placed(s) for k, s in boom.items() if not k.startswith("guides")]).exportStep(os.path.join(OUT, "spool_station_exploded.step"))
boom_dir = mesh_folder(os.path.join(OUT, "mesh_exploded"))
for name, shape in boom.items():
    write_mesh(os.path.join(boom_dir, name + ".stl"), shape, angular=0.3)
with open(os.path.join(boom_dir, "view.json"), "w") as f:
    out = PLATE_PL.Rotation.multVec(V(0, 0, 1))
    screen = PLATE_PL.multVec(V(W / 2, BOARD_S0 + PCB_H / 2, -PCB_W_FRONT)).add(out * (EXPLODE["board"] / 2))
    json.dump(dict(view, SCREEN_C=[screen.x, screen.y, screen.z], EXPLODE_C=[W / 2 - 20, Y_C - 20, 110],
                   ROLLER_C=[(xa + xb) / 2 - 45, Y_C - ROLLER_Y, Z_ROLL + EXPLODE["platform"] + EXPLODE["rollers"] - 10]), f)


def flat(m):
    return [m.A11, m.A12, m.A13, m.A14, m.A21, m.A22, m.A23, m.A24, m.A31, m.A32, m.A33, m.A34, m.A41, m.A42, m.A43, m.A44]


# The printed parts for toolpaths.py, which renders them sliced, bead by bead: each one's STL,
# the matrix from it to the station, and its move in the exploded view, in the station's frame.
mirror = App.Matrix(-1, 0, 0, W, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1) if SIDE == "left" else App.Matrix()
moves = {}
for kind, _, move in pieces(READER, built):
    moves.setdefault(kind, []).append([-move.x if SIDE == "left" else move.x, move.y, move.z])
printed = [dict(kind="base", stl="spool_station_base.stl", matrix=flat(frames["base"]), move=moves["base"][0]),
           dict(kind="bezel", stl="spool_station_bezel.stl", matrix=flat(frames["bezel"]), move=moves["bezel"][0]),
           dict(kind="mark", stl="spool_station_platform_mark.stl", matrix=flat(frames["mark"]), move=moves["mark"][0])]
for r in readers:
    printed += [dict(kind="platform", reader=r, stl=f"spool_station_platform_{r}.stl", matrix=flat(frames["platform_" + r]),
                     move=moves["platform"][0]),
                dict(kind="cap", reader=r, stl=f"spool_station_fin_cap_{r}.stl", matrix=flat(frames["cap_" + r]), move=moves["cap"][0])]
x0 = roller_ends()[0]
for y, move in zip(roller_ys(), moves["rollers"]):
    tube = mirror.multiply(App.Matrix(0, 0, 1, x0, 0, 1, 0, y, -1, 0, 0, Z_ROLL, 0, 0, 0, 1)).multiply(frames["roller"])
    printed.append(dict(kind="rollers", stl="spool_station_roller.stl", matrix=flat(tube), move=move))
with open(os.path.join(mesh_dir, "print.json"), "w") as f:
    json.dump({"parts": printed}, f, indent=1)


def cut_set(d, cutter, reader, cut_x):
    """The pictures' meshes with everything in `cutter` cut away: all on the AMS's side of x =
    cut_x, in the build's frame."""
    platform, cap = readers[reader]
    mesh_folder(d)
    for name, shape in (("base", base), ("bezel", bezel), ("platform", platform), ("cap", cap), ("mark", mark),
                        ("pcb", held["PCB"]), ("glass", held["glass"]), ("cell", cell_model()), ("reader", reader_model(reader)),
                        ("spool", spool_model()), ("rollers", rollers_model()), ("fasteners", fasteners)):
        s = shape.cut(cutter)
        if s.Volume > 1e-6:
            write_mesh(os.path.join(d, name + ".stl"), s)
    with open(os.path.join(d, "view.json"), "w") as f:     # CUT: the plane, and the side kept, in the station's frame
        json.dump(dict(view, reader=reader, CUT=dict(x=W - cut_x, keep=-1) if SIDE == "left" else dict(x=cut_x, keep=1)), f)


if os.environ.get("STATION_CUTS") == "1":
    # Cut along the load cell's axis, everything on the AMS's side of it gone; and through
    # the fin at the reader's back, everything on the spool's side gone (one per reader).
    cut_set(os.path.join(OUT, "mesh_cut", "cell"), box(X_C - 1000, X_C, -500, 500, -500, 500), READER, X_C)
    for r in readers:
        cut_set(os.path.join(OUT, "mesh_cut", "fin_" + r), box(X_BACK - 0.5 - 1000, X_BACK - 0.5, -500, 500, -500, 500), r,
                X_BACK - 0.5)
if os.environ.get("STATION_PARTS") == "1":
    # Every part on its own, in place, and its move in the exploded view: for the 3D model
    # (make_glb.py), which plays the station coming apart.
    parts_dir = os.path.join(OUT, "mesh_parts")
    os.makedirs(parts_dir, exist_ok=True)
    moves = []
    for i, (kind, shape, move) in enumerate(pieces(READER, built)):
        name = f"{i:02d}_{kind}"
        write_mesh(os.path.join(parts_dir, name + ".stl"), shape, angular=0.3)
        moves.append({"name": name, "kind": kind, "move": [-move.x if SIDE == "left" else move.x, move.y, move.z]})
    with open(os.path.join(parts_dir, "parts.json"), "w") as f:
        json.dump({"view": view, "parts": moves}, f, indent=1)
print(f"wrote {OUT}")
print("checks:", "PASS" if ok else "FAIL")
