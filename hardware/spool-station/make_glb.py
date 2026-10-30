"""The 3D model for the wiki: the station coming apart, as one binary glTF.

    blender -b --factory-startup -P make_glb.py -- PARTS_DIR OUT.glb

PARTS_DIR is out/<board>/<side>/mesh_parts, which build_spool_station.py writes with
STATION_PARTS=1 (`python make.py glb` does both): every part in place, and parts.json with
its move in the exploded view. The model has one animation, "explode", from assembled (its
start) to exploded (its end), which the wiki's viewer scrubs with a slider.
"""

import json
import math
import os
import struct
import sys

import bmesh
import bpy

argv = sys.argv[sys.argv.index("--") + 1:]
PARTS_DIR, OUT = argv[0], argv[1]
S = 0.001                       # mm to m
FRAMES = 60                     # 2.5 s at 24 frames a second
MAX_FACES = 40000               # per part: the pictures' meshes are finer than a viewer needs

# The pictures' colours (render_blender.py), by kind: name, colour, roughness, metallic.
LOOKS = {
    "base": ("case", (0.05, 0.055, 0.06), 0.55, 0.0),
    "bezel": ("case", (0.05, 0.055, 0.06), 0.55, 0.0),
    "platform": ("platform", (0.90, 0.20, 0.012), 0.45, 0.0),
    "cap": ("platform", (0.90, 0.20, 0.012), 0.45, 0.0),
    "mark": ("RF mark", (0.9, 0.9, 0.88), 0.45, 0.0),
    "rollers": ("roller", (0.05, 0.055, 0.06), 0.55, 0.0),
    "bearings": ("bearing steel", (0.78, 0.79, 0.8), 0.22, 1.0),
    "washers": ("washer", (0.7, 0.71, 0.72), 0.3, 1.0),
    # Brighter and rougher than the pictures' stainless: the viewer's room light is dim, and
    # a dark or mirror-like metal goes black there, lost on the black bezel.
    "screws": ("stainless", (0.9, 0.9, 0.91), 0.3, 1.0),
    "fasteners": ("stainless", (0.9, 0.9, 0.91), 0.3, 1.0),
    "glass": ("glass", (0.01, 0.012, 0.02), 0.08, 0.0),
    "pcb": ("PCB", (0.05, 0.25, 0.10), 0.6, 0.0),
    "cell": ("aluminium", (0.8, 0.8, 0.82), 0.3, 1.0),
    "reader": ("reader", (0.05, 0.15, 0.55), 0.6, 0.0),
}
NAMES = {"rollers": "roller", "bearings": "bearing", "washers": "washer", "screws": "M5 screw", "fasteners": "M3 screw",
         "cell": "load cell", "pcb": "display board", "glass": "display glass", "cap": "fin cap",
         "mark": "RF mark", "reader": "tag reader"}
materials = {}


def material(kind):
    name, rgb, rough, metal = LOOKS[kind]
    if name not in materials:
        m = bpy.data.materials.new(name)
        m.use_nodes = True
        b = m.node_tree.nodes["Principled BSDF"]
        b.inputs["Base Color"].default_value = (*rgb, 1.0)
        b.inputs["Roughness"].default_value = rough
        b.inputs["Metallic"].default_value = metal
        materials[name] = m
    return materials[name]


def load(name, kind, label):
    bpy.ops.object.select_all(action="DESELECT")
    bpy.ops.wm.stl_import(filepath=os.path.join(PARTS_DIR, name + ".stl"), global_scale=S)
    o = bpy.context.selected_objects[0]
    o.name = o.data.name = label
    bm = bmesh.new()
    bm.from_mesh(o.data)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-7)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(o.data)
    bm.free()
    if len(o.data.polygons) > MAX_FACES:
        d = o.modifiers.new("fewer faces", "DECIMATE")
        d.ratio = MAX_FACES / len(o.data.polygons)
    o.data.materials.append(material(kind))
    bpy.context.view_layer.objects.active = o
    bpy.ops.object.shade_smooth_by_angle(angle=math.radians(35), keep_sharp_edges=True)
    return o


for o in list(bpy.data.objects):           # the factory scene's cube, light and camera
    bpy.data.objects.remove(o)
scene = bpy.context.scene
scene.name = "explode"                     # the animation's name in the glTF
scene.frame_start, scene.frame_end = 0, FRAMES
scene.render.fps = 24
bpy.context.preferences.edit.keyframe_new_interpolation_type = "LINEAR"
with open(os.path.join(PARTS_DIR, "parts.json")) as f:
    parts = json.load(f)["parts"]
count = {}
for p in parts:
    kind = p["kind"]
    count[kind] = count.get(kind, 0) + 1
    label = NAMES.get(kind, kind)
    many = sum(q["kind"] == kind for q in parts) > 1
    o = load(p["name"], kind, f"{label} {count[kind]}" if many else label)
    o.location = (0, 0, 0)
    o.keyframe_insert("location", frame=0)
    o.location = tuple(c * S for c in p["move"])
    o.keyframe_insert("location", frame=FRAMES)
scene.frame_set(0)
os.makedirs(os.path.dirname(os.path.abspath(OUT)), exist_ok=True)
bpy.ops.export_scene.gltf(filepath=OUT, export_format="GLB", export_apply=True, export_animations=True,
                          export_cameras=False, export_lights=False)


def one_animation(path, name="explode"):
    """The exporter writes an animation per object; the viewer plays one. Merges them."""
    with open(path, "rb") as f:
        data = f.read()
    length = struct.unpack("<I", data[12:16])[0]
    gltf = json.loads(data[20:20 + length])
    channels, samplers = [], []
    for a in gltf.get("animations", []):
        for c in a["channels"]:
            channels.append(dict(c, sampler=c["sampler"] + len(samplers)))
        samplers += a["samplers"]
    gltf["animations"] = [{"name": name, "channels": channels, "samplers": samplers}]
    text = json.dumps(gltf, separators=(",", ":")).encode()
    text += b" " * (-len(text) % 4)
    rest = data[20 + length:]
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(text) + len(rest)))
        f.write(struct.pack("<I", len(text)) + b"JSON" + text + rest)


one_animation(OUT)
print("wrote", OUT)
