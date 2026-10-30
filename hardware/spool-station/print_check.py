"""Checks that a set's parts print without support, by slicing them with PrusaSlicer.

    python print_check.py [--slicer PATH] [--verbose] DIR

DIR is a build's folder, out/<board>/<side> (`python make.py check` runs this on the one
the menu says). Each part is sliced twice, as it lies in its STL, at 0.2 mm layers with a
0.4 mm nozzle (what the bridging steps over the counterbores are made for):

  - with supports off, for what PrusaSlicer prints over air: its bridges, and outlines it
    marks as overhanging;
  - with its automatic supports on, for faces under 35 degrees, for where it would put
    support: this also finds an edge that starts in the air, which the first pass prints
    as a plain outline and doesn't mark. The parts' faces are 45 degrees or steeper; the
    margin keeps PrusaSlicer from supporting a 45-degree countersink or the closing top
    of a nearly level teardrop hole, which print fine.

Infill is 100 % so that PrusaSlicer's bridges over sparse infill, inside the part, don't
show up. A part passes when nothing overhangs that isn't a bridge, no bridge is longer than
MAX_SPAN, and no support would go anywhere but right under one of its bridges. Exit status:
0 pass, 3 a part needs support, 1 the slicer failed. Any Python 3, nothing to install.
"""

import glob
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile

MAX_SPAN = 30.0             # mm: a longer bridge sags
NOTABLE = 500.0             # mm of support path: less is a speck, such as a 45-degree countersink
SETTINGS = ["--layer-height", "0.2", "--first-layer-height", "0.2", "--perimeters", "3",
            "--fill-density", "100%", "--fill-pattern", "rectilinear", "--nozzle-diameter", "0.4",
            "--filament-diameter", "1.75", "--bed-shape", "0x0,400x0,400x400,0x400", "--center", "200,200",
            "--dont-arrange", "--skirts", "0", "--brim-width", "0", "--use-relative-e-distances",
            "--gcode-comments"]
OVER_AIR = ("Overhang perimeter", "Bridge infill")
SUPPORT = ("Support material", "Support material interface")


def find_slicer(given=""):
    """PrusaSlicer's command, or None."""
    if given:
        path = os.path.expanduser(given)
        return path if os.path.isfile(path) or shutil.which(path) else None
    for name in ("prusa-slicer", "PrusaSlicer", "prusaslicer", "prusa-slicer-console.exe"):
        if shutil.which(name):
            return shutil.which(name)
    patterns = ["/Applications/PrusaSlicer*.app/Contents/MacOS/PrusaSlicer"]
    root = os.environ.get("ProgramFiles", "")
    if root:
        patterns.append(os.path.join(root, "Prusa3D", "PrusaSlicer*", "prusa-slicer-console.exe"))
    for pattern in patterns:
        found = sorted(glob.glob(pattern), reverse=True)
        if found:
            return found[0]
    if shutil.which("flatpak") and subprocess.call(["flatpak", "info", "com.prusa3d.PrusaSlicer"],
                                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) == 0:
        return "flatpak:com.prusa3d.PrusaSlicer"
    return None


def command(slicer):
    return ["flatpak", "run", slicer.split(":", 1)[1]] if slicer.startswith("flatpak:") else [slicer]


def stl_box(path):
    """(x0, y0, x1, y1) of a binary or text STL."""
    with open(path, "rb") as f:
        data = f.read()
    xs, ys = [], []
    if data[:5].lower() == b"solid" and b"facet" in data[:1024]:
        for m in re.finditer(rb"vertex\s+(\S+)\s+(\S+)\s+\S+", data):
            xs.append(float(m.group(1)))
            ys.append(float(m.group(2)))
    else:
        import struct
        for i in range(struct.unpack("<I", data[80:84])[0]):
            v = struct.unpack("<9f", data[84 + 50 * i + 12:84 + 50 * i + 48])
            xs += v[0::3]
            ys += v[1::3]
    return min(xs), min(ys), max(xs), max(ys)


CELL = 0.25                 # mm: the grid a layer is drawn on to see what a bridge line rests on


def clip(seg, box):
    """The part of a segment (x0, y0, x1, y1) inside box (x0, y0, x1, y1), or None."""
    x0, y0, x1, y1 = seg
    t0, t1 = 0.0, 1.0
    for p, q in ((x0 - x1, x0 - box[0]), (x1 - x0, box[2] - x0), (y0 - y1, y0 - box[1]), (y1 - y0, box[3] - y0)):
        if p == 0:
            if q < 0:
                return None
        elif p < 0:
            t0 = max(t0, q / p)
        else:
            t1 = min(t1, q / p)
    if t0 > t1:
        return None
    return x0 + t0 * (x1 - x0), y0 + t0 * (y1 - y0), x0 + t1 * (x1 - x0), y0 + t1 * (y1 - y0)


def points(seg, step):
    x0, y0, x1, y1 = seg
    n = max(1, int(math.hypot(x1 - x0, y1 - y0) / step))
    return [(x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n) for i in range(n + 1)]


def free_runs(bridge, below):
    """For each bridge line, the longest stretch of it with nothing in the layer below."""
    if not bridge:
        return []
    box = (min(min(s[0], s[2]) for s in bridge) - 1, min(min(s[1], s[3]) for s in bridge) - 1,
           max(max(s[0], s[2]) for s in bridge) + 1, max(max(s[1], s[3]) for s in bridge) + 1)
    drawn = set()
    for seg in below:
        part = clip(seg, box)
        if part:
            for x, y in points(part, CELL / 2):
                i, j = int(x // CELL), int(y // CELL)
                drawn.update((i + a, j + b) for a in (-1, 0, 1) for b in (-1, 0, 1))   # an extrusion's width
    runs = []
    for seg in bridge:
        pts = points(seg, CELL)
        step = math.hypot(seg[2] - seg[0], seg[3] - seg[1]) / max(1, len(pts) - 1)
        run = best = 0.0
        for x, y in pts:
            run = 0.0 if (int(x // CELL), int(y // CELL)) in drawn else run + step
            best = max(best, run)
        runs.append(best)
    return runs


def paths(slicer, stl, supports, tmp):
    """(kind, z, x0, y0, x1, y1, free) of each extruding move over air or of support, in the
    STL's own x and y (the slicer centres the part on its bed); free: for bridge infill, its
    longest stretch with nothing under it."""
    gcode = os.path.join(tmp, "part.gcode")
    extra = (["--support-material", "--support-material-auto", "--support-material-threshold", "35"]
             if supports else ["--no-support-material"])
    r = subprocess.run(command(slicer) + ["--export-gcode", "--output", gcode] + SETTINGS + extra + [stl],
                       capture_output=True, text=True, errors="replace")
    if r.returncode or not os.path.exists(gcode):
        raise RuntimeError(f"PrusaSlicer couldn't slice {os.path.basename(stl)}:\n{(r.stdout + r.stderr)[-1500:]}")
    x0, y0, x1, y1 = stl_box(stl)
    dx, dy = 200 - (x0 + x1) / 2, 200 - (y0 + y1) / 2
    out, kind, z, x, y = [], None, 0.0, 0.0, 0.0
    below, layer, bridge = [], [], []

    def end_layer():
        for (k, zz, a, b, c, d), run in zip(bridge, free_runs([s[2:] for s in bridge], below)):
            out.append((k, zz, a, b, c, d, run))
    with open(gcode, errors="replace") as f:
        for line in f:
            if line.startswith(";TYPE:"):
                kind = line[6:].strip()
            elif line.startswith(";Z:"):
                end_layer()
                below, layer, bridge = layer, [], []
                z = float(line[3:])
            elif line.startswith("G1 "):
                body = line.split(";")[0]
                mx, my, me = (re.search(a + r"(-?[\d.]+)", body) for a in "XYE")
                nx = float(mx.group(1)) if mx else x
                ny = float(my.group(1)) if my else y
                if me and float(me.group(1)) > 0 and (mx or my):
                    seg = (kind, z, x - dx, y - dy, nx - dx, ny - dy)
                    layer.append(seg[2:])
                    if kind == "Bridge infill":
                        bridge.append(seg)
                    elif kind in OVER_AIR + SUPPORT:
                        out.append(seg + (0.0,))
                x, y = nx, ny
    end_layer()
    os.remove(gcode)
    return out


def groups(segs, cell):
    """Segments joined where their middles are within a cell of each other, a layer or two apart."""
    parent = list(range(len(segs)))

    def root(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    grid = {}
    for i, (_, z, x0, y0, x1, y1, _) in enumerate(segs):
        grid.setdefault((round(z / 0.2), int((x0 + x1) / 2 // cell), int((y0 + y1) / 2 // cell)), []).append(i)
    for (l, cx, cy), members in grid.items():
        for m in members[1:]:
            parent[root(m)] = root(members[0])
        for key in ((l + a, cx + b, cy + c) for a in (-2, -1, 0, 1, 2) for b in (-1, 0, 1) for c in (-1, 0, 1)):
            if key in grid:
                parent[root(grid[key][0])] = root(members[0])
    found = {}
    for i, s in enumerate(segs):
        found.setdefault(root(i), []).append(s)
    return [Region(g) for g in found.values()]


class Region:
    def __init__(self, segs):
        self.segs = segs
        xs = [p for s in segs for p in (s[2], s[4])]
        ys = [p for s in segs for p in (s[3], s[5])]
        self.box = (min(xs), min(ys), max(xs), max(ys))
        self.z0, self.z1 = min(s[1] for s in segs), max(s[1] for s in segs)
        self.length = {}
        for k, _, x0, y0, x1, y1, _ in segs:
            self.length[k] = self.length.get(k, 0.0) + math.hypot(x1 - x0, y1 - y0)
        self.total = sum(self.length.values())

    def near(self, other, grow=1.5):
        a, b = self.box, other.box
        return a[0] - grow <= b[2] and b[0] - grow <= a[2] and a[1] - grow <= b[3] and b[1] - grow <= a[3]

    def span(self):
        """A bridge's span: the longest stretch of one of its lines with nothing under it."""
        return max(s[6] for s in self.segs)

    def where(self):
        x0, y0, x1, y1 = self.box
        z = f"z {self.z0:.1f}" if self.z1 - self.z0 < 0.05 else f"z {self.z0:.1f}-{self.z1:.1f}"
        return f"{z}, x {x0:.0f}-{x1:.0f}, y {y0:.0f}-{y1:.0f}"


def check_part(slicer, stl, tmp, verbose):
    """Prints what the part does over air; returns True when it needs no support."""
    air = paths(slicer, stl, False, tmp)
    bridges = [b for b in groups([s for s in air if s[0] == "Bridge infill"], 0.8) if b.total >= 5]
    loose = [g for g in groups([s for s in air if s[0] == "Overhang perimeter"], 1.5) if g.total >= 10]
    loose = [g for g in loose if not any(b.near(g) and b.z0 - 0.45 <= g.z0 <= b.z1 + 0.45 for b in bridges)]
    held = groups([s for s in paths(slicer, stl, True, tmp) if s[0] in SUPPORT], 1.5)
    held = [g for g in held if g.total >= NOTABLE]
    under = [g for g in held if any(b.near(g, 1.0) and g.z1 < b.z0 <= g.z1 + 1.0 for b in bridges)]
    unexplained = [g for g in held if g not in under]
    long_ = [b for b in bridges if b.span() > MAX_SPAN]
    ok = not (loose or unexplained or long_)
    name = os.path.basename(stl)
    print(f"  {(name + ' ').ljust(44, '.')} {'no support needed' if ok else 'NEEDS SUPPORT'}")
    real = [b for b in bridges if b.span() >= 1.0]       # the rest is bridge infill over something
    if real:
        print(f"      {len(real)} bridge{'s' if len(real) > 1 else ''}, the longest {max(b.span() for b in real):.0f} mm over air")
        if verbose:
            for b in sorted(real, key=lambda b: b.z0):
                print(f"        {b.span():5.1f} mm at {b.where()}")
    for b in long_:
        print(f"      a bridge {b.span():.0f} mm long (more than {MAX_SPAN:g}) at {b.where()}")
    for g in loose:
        print(f"      {g.total:.0f} mm of outline over air, not beside a bridge, at {g.where()}")
    for g in unexplained:
        x0, y0, x1, y1 = g.box
        print(f"      support wanted at x {x0:.0f}-{x1:.0f}, y {y0:.0f}-{y1:.0f}, up to z {g.z1:.1f}: "
              "something starts in the air there")
    if verbose and under:
        print(f"      (support PrusaSlicer would put under {len(under)} of the bridges: not needed)")
    return ok


def main(args):
    verbose = "--verbose" in args
    args = [a for a in args if a != "--verbose"]
    given = ""
    if "--slicer" in args:
        i = args.index("--slicer")
        given = args[i + 1]
        del args[i:i + 2]
    if len(args) != 1 or not os.path.isdir(args[0]):
        print(__doc__)
        return 1
    folder = args[0]
    slicer = find_slicer(given)
    if not slicer:
        print("PrusaSlicer isn't installed, or isn't where it usually is: install it (prusa3d.com), "
              "or give its path (--slicer, or SLICER in the menu)")
        return 1
    stls = sorted(f for f in glob.glob(os.path.join(folder, "spool_station_*.stl")) if "_mark" not in f)
    if not stls:
        print(f"no parts in {folder}: build first")
        return 1
    print(f"print check of {folder}: PrusaSlicer, 0.2 mm layers, as each part lies in its STL")
    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        for stl in stls:
            try:
                ok = check_part(slicer, stl, tmp, verbose) and ok
            except RuntimeError as e:
                print(e)
                return 1
    print("print check:", "PASS, every part prints without support" if ok else "FAIL")
    return 0 if ok else 3


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
