"""Preview renders of the spool station with Blender (Cycles).

    blender -b --factory-startup -P render_blender.py -- MESH_DIR OUT_DIR [SAMPLES]

MESH_DIR holds the STLs and view.json that build_spool_station.py writes to
out/<board>/<side>/mesh (and mesh_exploded, mesh_cut/...); the cameras follow the station's
size and side. `python make.py render` runs this for every set. SHOTS=a,b renders only those
views. RENDER_DEVICE picks the card: AUTO (the first GPU there is, else the CPU; the
default), OPTIX, CUDA, HIP, METAL, ONEAPI or CPU. RES_PERCENT=50 renders smaller.
"""

import json
import math
import os
import sys

import bmesh
import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
MESH_DIR, OUT_DIR = argv[0], argv[1]
SAMPLES = int(argv[2]) if len(argv) > 2 else 64
S = 0.001                       # mm to m
RES = (1800, 1350)
HDRI = os.path.join(os.path.dirname(bpy.app.binary_path), "%d.%d" % bpy.app.version[:2],
                    "datafiles", "studiolights", "world", "studio.exr")
VIEW = json.load(open(os.path.join(MESH_DIR, "view.json")))
LEFT = VIEW["side"] == "left"
W = VIEW["W"]

# Points looked at, mm, in the build's frame (the right-hand station's).
POINTS = {
    "station": (W / 2, VIEW["Y_END"] / 2 + 10, 65),
    "whole": ((VIEW["AMS_X"] - 186 + W) / 2, (VIEW["AMS_Y"] + 140) / 2 + 20, 60),     # the station and the AMS 2 Pro
    "floor": ((VIEW["AMS_X"] - 186 + W) / 2, (VIEW["AMS_Y"] + 140) / 2 + 20, 0),
    "fin": (W - VIEW["T_FIN"] / 2, VIEW["Y_C"], (VIEW["Z_PB"] + VIEW["FIN_TOP"]) / 2),
    "cradle": (VIEW["X_C"], VIEW["Y_C"], 45),
}
if "EXPLODE_C" in VIEW:                                    # the exploded meshes' view
    POINTS["exploded"] = tuple(VIEW["EXPLODE_C"])
    POINTS["roller"] = tuple(VIEW["ROLLER_C"])
    POINTS["screen"] = tuple(VIEW["SCREEN_C"])
# (yaw from the front, degrees clockwise seen from above; pitch; distance mm; parts left out;
# the point looked at), for the right-hand station, mirrored for the left. The AMS 2 Pro is
# the fit check's model of it.
CRADLE = ("platform", "cap", "mark", "reader", "rollers", "bearings", "washers", "screws", "guides", "cell")
SHOTS = {
    "station": (35, 24, 760, ("ams",), "station"),          # from the front, on the fin's side
    "side": (90, 12, 720, ("spool", "ams"), "station"),     # the fin's side
    "with_ams": (22, 24, 1250, (), "whole"),
    "top": (0, 90, 1150, (), "floor"),
    "cradle": (235, 42, 480, ("spool", "ams"), "cradle"),   # the rollers, from behind on the AMS's side
    "cutaway": (285, 14, 640, ("ams",), "station"),         # meshes cut along the load cell's axis
    "fin": (270, 4, 330, ("ams", "spool"), "fin"),          # meshes cut at the reader's back, from the spool's side
    "exploded": (35, 28, 1150, (), "exploded"),             # the exploded meshes
    "rollers": (-38, 24, 430, (), "roller"),                # the front roller coming apart, from the inner rail's side
    "screen": (50, 14, 560, CRADLE, "screen"),             # the board onto the bezel, the bezel onto the pod
}


def material(name, rgb, rough=0.5, metal=0.0, alpha=1.0, glow=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*rgb, 1.0)
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metal
    if alpha < 1.0:
        b.inputs["Alpha"].default_value = alpha
    if glow:
        b.inputs["Emission Color"].default_value = (*rgb, 1.0)
        b.inputs["Emission Strength"].default_value = glow
    return m


def load_stl(name, mat):
    if not os.path.exists(os.path.join(MESH_DIR, name + ".stl")):
        return None
    bpy.ops.object.select_all(action="DESELECT")
    bpy.ops.wm.stl_import(filepath=os.path.join(MESH_DIR, name + ".stl"), global_scale=S)
    o = bpy.context.selected_objects[0]
    o.name = name
    bm = bmesh.new()
    bm.from_mesh(o.data)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-7)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(o.data)
    bm.free()
    o.data.materials.append(mat)
    bpy.context.view_layer.objects.active = o
    bpy.ops.object.shade_smooth_by_angle(angle=math.radians(35), keep_sharp_edges=True)
    return o


def setup_render():
    for o in list(bpy.data.objects):   # the factory scene's cube, light and camera
        bpy.data.objects.remove(o)
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.samples = SAMPLES
    sc.cycles.use_denoising = True
    sc.render.use_persistent_data = True
    sc.view_settings.view_transform = "AgX"
    sc.view_settings.look = "AgX - Medium High Contrast"
    sc.view_settings.exposure = -0.4
    sc.render.resolution_x, sc.render.resolution_y = RES
    sc.render.resolution_percentage = int(os.environ.get("RES_PERCENT", "100"))
    sc.render.image_settings.color_mode = "RGB"
    sc.render.image_settings.compression = 90
    prefs = bpy.context.preferences.addons["cycles"].preferences
    want = os.environ.get("RENDER_DEVICE", "AUTO").upper()
    for kind in (("OPTIX", "CUDA", "HIP", "METAL", "ONEAPI") if want == "AUTO" else (want,)):
        if kind == "CPU":
            break
        try:
            prefs.compute_device_type = kind
        except TypeError:                     # this Blender doesn't know that kind
            continue
        prefs.get_devices()
        if any(d.type == kind for d in prefs.devices):
            for d in prefs.devices:
                d.use = d.type == kind
            sc.cycles.device = "GPU"
            print("render device:", kind, ", ".join(d.name for d in prefs.devices if d.use))
            break
    else:
        if want != "AUTO":
            raise SystemExit(f"no {want} device; RENDER_DEVICE=CPU renders without a GPU")
    if sc.cycles.device != "GPU":
        print("render device: CPU")
    world = bpy.data.worlds.new("studio")
    sc.world = world
    world.use_nodes = True
    nt = world.node_tree
    bg = nt.nodes["Background"]
    bg.inputs["Strength"].default_value = 0.3
    if os.path.exists(HDRI):
        env = nt.nodes.new("ShaderNodeTexEnvironment")
        env.image = bpy.data.images.load(HDRI)
        nt.links.new(env.outputs["Color"], bg.inputs["Color"])
    # The camera sees a plain light grey; the HDRI only lights and reflects.
    plain = nt.nodes.new("ShaderNodeBackground")
    plain.inputs["Color"].default_value = (0.82, 0.83, 0.84, 1.0)
    path = nt.nodes.new("ShaderNodeLightPath")
    mix = nt.nodes.new("ShaderNodeMixShader")
    nt.links.new(path.outputs["Is Camera Ray"], mix.inputs["Fac"])
    nt.links.new(bg.outputs["Background"], mix.inputs[1])
    nt.links.new(plain.outputs["Background"], mix.inputs[2])
    nt.links.new(mix.outputs["Shader"], nt.nodes["World Output"].inputs["Surface"])
    # A table to stand on, and a soft key light, from the front on the fin's side.
    centre = point("station")
    bpy.ops.mesh.primitive_plane_add(size=6, location=(centre.x, centre.y, -0.0002))
    bpy.context.active_object.data.materials.append(material("table", (0.80, 0.80, 0.78), rough=0.8))
    bpy.ops.object.light_add(type="AREA", location=(centre.x + (0.43 if LEFT else -0.43), -0.45, 0.65))
    key = bpy.context.active_object
    key.data.energy, key.data.size = 45, 0.6
    key.rotation_euler = (centre - key.location).to_track_quat("-Z", "Y").to_euler()


def point(name):
    x, y, z = POINTS[name]
    return Vector((W - x if LEFT else x, y, z)) * S


def camera():
    bpy.ops.object.camera_add()
    cam = bpy.context.active_object
    cam.data.lens = 50
    bpy.context.scene.camera = cam
    return cam


def place(cam, yaw, pitch, dist, c):
    yaw = -yaw if LEFT else yaw
    a, p = math.radians(yaw), math.radians(pitch)
    d = dist * S
    cam.location = c + Vector((d * math.cos(p) * math.sin(a), -d * math.cos(p) * math.cos(a), d * math.sin(p)))
    if pitch >= 89:   # straight down, the back of the station at the top of the picture
        cam.location = c + Vector((0, 0, d))
        cam.rotation_euler = (0, 0, math.radians(-yaw))
    else:
        cam.rotation_euler = (c - cam.location).to_track_quat("-Z", "Y").to_euler()


setup_render()
dark = material("case", (0.05, 0.055, 0.06), rough=0.55)
orange = material("cradle", (0.90, 0.20, 0.012), rough=0.45)
# Polished stainless: it mirrors the dark floor, so a flush head reads as a screw on the bezel.
# Black oxide here takes the bezel's grey, and the screws look like empty countersinks.
screw = material("stainless", (0.92, 0.92, 0.93), rough=0.12, metal=1.0)
guide = material("guide", (0.02, 0.3, 1.0), rough=0.5, glow=2.0)
objs = {
    "base": load_stl("base", dark),
    "bezel": load_stl("bezel", dark),
    "platform": load_stl("platform", orange),
    "cap": load_stl("cap", orange),
    "rollers": load_stl("rollers", dark),
    "bearings": load_stl("bearings", material("steel", (0.78, 0.79, 0.8), rough=0.22, metal=1.0)),
    "washers": load_stl("washers", material("washer", (0.7, 0.71, 0.72), rough=0.3, metal=1.0)),
    "screws": load_stl("screws", screw),
    "fasteners": load_stl("fasteners", screw),
    "guides": load_stl("guides", guide),
    "guides_screen": load_stl("guides_screen", guide),
    "mark": load_stl("mark", material("mark", (0.9, 0.9, 0.88), rough=0.45)),
    "glass": load_stl("glass", material("glass", (0.01, 0.012, 0.02), rough=0.08)),
    "pcb": load_stl("pcb", material("pcb", (0.05, 0.25, 0.10), rough=0.6)),
    "cell": load_stl("cell", material("aluminium", (0.8, 0.8, 0.82), rough=0.3, metal=1.0)),
    "reader": load_stl("reader", material("reader", (0.05, 0.15, 0.55), rough=0.6)),
    "spool": load_stl("spool", material("spool", (0.92, 0.92, 0.9), rough=0.5)),
    "ams": load_stl("ams", material("ams", (0.10, 0.10, 0.11), rough=0.35, alpha=0.75)),
}
objs = {k: o for k, o in objs.items() if o is not None}
cam = camera()
want = os.environ.get("SHOTS", "")
os.makedirs(OUT_DIR, exist_ok=True)
for name, (yaw, pitch, dist, hidden, look) in SHOTS.items():
    if want and name not in want.split(",") or look not in POINTS:
        continue
    for k, o in objs.items():
        o.hide_render = k in hidden
    place(cam, yaw, pitch, dist, point(look))
    file = f"fin_{VIEW['reader']}" if name == "fin" else name     # one per reader
    bpy.context.scene.render.filepath = os.path.join(OUT_DIR, file + ".png")
    bpy.ops.render.render(write_still=True)
    print("wrote", bpy.context.scene.render.filepath)
