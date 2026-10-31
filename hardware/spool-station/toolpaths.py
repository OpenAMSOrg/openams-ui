"""The printed parts as they come off the printer, for the pictures: each part sliced with
PrusaSlicer, and the beads it would lay down built into a mesh.

    python toolpaths.py [--slicer PATH] DIR

DIR is a build's folder, out/<board>/<side>, built with its pictures' meshes (`python make.py
render` runs this before it renders). Each printed part is sliced as it lies in its STL, at
0.2 mm layers with a 0.4 mm nozzle, three perimeters and 15 % gyroid infill. Every extrusion
becomes a bead of the width and height PrusaSlicer gives it in the G-code: flat on top and
underneath, round at the sides, as it squashes onto the layer below. Only beads that can be
seen are kept: the outer walls, top skins, bridges and overhangs, the first layer, and
whatever the layer above doesn't cover. Where a picture cuts the station open, every bead
near the cut is kept too, cut off at it, so the cut shows the walls and the infill inside.

Each part goes where it sits in the station (mesh/print.json, from the build), in every
folder of meshes the build made for the pictures, as <folder>/toolpaths/<part>.ply;
render_blender.py takes those in place of the smooth parts. Needs numpy.
"""

import glob
import json
import math
import os
import re
import subprocess
import sys
import tempfile

import numpy as np

import print_check

SETTINGS = ["--layer-height", "0.2", "--first-layer-height", "0.2", "--perimeters", "3",
            "--fill-density", "15%", "--fill-pattern", "gyroid", "--nozzle-diameter", "0.4",
            "--filament-diameter", "1.75", "--bed-shape", "0x0,400x0,400x400,0x400", "--center", "200,200",
            "--dont-arrange", "--skirts", "0", "--brim-width", "0", "--use-relative-e-distances",
            "--no-support-material"]
SEEN = ("External perimeter", "Overhang perimeter", "Top solid infill", "Bridge infill", "Ironing")
SKIP = ("Skirt/Brim", "Wipe tower", "Custom", "Support material", "Support material interface")
CELL = 0.2                  # mm: the grid a layer is drawn on, to see what the next one covers
BAND = 6.0                  # mm: beads this near a cut are kept, whatever they are
SHARP = -0.2                # a bead turning more sharply than this (the cosine) starts afresh
# A bead's section, round sides of radius h/2 joined by a flat top and bottom: (out, up) of
# each corner for a bead of width w and height h is (a + r * c, r * s) on the right and
# (-a - r * c, r * s) on the left, with r = h/2 and a = w/2 - r; its normal is (+-c, s).
SECTION = [(1, 0.0, -1.0), (1, 0.866, -0.5), (1, 0.866, 0.5), (1, 0.0, 1.0),
           (-1, 0.0, 1.0), (-1, 0.866, 0.5), (-1, 0.866, -0.5), (-1, 0.0, -1.0)]


def stl_bounds(path):
    """(lowest, highest) corner of a binary or text STL."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:5].lower() == b"solid" and b"facet" in data[:1024]:
        v = np.array([[float(c) for c in m] for m in re.findall(rb"vertex\s+(\S+)\s+(\S+)\s+(\S+)", data)])
    else:
        n = int(np.frombuffer(data[80:84], "<u4")[0])
        v = np.frombuffer(data[84:84 + 50 * n], dtype=[("n", "<3f4"), ("v", "<9f4"), ("a", "<u2")])["v"].reshape(-1, 3)
    return v.min(axis=0), v.max(axis=0)


def slice_part(slicer, stl, tmp):
    """The part's extrusions as rows of (x0, y0, x1, y1, z, width, height, kind, seen), in the
    STL's frame: z is a layer's top, kind an index into the kinds list."""
    gcode = os.path.join(tmp, "part.gcode")
    r = subprocess.run(print_check.command(slicer) + ["--export-gcode", "--output", gcode] + SETTINGS + [stl],
                       capture_output=True, text=True, errors="replace")
    if r.returncode or not os.path.exists(gcode):
        raise RuntimeError(f"PrusaSlicer couldn't slice {os.path.basename(stl)}:\n{(r.stdout + r.stderr)[-1500:]}")
    # The slicer centres the part on its bed and sets it down on it.
    lo, hi = stl_bounds(stl)
    dx, dy = 200 - (lo[0] + hi[0]) / 2, 200 - (lo[1] + hi[1]) / 2
    dz = -lo[2]
    kinds, rows = [], []
    kind, z, w, h, x, y = -1, 0.0, 0.45, 0.2, 0.0, 0.0
    with open(gcode, errors="replace") as f:
        for line in f:
            if line[0] == ";":
                if line.startswith(";TYPE:"):
                    name = line[6:].strip()
                    if name not in kinds:
                        kinds.append(name)
                    kind = -1 if name in SKIP else kinds.index(name)
                elif line.startswith(";Z:"):
                    z = float(line[3:])
                elif line.startswith(";WIDTH:"):
                    w = float(line[7:])
                elif line.startswith(";HEIGHT:"):
                    h = float(line[8:])
            elif line.startswith("G1 "):
                nx, ny, e = x, y, 0.0
                for word in line.split(";")[0].split()[1:]:
                    if word[0] == "X":
                        nx = float(word[1:])
                    elif word[0] == "Y":
                        ny = float(word[1:])
                    elif word[0] == "E":
                        e = float(word[1:])
                if e > 0 and kind >= 0 and (nx != x or ny != y):
                    rows.append((x - dx, y - dy, nx - dx, ny - dy, z - dz, w, h, kind))
                x, y = nx, ny
    os.remove(gcode)
    segs = np.array(rows, dtype=float)
    seen = np.isin(segs[:, 7], [kinds.index(k) for k in SEEN if k in kinds])
    return np.column_stack([segs, seen | uncovered(segs)])


def samples(segs, step):
    """Points along each segment, no more than `step` apart, and the segment each belongs to."""
    a, b = segs[:, 0:2], segs[:, 2:4]
    n = np.maximum(1, np.ceil(np.hypot(*(b - a).T) / step)).astype(int)
    owner = np.repeat(np.arange(len(segs)), n)
    t = (np.arange(n.sum()) - np.repeat(np.cumsum(n) - n, n) + 0.5) / np.repeat(n, n)
    return a[owner] + (b - a)[owner] * t[:, None], owner


def uncovered(segs):
    """Which extrusions the next layer up leaves at least partly bare: the top layer, and the
    edges of each step in; and the first layer, which is seen from below."""
    z = segs[:, 4]
    layers = np.unique(z)
    bare = (z == layers[0]) | (z == layers[-1])
    lo = segs[:, 0:4].reshape(-1, 2).min(axis=0) - 2
    size = np.ceil((segs[:, 0:4].reshape(-1, 2).max(axis=0) + 2 - lo) / CELL).astype(int)
    for below, above in zip(layers[:-1], layers[1:]):
        mine = np.flatnonzero(z == below)
        pts, _ = samples(segs[z == above], CELL / 2)
        ij = ((pts - lo) / CELL).astype(int)
        covered = np.zeros(size, dtype=bool)
        for di in (-1, 0, 1):
            for dj in (-1, 0, 1):
                covered[ij[:, 0] + di, ij[:, 1] + dj] = True
        pts, owner = samples(segs[mine], CELL)
        ij = ((pts - lo) / CELL).astype(int)
        bare[mine[np.unique(owner[~covered[ij[:, 0], ij[:, 1]]])]] = True
    return bare


def apply(matrix, pts):
    m = np.array(matrix).reshape(4, 4)
    return pts @ m[:3, :3].T + m[:3, 3]


def clip(segs, matrix, cut):
    """The segments on the kept side of the cut (x = cut["x"] in the station, kept toward
    cut["keep"]), those across it shortened to it; and which of them are near it."""
    xa = apply(matrix, np.column_stack([segs[:, 0:2], segs[:, 4] - segs[:, 6] / 2]))[:, 0] - cut["x"]
    xb = apply(matrix, np.column_stack([segs[:, 2:4], segs[:, 4] - segs[:, 6] / 2]))[:, 0] - cut["x"]
    xa, xb = xa * cut["keep"], xb * cut["keep"]
    keep = (xa > 0) | (xb > 0)
    segs, xa, xb = segs[keep].copy(), xa[keep], xb[keep]
    t = np.where(xa != xb, xa / np.where(xa != xb, xa - xb, 1), 0)[:, None]   # where it meets the plane
    at = segs[:, 0:2] + (segs[:, 2:4] - segs[:, 0:2]) * t
    segs[xa <= 0, 0:2] = at[xa <= 0]
    segs[xb <= 0, 2:4] = at[xb <= 0]
    near = np.minimum(np.maximum(xa, 0), np.maximum(xb, 0)) < BAND
    return segs, near


def beads(segs, matrix):
    """A mesh of the segments as beads: (vertices, normals, quads, caps) in the station's
    frame, through matrix (from the STL's)."""
    n = len(segs)
    if not n:
        return None
    a, b = segs[:, 0:2], segs[:, 2:4]
    d = b - a
    d /= np.maximum(np.hypot(*d.T), 1e-9)[:, None]
    same = np.zeros(n, dtype=bool)                # carries on from the segment before it
    same[1:] = (np.all(np.abs(a[1:] - b[:-1]) < 1e-4, axis=1) & np.all(segs[1:, 4:8] == segs[:-1, 4:8], axis=1)
                & (np.sum(d[1:] * d[:-1], axis=1) > SHARP))
    first = ~same
    last = np.append(first[1:], True)
    end = np.arange(n) + np.cumsum(first)          # each segment's end point in the point list
    pts = np.zeros((n + first.sum(), 2))
    pts[end] = b
    pts[end[first] - 1] = a[first]
    seg_of = np.zeros(len(pts), dtype=int)        # the segment each point takes its bead from
    seg_of[end] = np.arange(n)
    seg_of[end[first] - 1] = np.flatnonzero(first)
    # Each point's direction across the bead: square to the segments either side, mitred.
    back = np.zeros((len(pts), 2))
    ahead = np.zeros((len(pts), 2))
    back[end] = d
    ahead[end[first] - 1] = d[first]
    ahead[end[:-1][same[1:]]] = d[1:][same[1:]]
    along = back + ahead
    along /= np.maximum(np.hypot(*along.T), 1e-9)[:, None]
    across = np.column_stack([-along[:, 1], along[:, 0]])
    one = np.where(np.hypot(*back.T) > 0, back.T, ahead.T).T
    mitre = 1 / np.maximum(np.sum(across * np.column_stack([-one[:, 1], one[:, 0]]), axis=1), 0.5)
    z, w, h = segs[seg_of, 4], segs[seg_of, 5], segs[seg_of, 6]
    r = h / 2
    flat = np.maximum(w / 2 - r, 0)
    verts, norms = [], []
    for side, c, s in SECTION:
        out = side * (flat + r * c) * mitre
        verts.append(np.column_stack([pts + across * out[:, None], z - r + r * s]))
        norms.append(np.column_stack([across * (side * c), np.full(len(pts), s)]))
    k = len(SECTION)
    verts = np.stack(verts, axis=1).reshape(-1, 3)        # point by point, its section's corners in turn
    norms = np.stack(norms, axis=1).reshape(-1, 3)
    p0 = end - 1                                          # each segment runs from point p0 to p0 + 1
    j = np.arange(k)
    quads = np.stack([p0[:, None] * k + j, p0[:, None] * k + (j + 1) % k,
                      (p0[:, None] + 1) * k + (j + 1) % k, (p0[:, None] + 1) * k + j], axis=2).reshape(-1, 4)
    # Caps on each run's ends, their own corners facing along it.
    starts, ends = end[first] - 1, end[last]
    cap_v = np.concatenate([verts.reshape(-1, k, 3)[starts], verts.reshape(-1, k, 3)[ends]]).reshape(-1, 3)
    dirs = np.concatenate([-ahead[starts], back[ends]])
    cap_n = np.repeat(np.column_stack([dirs, np.zeros(len(dirs))]), k, axis=0)
    base = len(verts)
    idx = base + np.arange(len(dirs) * k).reshape(-1, k)
    caps = np.concatenate([idx[:len(starts), ::-1], idx[len(starts):]])
    verts = apply(matrix, np.concatenate([verts, cap_v]))
    m = np.array(matrix).reshape(4, 4)[:3, :3]
    norms = np.concatenate([norms, cap_n]) @ m.T
    norms /= np.maximum(np.linalg.norm(norms, axis=1), 1e-9)[:, None]
    if np.linalg.det(m) < 0:                      # mirrored: keep the faces turned outward
        quads, caps = quads[:, ::-1], caps[:, ::-1]
    return verts, norms, quads, caps


def write_ply(path, meshes):
    meshes = [m for m in meshes if m is not None]
    if not meshes:
        return False
    verts, norms, quads, caps, at = [], [], [], [], 0
    for v, nm, q, c in meshes:
        verts.append(v)
        norms.append(nm)
        quads.append(q + at)
        caps.append(c + at)
        at += len(v)
    v = np.concatenate(verts).astype("<f4")
    nm = np.concatenate(norms).astype("<f4")
    q, c = np.concatenate(quads), np.concatenate(caps)
    k = c.shape[1]
    with open(path, "wb") as f:
        f.write((f"ply\nformat binary_little_endian 1.0\nelement vertex {len(v)}\n"
                 "property float x\nproperty float y\nproperty float z\n"
                 "property float nx\nproperty float ny\nproperty float nz\n"
                 f"element face {len(q) + len(c)}\nproperty list uchar int vertex_indices\nend_header\n").encode())
        f.write(np.hstack([v, nm]).tobytes())
        for faces, size in ((q, 4), (c, k)):
            rec = np.zeros(len(faces), dtype=[("n", "u1"), ("v", "<i4", size)])
            rec["n"], rec["v"] = size, faces
            f.write(rec.tobytes())
    return True


def main(args):
    given = ""
    if "--slicer" in args:
        i = args.index("--slicer")
        given = args[i + 1]
        del args[i:i + 2]
    if len(args) != 1 or not os.path.isfile(os.path.join(args[0], "mesh", "print.json")):
        print(__doc__)
        return 1
    folder = args[0]
    slicer = print_check.find_slicer(given)
    if not slicer:
        print("PrusaSlicer isn't installed, or isn't where it usually is: install it (prusa3d.com), "
              "or give its path (--slicer, or SLICER in the menu)")
        return 1
    parts = json.load(open(os.path.join(folder, "mesh", "print.json")))["parts"]
    sliced = {}
    with tempfile.TemporaryDirectory() as tmp:
        for stl in sorted({p["stl"] for p in parts}):
            try:
                sliced[stl] = slice_part(slicer, os.path.join(folder, stl), tmp)
            except RuntimeError as e:
                print(e)
                return 1
            segs = sliced[stl]
            print(f"  {stl:44s} {len(segs):7d} extrusions, {int(segs[:, 8].sum()):7d} seen")
    dirs = [os.path.join(folder, "mesh"), os.path.join(folder, "mesh_exploded")] + \
        sorted(glob.glob(os.path.join(folder, "mesh_cut", "*")))
    for d in dirs:
        if not os.path.isfile(os.path.join(d, "view.json")):
            continue
        view = json.load(open(os.path.join(d, "view.json")))
        cut = view.get("CUT")
        moved = os.path.basename(d) == "mesh_exploded"
        os.makedirs(os.path.join(d, "toolpaths"), exist_ok=True)
        for kind in sorted({p["kind"] for p in parts}):
            if not os.path.isfile(os.path.join(d, kind + ".stl")):      # cut away whole
                continue
            # A part the build left whole in a cut picture (its mesh reaches across the cut)
            # stays whole here too.
            whole = False
            if cut:
                lo, hi = stl_bounds(os.path.join(d, kind + ".stl"))
                whole = (cut["x"] - lo[0] if cut["keep"] > 0 else hi[0] - cut["x"]) > 1.0
            meshes = []
            for p in parts:
                if p["kind"] != kind or p.get("reader", view["reader"]) != view["reader"]:
                    continue
                m = np.array(p["matrix"]).reshape(4, 4)
                if moved:
                    m[:3, 3] += p["move"]
                segs = sliced[p["stl"]]
                if cut and not whole:
                    segs, near = clip(segs, m.ravel(), cut)
                    segs = segs[(segs[:, 8] > 0) | near]
                else:
                    segs = segs[segs[:, 8] > 0]
                meshes.append(beads(segs, m.ravel()))
            path = os.path.join(d, "toolpaths", kind + ".ply")
            if write_ply(path, meshes):
                print(f"  {os.path.relpath(path, folder)}: {os.path.getsize(path) / 1e6:.1f} MB")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
