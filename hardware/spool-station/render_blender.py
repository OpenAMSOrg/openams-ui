"""Preview renders of the spool station with Blender (Cycles).

    blender -b --factory-startup -P render_blender.py -- MESH_DIR OUT_DIR [SAMPLES]

MESH_DIR holds the assembly-pose STLs that build_spool_station.py writes to out/mesh.
SHOTS=a,b renders only those views. RENDER_DEVICE picks the card: OPTIX (the NVIDIA,
the default), CUDA, HIP or CPU. RES_PERCENT=50 renders smaller, for a quick look.
"""

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
CENTRE = Vector((78, 140, 55)) * S
RES = (1800, 1350)
HDRI = os.path.join(os.path.dirname(bpy.app.binary_path), "%d.%d" % bpy.app.version[:2],
                    "datafiles", "studiolights", "world", "studio.exr")

# (yaw from the front, degrees clockwise seen from above; pitch; distance mm; parts left out;
# the point looked at, mm, if not CENTRE). The AMS 2 Pro is the fit check's model of it.
WHOLE = (-75, 190, 60)
SHOTS = {
    "front_right": (35, 24, 700, ("ams",)),
    "front": (0, 18, 720, ("ams",)),
    "back_left": (215, 30, 650, ("spool", "ams")),
    "right": (90, 12, 680, ("spool", "ams")),
    "inside": (25, 55, 620, ("spool", "platform", "cap", "mark", "reader", "ams")),
    "cutaway": (285, 14, 640, ("ams",)),   # meshes cut along the load cell's axis, seen from the left
    "with_ams": (22, 24, 1250, (), WHOLE),
    "top": (0, 90, 1150, (), (-75, 190, 0)),
}


def material(name, rgb, rough=0.5, metal=0.0, alpha=1.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*rgb, 1.0)
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metal
    if alpha < 1.0:
        b.inputs["Alpha"].default_value = alpha
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
    prefs = bpy.context.preferences.addons["cycles"].preferences
    want = os.environ.get("RENDER_DEVICE", "OPTIX")
    if want != "CPU":
        prefs.compute_device_type = want
        prefs.get_devices()
        if not any(d.type == want for d in prefs.devices):
            raise SystemExit(f"no {want} device; RENDER_DEVICE=CPU renders without a GPU")
        for d in prefs.devices:
            d.use = d.type == want
        sc.cycles.device = "GPU"
    print("render device:", want, ", ".join(d.name for d in prefs.devices if d.use) if want != "CPU" else "")
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
    # A table to stand on, and a soft key light.
    bpy.ops.mesh.primitive_plane_add(size=6, location=(0.078, 0.14, -0.0002))
    bpy.context.active_object.data.materials.append(material("table", (0.80, 0.80, 0.78), rough=0.8))
    bpy.ops.object.light_add(type="AREA", location=(-0.35, -0.45, 0.65))
    key = bpy.context.active_object
    key.data.energy, key.data.size = 45, 0.6
    key.rotation_euler = (CENTRE - key.location).to_track_quat("-Z", "Y").to_euler()


def camera():
    bpy.ops.object.camera_add()
    cam = bpy.context.active_object
    cam.data.lens = 50
    bpy.context.scene.camera = cam
    return cam


def place(cam, yaw, pitch, dist, centre=None):
    c = Vector(centre) * S if centre else CENTRE
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
blue = material("cradle", (0.06, 0.28, 0.62), rough=0.45)
objs = {
    "base": load_stl("base", dark),
    "bezel": load_stl("bezel", dark),
    "platform": load_stl("platform", blue),
    "cap": load_stl("cap", blue),
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
for name, (yaw, pitch, dist, hidden, *centre) in SHOTS.items():
    if want and name not in want.split(","):
        continue
    for k, o in objs.items():
        o.hide_render = k in hidden
    place(cam, yaw, pitch, dist, centre[0] if centre else None)
    bpy.context.scene.render.filepath = os.path.join(OUT_DIR, name + ".png")
    bpy.ops.render.render(write_still=True)
    print("wrote", bpy.context.scene.render.filepath)
