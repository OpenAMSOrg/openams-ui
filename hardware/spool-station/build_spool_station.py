"""Parametric printable spool station: the 5" ES3C50P display, a PN5180 reader and a
5 kg bar load cell under a spool cradle, to stand beside an AMS.

Run with FreeCAD's command-line interpreter (FreeCAD 0.21+):

    freecadcmd -c "exec(open('build_spool_station.py').read())"

It writes print-oriented STL and STEP files for each part, and an assembly STEP with
the board, load cell, reader and a 200 mm spool, into out/ (STATION_OUT overrides).
Every part is checked against simple models of what it holds before export.

Layout, seen from the front: the screen on a sloped face, a curved cradle behind it on
the load cell, and on the cradle's right a fin that holds the PN5180 against the
spool's flange, near its tag. The board is mounted rotated 180 degrees from LCDWiki's
drawing (the firmware's BOARD_ROTATED_180), so its USB-C and its P3..P6 headers face
right and up, toward the fin and the USB opening in the right wall.

Coordinates: x to the right, y away from the viewer, z up; the base's front-left
bottom corner is the origin. Bezel features are placed in the bezel's own frame:
u along x, s up the slope from its lower front edge, w into the case from its face.
"""

import math
import os

import FreeCAD as App
import Mesh
import MeshPart
import Part

V = App.Vector
HERE = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else os.getcwd()
OUT = os.environ.get("STATION_OUT", os.path.join(HERE, "out"))

# --- The 5" board: LCDWiki ES3C50P, 5inch_ESP32S3_Size.pdf V1.0 (+/-0.2) ---------------
# (a, b) from the installed board's bottom-left corner, seen from the front. The drawing's
# front view is turned 180 degrees: its right edge (3.45 to the holes) is our left.
PCB_W, PCB_H, PCB_T = 137.0, 84.0, 1.6
PCB_HOLES = [(3.45, 3.55), (133.48, 3.55), (3.45, 80.50), (133.48, 80.50)]   # 130.03 x 76.95
GLASS = (8.15, 4.05, 128.85, 79.95)      # LCM and touch glass, 120.7 x 75.9
GLASS_T = 4.2                             # touch 1.0 + LCM 3.2
FACE_TO_PCB = 5.70                        # glass front to the PCB's front face
VA = (13.86, 6.81, 123.06, 72.86)         # visible area, 109.2 x 66.05
BACK_PARTS = 6.45                         # tallest part behind the PCB
USB_B = 42.0                              # USB-C on the right edge, centred
USB_W = 1.65                              # receptacle centre behind the PCB's back face
HEADERS_A = [71.70, 86.56, 101.42, 114.50, 125.26]   # 1.25 mm headers on the top edge
SD_A = 53.40                              # microSD slot, top edge
TOUCH_FPC = (2.15, 6.55, 13.5, 12.15)     # touch flex beside the glass, bottom-left

# --- Bezel -------------------------------------------------------------------------
W = 156.0                 # case width; the board sits centred, 9.5 mm in from each side
SLOPE = 35.0              # screen face, degrees from horizontal
Z_F = 14.0                # height of the face's lower front edge
L_PLATE = 106.0           # face length up the slope
T_PLATE = 2.5
BOARD_S0 = 8.0            # board's bottom edge, up the slope from the face's lower edge
WIN_GROW = 0.3            # window over the visible area, per side
WIN_CHAMFER = 1.5         # 45 degrees round the window, on the face
BOSS_D = 5.8              # board bosses; 6.0 would touch the touch flex
BOSS_FLOAT = 0.05         # glass floats this far behind the face when the PCB is screwed down
M3_PILOT = 2.5            # thread-forming M3
M3_CLEAR = 3.4
CSK_D = 6.4               # M3 countersunk (ISO 10642) head, 90 degrees
RIM_T, RIM_H, RIM_GAP = 1.2, 3.0, 0.3   # glass-locating rim on the bezel's back

# --- Base ----------------------------------------------------------------------------
WALL = 2.5
FLOOR = 3.0
DECK = 2.5                # pod roof behind the face, under the cradle's arc
Y_POD = 100.0             # back of the screen pod
R_CORNER = 8.0            # vertical corners of base and platform
COL_W = 8.5               # bezel screw columns, inside each side wall
COL_Y_BOT = 14.0          # front columns run y 0..14
COL_Y_TOP = 78.0          # rear columns run y 78..back wall
SCREW_S = (5.0, 100.0)    # bezel screws, up the slope
PILOT_DEPTH = 10.0
FOOT_D, FOOT_DEPTH = 10.5, 1.0     # recesses for stick-on rubber feet

# --- Load cell: 5 kg bar, 80 x 12.7 x 12.7 (the user's; hole layout to be measured) ----
CELL_L, CELL_S = 80.0, 12.7
CELL_HOLES = (5.0, 20.0)  # from each end, on the centre line
CELL_PAD = 25.0           # mounting block length at each end
# Both ends take M4 or M5 (with a washer for M4), so the cell fits either way round.
FIXED_SCREW = (5.5, 9.5, 3.0)   # clearance, counterbore d, counterbore depth (from below)
FREE_SCREW = (5.5, 9.5, 4.0)    # clearance, counterbore d, plastic left under the head
SLOT = 1.5                # hole slots +/- along y, for other hole pitches
BOSS_H = 5.0              # spacer under the fixed end
CELL_CLEAR = 3.0          # gap over the cell's middle and fixed end

# --- Platform and cradle ------------------------------------------------------------------
GAP_SIDE = 1.5            # platform over the base walls (also the overload stop)
GAP_FRONT = 2.0           # platform to the pod's back wall
PLAT_MIN = 6.0            # platform thickness at the cradle's lowest point
R_CRADLE = 130.0          # cradle arc; a 200 mm spool touches only its bottom
SPOOL_R, SPOOL_W = 100.0, 67.0     # Bambu Lab 1 kg spool
PLAT_REAR = 60.0          # cradle behind the spool's contact; the base fits a 256 mm bed

# --- Beside an AMS 2 Pro -------------------------------------------------------------------
# The station stands on the AMS's right, its screen pod reaching across in front of the
# AMS's front-right corner: the cradle, and the base under it, end CRADLE_W from the fin,
# and the AMS stands in the notch. Its shape is from Bambu Lab's front and side product
# views, scaled to the listed 372 x 280 x 226 mm: flat front and right side; the shell on
# an 11.7 mm base set in 7 mm at the front and 22 mm at the sides; the lid's dome 50 mm
# back from the front. The vertical corner radius, about 10 mm, is an estimate; the
# notch's inner corner is sharp, so any radius fits.
CRADLE_W = 75.0           # cradle beside the fin; the AMS 2 Pro takes spools 50..68 mm wide
AMS_GAP = 0.8             # to the AMS's front and side
PLAT_INSET = 2.0          # platform's left edge in from the base's: the AMS meets the base, not the scale
AMS = dict(w=372.0, d=280.0, h=226.0, base_h=11.7, base_in_front=7.0, base_in_side=22.0,
           base_in_back=24.0, shell_top=120.6, corner_r=10.0, dome_r=105.0, dome_y=156.7)

# --- Reader fin ------------------------------------------------------------------------
# The PN5180 slides into a slot from the fin's top, antenna side against the spool-side
# wall, wires down a channel into the base; a plug closes the slot. Any board up to
# READER_L long fits; READER_H only sets the plug's length. To be measured.
READER_L, READER_H = 86.0, 42.0
TAG_R = 38.0              # Bambu tag's distance from the spool's axis
FIN_IN = 1.2              # spool-side wall over the antenna
FIN_SLOT = 6.5            # board, its parts and a foam pad
FIN_OUT = 2.5
FIN_END = 6.0             # end walls past the slot
FIN_R = 6.0               # fin top corners
CAP_MIN = 8.0             # plug length above the board
CAP_GAP = 0.15
CHANNEL = (5.0, 14.0)     # wire channel, x by y
# Contactless mark on the fin's outside, a flush inlay in a second colour: depth into the
# wall (three 0.4 mm lines, as it sits on a vertical face), stroke, arc radii.
NFC_MARK = (1.2, 1.6, (3.0, 6.5, 10.0, 13.5))

T_FIN = FIN_IN + FIN_SLOT + FIN_OUT

# --- Derived ------------------------------------------------------------------------
TH = math.radians(SLOPE)
Z_TOP = Z_F + L_PLATE * math.sin(TH)
PLATE_PL = App.Placement(V(0, 0, Z_F), App.Rotation(V(1, 0, 0), SLOPE))   # bezel frame -> case
U0 = (W - PCB_W) / 2      # board's left edge
X_CUT = W - T_FIN - CRADLE_W   # left edge of the base behind the pod; the AMS's right side


def p1_z(y):
    """The base's slope surface, where the bezel's back rests."""
    return Z_F - T_PLATE / math.cos(TH) + y * math.tan(TH)


def p1_y(z):
    return (z - Z_F + T_PLATE / math.cos(TH)) / math.tan(TH)


Z_CELL0 = BOSS_H + FLOOR                  # cell bottom (the boss stands on the floor)
Z_PB = Z_CELL0 + CELL_S                   # platform bottom = cell top at the free end
Z_RWALL = Z_PB - GAP_SIDE                 # base walls under the platform
Z_CB = Z_PB + PLAT_MIN                    # cradle bottom
SPOOL_Z = Z_CB + SPOOL_R
# The cradle's arc runs on over the pod and meets the face's top edge: no step at the
# screen's top. The spool stays inside that circle, so it clears the pod and the bezel.
Y_C = L_PLATE * math.cos(TH) + math.sqrt(R_CRADLE ** 2 - (Z_CB + R_CRADLE - Z_TOP) ** 2)
Y_END = Y_C + PLAT_REAR
Y_P0 = Y_POD + GAP_FRONT
X_C = W - T_FIN - SPOOL_W / 2             # load cell under the spool, which leans on the fin
Z_TAG = SPOOL_Z - TAG_R                   # reader centre: the tag at six o'clock
SLOT_X = (W - T_FIN + FIN_IN, W - FIN_OUT)
SLOT_BOT = Z_TAG - READER_H / 2
FIN_TOP = SLOT_BOT + READER_H + CAP_MIN
FIN_L = READER_L + 2 + 2 * FIN_END
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


def rrect_face(x0, y0, x1, y1, r, z=0.0):
    """Rounded rectangle in a plane z = const."""
    r = min(r, (x1 - x0) / 2 - 1e-3, (y1 - y0) / 2 - 1e-3)
    corners = [((x1 - r, y0 + r), -90), ((x1 - r, y1 - r), 0), ((x0 + r, y1 - r), 90), ((x0 + r, y0 + r), 180)]
    edges = []
    for i, ((cx, cy), a0) in enumerate(corners):
        pts = [V(cx + r * math.cos(math.radians(a)), cy + r * math.sin(math.radians(a)), z) for a in (a0, a0 + 45, a0 + 90)]
        edges.append(Part.Arc(*pts).toShape())
        (nx, ny), na0 = corners[(i + 1) % 4]
        edges.append(Part.LineSegment(pts[2], V(nx + r * math.cos(math.radians(na0)), ny + r * math.sin(math.radians(na0)), z)).toShape())
    return Part.Face(Part.Wire(edges))


def rrect_prism(x0, x1, y0, y1, z0, z1, r):
    return rrect_face(x0, y0, x1, y1, r, z0).extrude(V(0, 0, z1 - z0))


def outline(z0, z1, inset=0.0):
    """The base's footprint, shrunk by `inset`: the pod across the full width, and behind it
    only the cradle beside the fin. The AMS stands in the notch on the left."""
    r = R_CORNER - inset
    pod = rrect_prism(inset, W - inset, inset, Y_POD - inset, z0, z1, r)
    return pod.fuse(rrect_prism(X_CUT + inset, W - inset, inset, Y_END - inset, z0, z1, r)).removeSplitter()


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


def plate_frustum(u0, u1, s0, s1, grow, w_face, w_back):
    """Rectangular hole that widens by `grow` per side toward the face."""
    def rect(g, z):
        return Part.makePolygon([V(u0 - g, s0 - g, z), V(u1 + g, s0 - g, z), V(u1 + g, s1 + g, z),
                                 V(u0 - g, s1 + g, z), V(u0 - g, s0 - g, z)])
    return on_plate(Part.makeLoft([rect(0, -w_back), rect(grow, -w_face)], True))


def board_uv(a, b):
    return U0 + a, BOARD_S0 + b


# --- Base ----------------------------------------------------------------------------
def make_base():
    y1 = p1_y(Z_TOP)
    pod = yz_prism([(0, 0), (0, p1_z(0)), (y1, Z_TOP), (Y_POD, Z_TOP), (Y_POD, 0)], 0, W)
    rear = box(0, W, Y_POD - 1, Y_END, 0, Z_RWALL)
    base = pod.fuse(rear).common(outline(0, Z_TOP + 1))
    # Cavities: the pod (open where the slope is), and the rear tub. The pod's ceiling is a
    # 45-degree plane, z + y = k, touching the roof's inside (the arc grown by DECK), so it
    # prints without support and the roof is nowhere thinner than DECK.
    y_w = Y_POD - WALL
    k = Y_C + Z_CB + R_CRADLE - math.sqrt(2) * (R_CRADLE + DECK)
    base = base.cut(yz_prism([(WALL, FLOOR), (WALL, k - WALL), (y_w, k - y_w), (y_w, FLOOR)], WALL, W - WALL))
    base = base.cut(outline(FLOOR, Z_RWALL + 1, WALL).common(box(0, W, Y_POD, Y_END, FLOOR, Z_RWALL + 1)))
    # Columns for the bezel screws, clipped to the outside shape.
    outer = pod.common(outline(0, Z_TOP + 1))
    for x0, x1 in ((0, COL_W), (W - COL_W, W)):
        for y0, y1 in ((0, COL_Y_BOT), (COL_Y_TOP, Y_POD - WALL)):
            base = base.fuse(box(x0, x1, y0, y1, 0, Z_TOP).common(outer))
    base = base.cut(cradle_cut(R_CRADLE))
    for s in SCREW_S:
        for u in (COL_W / 2, W - COL_W / 2):
            base = base.cut(plate_cyl(u, s, T_PLATE - 0.01, T_PLATE + PILOT_DEPTH, M3_PILOT))
    # Pod back wall: wires and the load cell's cable pass under the platform, through
    # openings peaked at 45 degrees so their tops print without bridging.
    for x in (X_C - 25, X_C, X_C + 25):
        pts = [(x - 11, FLOOR), (x + 11, FLOOR), (x + 11, 8), (x, 19), (x - 11, 8)]
        base = base.cut(poly_face([(px, Y_POD - WALL - 1, pz) for px, pz in pts]).extrude(V(0, WALL + 2, 0)))
    # USB-C opening in the right wall, at the receptacle.
    u_usb, s_usb = board_uv(PCB_W, USB_B)
    w_usb = PCB_W_FRONT + PCB_T + USB_W
    base = base.cut(usb_opening(u_usb, s_usb, w_usb))
    # Load cell fixed end: a block on the floor, M5 from below.
    yf = Y_C - CELL_L / 2
    base = base.fuse(box(X_C - 10, X_C + 10, yf - 1, yf + CELL_PAD, 0, Z_CELL0))
    for h in CELL_HOLES:
        y = yf + h
        base = base.cut(slot_y(X_C, y, -1, Z_CELL0 + 1, FIXED_SCREW[0], SLOT))
        base = base.cut(slot_y(X_C, y, -1, FIXED_SCREW[2], FIXED_SCREW[1], SLOT))
    # NAU7802 pad: a low rim for foam tape, in the pod behind the board.
    nx, ny = 45.0, 80.0
    rim = box(nx - 16.2, nx + 16.2, ny - 13.2, ny + 13.2, FLOOR, FLOOR + 2).cut(
        box(nx - 15, nx + 15, ny - 12, ny + 12, FLOOR - 1, FLOOR + 3))
    base = base.fuse(rim)
    for x, y in ((12, 12), (W - 12, 12), (12, Y_POD - 12), (X_CUT + 12, Y_END - 12), (W - 12, Y_END - 12)):
        base = base.cut(cyl(x, y, -1, FOOT_DEPTH, FOOT_D))
    return base.removeSplitter()


def usb_opening(u, s, w, length=15.0, width=9.5):
    """Stadium through the right wall: long side up the slope, like the receptacle."""
    r = width / 2
    prof = Part.Face(Part.Wire([
        Part.LineSegment(V(0, s - length / 2 + r, -w - r), V(0, s + length / 2 - r, -w - r)).toShape(),
        Part.Arc(V(0, s + length / 2 - r, -w - r), V(0, s + length / 2, -w), V(0, s + length / 2 - r, -w + r)).toShape(),
        Part.LineSegment(V(0, s + length / 2 - r, -w + r), V(0, s - length / 2 + r, -w + r)).toShape(),
        Part.Arc(V(0, s - length / 2 + r, -w + r), V(0, s - length / 2, -w), V(0, s - length / 2 + r, -w - r)).toShape(),
    ]))
    prof.translate(V(u - 1, 0, 0))
    return on_plate(prof.extrude(V(W - u + 5, 0, 0)))


# --- Bezel -----------------------------------------------------------------------------
def make_bezel():
    y1 = p1_y(Z_TOP)
    plate = yz_prism([(0, Z_F), (L_PLATE * math.cos(TH), Z_TOP), (y1, Z_TOP), (0, p1_z(0))], 0, W)
    plate = plate.cut(cradle_cut(R_CRADLE))        # its top edge follows the cradle's arc
    # Window over the visible area, chamfered on the face.
    u0, s0 = board_uv(VA[0] - WIN_GROW, VA[1] - WIN_GROW)
    u1, s1 = board_uv(VA[2] + WIN_GROW, VA[3] + WIN_GROW)
    plate = plate.cut(plate_frustum(u0, u1, s0, s1, WIN_CHAMFER + 0.01, -0.01, WIN_CHAMFER))
    plate = plate.cut(plate_box(u0, u1, s0, s1, -1, T_PLATE + 1))
    # Glass-locating rim, open at the touch flex.
    g0u, g0s = board_uv(GLASS[0] - RIM_GAP, GLASS[1] - RIM_GAP)
    g1u, g1s = board_uv(GLASS[2] + RIM_GAP, GLASS[3] + RIM_GAP)
    rim = plate_box(g0u - RIM_T, g1u + RIM_T, g0s - RIM_T, g1s + RIM_T, T_PLATE - 0.01, T_PLATE + RIM_H).cut(
        plate_box(g0u, g1u, g0s, g1s, 0, T_PLATE + RIM_H + 1))
    fu0, fs0 = board_uv(TOUCH_FPC[0] - 0.5, TOUCH_FPC[1] - 0.5)
    fu1, fs1 = board_uv(TOUCH_FPC[2] + 0.5, TOUCH_FPC[3] + 0.5)
    rim = rim.cut(plate_box(fu0, fu1, fs0, fs1, 0, T_PLATE + RIM_H + 1))
    plate = plate.fuse(rim)
    # Board bosses: the PCB's front face rests on them; M3 x 6 from behind.
    for a, b in PCB_HOLES:
        u, s = board_uv(a, b)
        plate = plate.fuse(plate_cyl(u, s, T_PLATE - 0.01, PCB_W_FRONT, BOSS_D))
        plate = plate.cut(plate_cyl(u, s, PCB_W_FRONT - 6.0, PCB_W_FRONT + 0.1, M3_PILOT))
    # Countersunk M3 into the base's columns.
    for s in SCREW_S:
        for u in (COL_W / 2, W - COL_W / 2):
            plate = plate.cut(plate_cyl(u, s, -1, T_PLATE + 1, M3_CLEAR))
            depth = (CSK_D - M3_CLEAR) / 2
            cone = Part.makeCone(M3_CLEAR / 2, CSK_D / 2 + 0.5, depth + 0.5, V(u, s, -depth), V(0, 0, 1))
            plate = plate.cut(on_plate(cone))
    return plate.removeSplitter()


# --- Platform -------------------------------------------------------------------------------
def cradle_cut(r):
    """Everything inside the cradle circle of radius r."""
    return Part.makeCylinder(r, W + 20, V(-10, Y_C, Z_CB + R_CRADLE), V(1, 0, 0))


def make_platform():
    top = Z_CB + R_CRADLE
    foot = rrect_prism(X_CUT + PLAT_INSET, W, Y_P0 - R_CORNER, Y_END, Z_PB, top, R_CORNER)
    plat = foot.common(box(0, W, Y_P0, Y_END, Z_PB, top)).cut(cradle_cut(R_CRADLE))
    # Fin on the right, rounded at the top.
    y0, y1 = Y_C - FIN_L / 2, Y_C + FIN_L / 2
    prof = rrect_face(y0, Z_PB - 20, y1, FIN_TOP, FIN_R)       # in a z = 0 plane, (y, z) as (x, y)
    fin = prof.extrude(V(0, 0, T_FIN))
    fin.transformShape(App.Matrix(0, 0, 1, W - T_FIN, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1))
    fin = fin.common(box(W - T_FIN, W, y0, y1, Z_PB, FIN_TOP + 1))
    plat = plat.fuse(fin)
    # Solid, printed with sparse infill: hollows under the curved cradle left its skin as a
    # shallow overhang, and saved no plastic over 15 % infill.
    yf = Y_C + CELL_L / 2 - CELL_PAD                   # free end's pad starts here
    # Clearance over the cell's middle and fixed end.
    plat = plat.cut(box(X_C - CELL_S / 2 - 2, X_C + CELL_S / 2 + 2, Y_C - CELL_L / 2 - 2, yf, Z_PB - 1, Z_PB + CELL_CLEAR))
    # Free end: M4 from the top, counterbored into the cradle.
    for h in CELL_HOLES:
        y = Y_C + CELL_L / 2 - h
        plat = plat.cut(slot_y(X_C, y, Z_PB - 1, top, FREE_SCREW[0], SLOT))
        plat = plat.cut(slot_y(X_C, y, Z_PB + FREE_SCREW[2], top, FREE_SCREW[1], SLOT))
    # Reader slot from the fin's top, and the wire channel down into the base.
    plat = plat.cut(box(SLOT_X[0], SLOT_X[1], Y_C - READER_L / 2 - 1, Y_C + READER_L / 2 + 1, SLOT_BOT, FIN_TOP + 1))
    cx = (SLOT_X[0] + SLOT_X[1]) / 2
    plat = plat.cut(box(cx - CHANNEL[0] / 2, cx + CHANNEL[0] / 2, Y_C - CHANNEL[1] / 2, Y_C + CHANNEL[1] / 2, Z_PB - 1, SLOT_BOT + 0.1))
    plat = plat.cut(nfc_mark())
    return plat.removeSplitter()


def nfc_mark():
    """Contactless mark, four arcs opening toward the back, on the fin's outside: the cut,
    which runs 1 mm past the face."""
    depth, stroke, radii = NFC_MARK
    cut = None
    for r in radii:
        ring = Part.makeCylinder(r + stroke / 2, depth + 1, V(0, 0, 0)).cut(Part.makeCylinder(r - stroke / 2, depth + 1, V(0, 0, 0)))
        wedge = Part.Face(Part.makePolygon([V(0, 0, -1), V(30, -30 * math.tan(math.radians(40)), -1),
                                            V(30, 30 * math.tan(math.radians(40)), -1), V(0, 0, -1)])).extrude(V(0, 0, depth + 3))
        arc = ring.common(wedge)
        for sgn in (-1, 1):
            a = math.radians(40) * sgn
            arc = arc.fuse(Part.makeCylinder(stroke / 2, depth + 1, V(r * math.cos(a), r * math.sin(a), 0)))
        cut = arc if cut is None else cut.fuse(arc)
    cut.translate(V(-radii[-1] * (1 + math.cos(math.radians(40))) / 2, 0, 0))   # centred
    # (x, y, z) -> (W - depth .. W, Y_C + x, Z_TAG + y)
    cut.transformShape(App.Matrix(0, 0, 1, W - depth, 1, 0, 0, Y_C, 0, 1, 0, Z_TAG, 0, 0, 0, 1))
    return cut


def make_mark():
    """The mark's inlay: exactly fills the engraving, flush with the fin."""
    return nfc_mark().common(box(W - NFC_MARK[0], W, Y_C - 30, Y_C + 30, Z_TAG - 30, Z_TAG + 30))


def make_cap():
    """Plug for the reader slot: flush with the fin's top, pressing the board down."""
    g = CAP_GAP
    cap = box(SLOT_X[0] + g, SLOT_X[1] - g, Y_C - READER_L / 2 - 1 + g, Y_C + READER_L / 2 + 1 - g,
              SLOT_BOT + READER_H, FIN_TOP)
    # Grip notch at the back end, on the outside.
    cap = cap.cut(box(SLOT_X[1] - 2.0, SLOT_X[1], Y_C + READER_L / 2 - 8, Y_C + READER_L / 2 + 2, FIN_TOP - 2.5, FIN_TOP + 1))
    return cap


# --- What the case holds ----------------------------------------------------------------
def board_parts():
    """PCB, glass, the space behind the PCB, and the header plugs, in the case."""
    a0, b0 = board_uv(0, 0)
    pcb = plate_box(a0, a0 + PCB_W, b0, b0 + PCB_H, PCB_W_FRONT, PCB_W_FRONT + PCB_T)
    for a, b in PCB_HOLES:
        u, s = board_uv(a, b)
        pcb = pcb.cut(plate_cyl(u, s, 0, 30, 3.2))
    g0 = board_uv(GLASS[0], GLASS[1])
    g1 = board_uv(GLASS[2], GLASS[3])
    glass = plate_box(g0[0], g1[0], g0[1], g1[1], T_PLATE + BOSS_FLOAT, T_PLATE + BOSS_FLOAT + GLASS_T)
    back = plate_box(a0 + 1, a0 + PCB_W - 1, b0 + 1, b0 + PCB_H - 1, PCB_W_FRONT + PCB_T, PCB_W_FRONT + PCB_T + BACK_PARTS)
    return {"PCB": pcb, "glass": glass, "parts behind the PCB": back}


def access_envelopes():
    """Plugs and wires that must reach the board."""
    env = {}
    wb = PCB_W_FRONT + PCB_T
    for a in HEADERS_A:
        u, s = board_uv(a, PCB_H)
        env[f"header plug at {a:.1f}"] = plate_box(u - 3.5, u + 3.5, s - 1, s + 9, wb, wb + 5.5)
    u, s = board_uv(SD_A, PCB_H)
    env["microSD card"] = plate_box(u - 6, u + 6, s - 2, s + 4, wb, wb + 2.0)
    u, s = board_uv(PCB_W, USB_B)
    w = wb + USB_W
    env["USB-C plug overmold"] = plate_box(u + 0.5, u + 40, s - 6.2, s + 6.2, w - 3.2, w + 3.2)
    return env


def cell_model():
    yf = Y_C - CELL_L / 2
    cell = box(X_C - CELL_S / 2, X_C + CELL_S / 2, yf, yf + CELL_L, Z_CELL0, Z_PB)
    for h in CELL_HOLES:
        for y, d in ((yf + h, 5.0), (yf + CELL_L - h, 4.0)):
            cell = cell.cut(Part.makeCylinder(d / 2, CELL_S + 2, V(X_C, y, Z_CELL0 - 1)))
    return cell


def reader_model():
    return box(SLOT_X[0] + 0.1, SLOT_X[0] + 0.1 + PCB_T + 3.5, Y_C - READER_L / 2, Y_C + READER_L / 2,
               SLOT_BOT, SLOT_BOT + READER_H)


def ams_model():
    """The AMS 2 Pro in the notch, for the fit check and the pictures (see AMS)."""
    a = AMS
    x1, y0 = X_CUT - AMS_GAP, Y_POD + AMS_GAP
    x0, y1 = x1 - a["w"], y0 + a["d"]
    shell = rrect_prism(x0, x1, y0, y1, a["base_h"], a["shell_top"], a["corner_r"])
    foot = box(x0 + a["base_in_side"], x1 - a["base_in_side"], y0 + a["base_in_front"], y1 - a["base_in_back"], 0, a["base_h"])
    dome = Part.makeCylinder(a["dome_r"], a["w"] - 6, V(x0 + 3, y0 + a["dome_y"], a["shell_top"]), V(1, 0, 0)).common(
        box(x0, x1, y0, y1, a["shell_top"], a["h"]))
    return shell.fuse(foot).fuse(dome)


def spool_model(gap=0.5):
    """A 200 mm spool resting in the cradle, its right flange against the fin."""
    x1 = W - T_FIN - gap
    flange = lambda x: Part.makeCylinder(SPOOL_R, 4.0, V(x, Y_C, SPOOL_Z), V(1, 0, 0))
    core = Part.makeCylinder(50.0, SPOOL_W - 8, V(x1 - SPOOL_W + 4, Y_C, SPOOL_Z), V(1, 0, 0))
    hole = Part.makeCylinder(27.5, SPOOL_W + 2, V(x1 - SPOOL_W - 1, Y_C, SPOOL_Z), V(1, 0, 0))
    spool = flange(x1 - SPOOL_W).fuse(flange(x1 - 4)).fuse(core).cut(hole)
    spool.translate(V(0, 0, 0.02))     # resting on, not in, the cradle
    return spool


# --- Checks and export ----------------------------------------------------------------------
def overlap(a, b):
    return a.common(b).Volume if a.BoundBox.intersect(b.BoundBox) else 0.0


def check(parts):
    ok = True
    held = dict(board_parts())
    held["load cell"] = cell_model()
    held["reader"] = reader_model()
    held["spool"] = spool_model()
    held["AMS 2 Pro"] = ams_model()
    for hname, h in held.items():
        for pname, p in parts.items():
            v = overlap(h, p)
            if v > 1e-3:
                print(f"[{hname} vs {pname}] INTERFERENCE {v:.3f} mm3")
                ok = False
    for ename, e in access_envelopes().items():
        for pname, p in parts.items():
            v = overlap(e, p)
            if v > 1e-3:
                print(f"[access] {ename} blocked by {pname}: {v:.3f} mm3")
                ok = False
    print("held parts and access:", "all clear" if ok else "see above")
    pairs = [("base", "bezel"), ("base", "platform"), ("bezel", "platform"), ("platform", "fin cap"), ("platform", "RF mark")]
    for a, b in pairs:
        v = overlap(parts[a], parts[b])
        print(f"{a} vs {b} overlap: {v:.4f} mm3")
        ok &= v < 1e-3
    gaps = {
        "platform to base (must float)": parts["platform"].distToShape(parts["base"])[0],
        "platform to bezel": parts["platform"].distToShape(parts["bezel"])[0],
        "spool to base": held["spool"].distToShape(parts["base"])[0],
        "spool to bezel": held["spool"].distToShape(parts["bezel"])[0],
        "spool to cradle": held["spool"].distToShape(parts["platform"])[0],
        "PCB to base": held["PCB"].distToShape(parts["base"])[0],
        "parts behind the PCB to base": held["parts behind the PCB"].distToShape(parts["base"])[0],
        "AMS 2 Pro to base": held["AMS 2 Pro"].distToShape(parts["base"])[0],
        "AMS 2 Pro to bezel": held["AMS 2 Pro"].distToShape(parts["bezel"])[0],
        "AMS 2 Pro to platform (must not touch the scale)": held["AMS 2 Pro"].distToShape(parts["platform"])[0],
        "load cell to base": held["load cell"].distToShape(parts["base"])[0],
        "load cell to platform": held["load cell"].distToShape(parts["platform"])[0],
    }
    for k, v in gaps.items():
        print(f"gap, {k}: {v:.2f} mm")
    ok &= gaps["platform to base (must float)"] >= GAP_SIDE - 0.01
    ok &= gaps["AMS 2 Pro to platform (must not touch the scale)"] >= AMS_GAP + PLAT_INSET - 0.01
    # The cell may touch the platform only at its free end's pad: nothing of the platform
    # within CELL_CLEAR above its middle and fixed end.
    yf = Y_C + CELL_L / 2 - CELL_PAD
    above = box(X_C - CELL_S / 2, X_C + CELL_S / 2, Y_C - CELL_L / 2, yf, Z_PB, Z_PB + CELL_CLEAR - 0.01)
    v = overlap(above, parts["platform"])
    print(f"{CELL_CLEAR} mm over the load cell's middle and fixed end: " + ("clear" if v < 1e-3 else f"BLOCKED {v:.3f} mm3"))
    ok &= v < 1e-3
    return ok


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


def export_two_colour(platform, mark):
    """Platform plus the mark's inlay in the platform's print orientation, sharing one shift."""
    import Import
    shapes = [platform.copy(), mark.copy()]
    bb = shapes[0].BoundBox
    for s in shapes:
        s.translate(V(-bb.XMin, -bb.YMin, -bb.ZMin))
    os.makedirs(OUT, exist_ok=True)
    doc = App.newDocument("platform2c")
    o1 = doc.addObject("Part::Feature", "Platform")
    o1.Label, o1.Shape = "Platform", shapes[0]
    o2 = doc.addObject("Part::Feature", "RF_mark")
    o2.Label, o2.Shape = "RF mark", shapes[1]
    Import.export([o1, o2], os.path.join(OUT, "spool_station_platform_2colour.step"))
    MeshPart.meshFromShape(Shape=shapes[1], LinearDeflection=0.01, AngularDeflection=0.2, Relative=False).write(
        os.path.join(OUT, "spool_station_platform_mark.stl"))
    write_3mf(os.path.join(OUT, "spool_station_platform_2colour.3mf"), [("Platform", shapes[0]), ("RF mark", shapes[1])],
              "Spool station platform")
    App.closeDocument(doc.Name)
    mb = shapes[1].BoundBox
    print(f"RF mark: {len(shapes[1].Solids)} bodies, {shapes[1].Volume:.1f} mm3, "
          f"x {mb.XMin:.2f}..{mb.XMax:.2f} in print orientation (fin face at x {shapes[0].BoundBox.XMax:.2f})")


def export(name, shape, placement=None):
    s = shape.copy()
    if placement is not None:
        s.transformShape(placement.toMatrix())
    bb = s.BoundBox
    s.translate(V(-bb.XMin, -bb.YMin, -bb.ZMin))
    os.makedirs(OUT, exist_ok=True)
    s.exportStep(os.path.join(OUT, name + ".step"))
    MeshPart.meshFromShape(Shape=s, LinearDeflection=0.02, AngularDeflection=0.25, Relative=False).write(
        os.path.join(OUT, name + ".stl"))
    bb = s.BoundBox
    print(f"{name}: {bb.XLength:.1f} x {bb.YLength:.1f} x {bb.ZLength:.1f} mm, "
          f"volume {s.Volume / 1000:.1f} cm3, valid {s.isValid()}, solids {len(s.Solids)}")


base, bezel, platform, cap, mark = make_base(), make_bezel(), make_platform(), make_cap(), make_mark()
parts = {"base": base, "bezel": bezel, "platform": platform, "fin cap": cap, "RF mark": mark}
print(f"footprint {W:.1f} x {Y_END:.1f} mm, notch for the AMS left of x {X_CUT:.1f} behind y {Y_POD:.1f}; pod top {Z_TOP:.1f}; cradle bottom {Z_CB:.1f}; "
      f"fin top {FIN_TOP:.1f}; spool axis y {Y_C:.1f} z {SPOOL_Z:.1f}; reader centre z {Z_TAG:.1f}")
ok = check(parts)

flip = App.Placement(V(0, 0, 0), App.Rotation(V(1, 0, 0), 180))
export("spool_station_base", base)
export("spool_station_bezel", bezel, flip.multiply(PLATE_PL.inverse()))   # face down
export("spool_station_platform", platform)          # single colour: the mark stays engraved
export_two_colour(platform, mark)
export("spool_station_fin_cap", cap, App.Placement(V(0, 0, 0), App.Rotation(V(0, 1, 0), 90)))   # on its side

held = board_parts()
assembly = [base, bezel, platform, cap, mark, held["PCB"], held["glass"], cell_model(), reader_model(), spool_model()]
Part.Compound(assembly).exportStep(os.path.join(OUT, "spool_station_assembly.step"))
mesh_dir = os.path.join(OUT, "mesh")
os.makedirs(mesh_dir, exist_ok=True)
for name, shape in (("base", base), ("bezel", bezel), ("platform", platform), ("cap", cap), ("mark", mark), ("pcb", held["PCB"]),
                    ("glass", held["glass"]), ("cell", cell_model()), ("reader", reader_model()), ("spool", spool_model()),
                    ("ams", ams_model())):
    MeshPart.meshFromShape(Shape=shape, LinearDeflection=0.05, AngularDeflection=0.4, Relative=False).write(
        os.path.join(mesh_dir, name + ".stl"))
print("checks:", "PASS" if ok else "FAIL")
