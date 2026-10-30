#!/usr/bin/env python3
"""The spool station's printable files, by menu, on Linux, macOS and Windows alike:

  python make.py menuconfig      pick the display, the tag reader, the side of the AMS and
                                 the screen's tilt, then build from the menu
  python make.py                 build the files the menu says (FreeCAD)
  python make.py render          and render their pictures (Blender)
  python make.py check           and check they print without support (PrusaSlicer)
  python make.py help            everything else

`make <target>` runs the same: the Makefile hands everything over to this file, which is
what runs where there is no make (Windows). It needs FreeCAD 0.21 or newer, and Blender 4
for the pictures, PrusaSlicer for the print check; any Python 3 runs it, FreeCAD's own
included, with nothing to install.

The choices are kept in station.config (not in git) and turn into the STATION_* variables
build_spool_station.py reads, so running FreeCAD by hand keeps working. A choice given as
NAME=value overrides the menu for that run (python make.py BOARD=e32r40t SIDE=left RAKE=25).
"""

import glob
import os
import re
import shlex
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CONFIG = os.path.join(HERE, "station.config")
BUILD = os.path.join(HERE, "build_spool_station.py")
RENDER = os.path.join(HERE, "render_blender.py")
GLB = os.path.join(HERE, "make_glb.py")
CHECK = os.path.join(HERE, "print_check.py")
WINDOWS = os.name == "nt"
RAKE_RANGE = (10.0, 40.0)       # the tilts the build's checks pass, given a bed big enough

# The menu, in order: (name, prompt, values, default, help). values: {value: description}
# for a choice, a (low, high) range for a number, or None for free text.
STATION = [
    ("BOARD", "Display board", {
        "es3c50p": '5" ES3C50P (ESP32-S3, 800x480, capacitive touch)',
        "es3c43p": '4.3" ES3C43P (ESP32-S3, 800x480, capacitive touch)',
        "e32r40t": '4" E32R40T (ESP32, 480x320, resistive touch)',
    }, "es3c50p",
     "The OpenAMS display the case holds: one of the three boards the display firmware builds "
     "for (platform/esp32, `make menuconfig` there). The case's width and the screen pod "
     "follow the board; the cradle, the rollers and the reader's fin are the same."),
    ("READER", "Tag reader", {
        "pn5180": "PN5180 (every tag type and format)",
        "rc522": "RC522 / MFRC522 (Bambu MIFARE Classic tags only)",
        "both": "both: a platform and a fin cap for each",
    }, "pn5180",
     "The reader stands in the fin beside the spool. Its slot takes either board, but its "
     "ledges sit at a different height for each, so the platform (and the cap over the "
     "slot) is made for one. `both` makes one of each."),
    ("SIDE", "Side of the AMS", {
        "right": "right of the AMS: the reader's fin on the right",
        "left": "left of the AMS: the station mirrored, the fin on the left",
    }, "right",
     "Which side of the AMS 2 Pro the station stands on. The screen pod reaches across in "
     "front of the AMS's corner, so the two are mirror images. The display board is "
     "mounted the right way up in both (its USB-C faces the fin)."),
    ("RAKE", "Screen tilt (degrees back from vertical)", RAKE_RANGE, "20",
     "How far the screen leans back from upright. 20 suits a station on a desk at about "
     "elbow height; more tilts the screen up toward you, for a low shelf. The crown over "
     "the screen and the case's depth follow it: further back, the base is longer. On a "
     "256 mm bed the 5\" station takes 14 to 20 degrees on the right of the AMS and 10 to "
     "31 on the left, the 4.3\" one 12 to 27 and 10 to 38, the 4\" one 10 to 40 on either "
     "side; past that the 5\" and 4.3\" need a bigger bed."),
    ("BED", "Print bed (mm, square)", (150.0, 1000.0), "256",
     "Your printer's bed. The base is the longest part and must fit it: the build checks. "
     "256 for Bambu Lab's X1, P1 and A1; 350 for the H2D."),
]
TOOLS = [
    ("FREECAD", "FreeCAD's command line", None, "",
     "FreeCADCmd (freecadcmd), FreeCAD's command-line program. Empty: look for it on the "
     "PATH and where FreeCAD installs (Windows, macOS, Flatpak, Snap). A FreeCAD AppImage "
     "works too: give its path."),
    ("BLENDER", "Blender", None, "",
     "Blender 4, for the pictures (render). Empty: look for it on the PATH and where "
     "Blender installs."),
    ("SLICER", "PrusaSlicer", None, "",
     "PrusaSlicer 2.6 or newer, for the print check (check), which slices each part to see "
     "that it prints without support. Empty: look for it on the PATH and where PrusaSlicer "
     "installs."),
    ("DEVICE", "Render device", {
        "auto": "the first GPU Blender finds, else the CPU",
        "OPTIX": "NVIDIA (OptiX)", "CUDA": "NVIDIA (CUDA)", "HIP": "AMD (HIP)",
        "METAL": "Apple (Metal)", "ONEAPI": "Intel (oneAPI)", "CPU": "the CPU only (slow)",
    }, "auto", "Which device Blender's Cycles renders the pictures on."),
    ("SAMPLES", "Render samples", (8, 4096), "64",
     "Cycles samples per pixel: more is smoother and slower. 64, denoised, is plenty."),
    ("SIZE", "Picture size (percent of 1800x1350)", (10, 200), "100",
     "50 renders a quick, smaller set."),
    ("OUT", "Where the files go", None, "",
     "Empty: out/<board>/<side>/ (out/<board>/<side>-rake<N>/ for a tilt other than 20) "
     "next to this file. A path of your own otherwise."),
]
OPTIONS = STATION + TOOLS
# Choices only the command line gives: VERBOSE=1 shows FreeCAD's and Blender's own output,
# SHOTS=station,side renders only those pictures.
EXTRA = ("VERBOSE", "SHOTS")
SHOTS = {                       # the pictures, by the meshes they are rendered from
    "mesh": ("station", "side", "with_ams", "top", "cradle"),
    "mesh_exploded": ("exploded", "rollers", "screen"),
    "mesh_cut/cell": ("cutaway",),
    "mesh_cut/fin_*": ("fin",),
}


class Stop(Exception):
    """Ends the run with a message (or with a command's exit status)."""

    def __init__(self, message="", status=1):
        super().__init__(message)
        self.status = status


# --------------------------------------------------------------- the choices

def defaults():
    return {name: default for name, _, _, default, _ in OPTIONS}


def read_config():
    values = defaults()
    if os.path.exists(CONFIG):
        with open(CONFIG, encoding="utf-8") as config:
            for line in config:
                name, sep, value = line.strip().partition("=")
                if sep and name in values:
                    values[name] = value.strip()
    return values


def write_config(values):
    with open(CONFIG, "w", encoding="utf-8") as config:
        config.write("# The spool station's choices: python make.py menuconfig changes them.\n")
        for name, prompt, _, _, _ in OPTIONS:
            config.write(f"# {prompt}\n{name}={values[name]}\n")


def check_value(name, value):
    """value, made valid for the option, or a Stop saying why it isn't."""
    option = next(o for o in OPTIONS if o[0] == name)
    values = option[2]
    if isinstance(values, dict):
        match = [v for v in values if v.lower() == value.lower()]
        if not match:
            raise Stop(f"{name} is one of {', '.join(values)}, not {value!r}")
        return match[0]
    if isinstance(values, tuple):
        try:
            number = float(value)
        except ValueError:
            raise Stop(f"{name} is a number, not {value!r}")
        if not values[0] <= number <= values[1]:
            raise Stop(f"{name} is {values[0]:g} to {values[1]:g}, not {value}")
        return f"{number:g}"
    return value


def out_dir(ch):
    if ch["OUT"]:
        return os.path.abspath(os.path.expanduser(ch["OUT"]))
    side = ch["SIDE"] if float(ch["RAKE"]) == 20 else f"{ch['SIDE']}-rake{float(ch['RAKE']):g}"
    return os.path.join(HERE, "out", ch["BOARD"], side)


def describe(name, value):
    option = next(o for o in OPTIONS if o[0] == name)
    if isinstance(option[2], dict):
        return option[2].get(value, value)
    if name == "RAKE":
        return f"{value} degrees"
    if name == "BED":
        return f"{value} mm"
    if name in ("FREECAD", "BLENDER", "SLICER"):
        found = {"FREECAD": find_freecad, "BLENDER": find_blender, "SLICER": find_slicer}[name](value, quiet=True)
        if value:
            return value + ("" if found else "  (NOT FOUND)")
        return "found: " + " ".join(found) if found else "not found: set it here"
    if name == "OUT":
        return value or "out/<board>/<side>/"
    return value


# --------------------------------------------------------------- the menu

def rule():
    print("  " + "-" * 74)


def show_menu(ch):
    print()
    print("  OpenAMS spool station: what to build")
    rule()
    for i, (name, prompt, _, _, _) in enumerate(OPTIONS, start=1):
        if i == len(STATION) + 1:
            rule()
        print(f"  {i:2d}  {(prompt + ' ').ljust(42, '.')} {describe(name, ch[name])}")
    rule()
    print(f"  Files go to {os.path.relpath(out_dir(ch), os.getcwd())}")
    print("   b  build the files         r  render their pictures    a  both")
    print("   c  check they print without support (PrusaSlicer)")
    print("   s  save and quit           q  quit without saving      ?  help")


def pick(ch, name):
    _, prompt, values, default, text = next(o for o in OPTIONS if o[0] == name)
    print()
    print(f"  {prompt}")
    for line in wrap(text, 72):
        print("    " + line)
    if isinstance(values, dict):
        keys = list(values)
        for i, key in enumerate(keys, start=1):
            mark = "*" if key == ch[name] else " "
            print(f"   {mark}{i}  {values[key]}")
        answer = input(f"  Number (Enter keeps {ch[name]}): ").strip()
        if answer.isdigit() and 1 <= int(answer) <= len(keys):
            ch[name] = keys[int(answer) - 1]
        elif answer:
            try:
                ch[name] = check_value(name, answer)
            except Stop as e:
                print(f"  {e}")
        return
    hint = f"{values[0]:g} to {values[1]:g}, " if isinstance(values, tuple) else ""
    answer = input(f"  Value ({hint}Enter keeps {ch[name] or 'empty'}, - empties it): ").strip()
    if answer == "-":
        ch[name] = default if isinstance(values, tuple) else ""
    elif answer:
        try:
            ch[name] = check_value(name, answer)
        except Stop as e:
            print(f"  {e}")


def wrap(text, width):
    lines, line = [], ""
    for word in text.split():
        if line and len(line) + 1 + len(word) > width:
            lines.append(line)
            line = word
        else:
            line = f"{line} {word}" if line else word
    return lines + [line] if line else lines


def menuconfig(ch):
    saved = dict(ch)
    while True:
        show_menu(ch)
        try:
            answer = input("  > ").strip().lower()
        except EOFError:
            answer = "q"
        if answer.isdigit() and 1 <= int(answer) <= len(OPTIONS):
            pick(ch, OPTIONS[int(answer) - 1][0])
        elif answer in ("b", "r", "a", "c"):
            write_config(ch)
            saved = dict(ch)
            try:
                if answer in ("b", "a"):
                    build(ch, pictures=answer == "a")
                if answer in ("r", "a"):
                    render(ch)
                if answer == "c":
                    check(ch)
            except Stop as e:
                if str(e):
                    print(f"make: {e}")
            input("  Enter goes back to the menu ")
        elif answer == "s":
            write_config(ch)
            print(f"  saved to {os.path.relpath(CONFIG, os.getcwd())}")
            return
        elif answer == "q":
            if ch != saved:
                print("  not saved")
            return
        elif answer in ("?", "h", "help"):
            print(__doc__)
            print(HELP)


# --------------------------------------------------------------- the programs

def which(*names):
    for name in names:
        path = shutil.which(name)
        if path:
            return path
    return None


def first(patterns):
    for pattern in patterns:
        found = sorted(glob.glob(pattern), reverse=True)    # the newest version first
        if found:
            return found[0]
    return None


def find_freecad(given="", quiet=False):
    """The command that runs FreeCAD without its window, as a list."""
    if given:
        path = os.path.expanduser(given)
        if not (os.path.isfile(path) or shutil.which(path)):
            if quiet:
                return None
            raise Stop(f"no FreeCAD at {given}: fix FREECAD in the menu")
        return [path, "freecadcmd"] if path.lower().endswith(".appimage") else [path]
    path = which("freecadcmd", "FreeCADCmd", "freecad.cmd", "FreeCADCmd.exe")
    if not path and WINDOWS:
        roots = [os.environ.get(v, "") for v in ("ProgramFiles", "LOCALAPPDATA")]
        path = first([os.path.join(r, "FreeCAD*", "bin", "FreeCADCmd.exe") for r in roots if r] +
                     [os.path.join(r, "Programs", "FreeCAD*", "bin", "FreeCADCmd.exe") for r in roots if r])
    if not path and sys.platform == "darwin":
        path = first(["/Applications/FreeCAD*.app/Contents/Resources/bin/FreeCADCmd",
                      "/Applications/FreeCAD*.app/Contents/Resources/bin/freecadcmd",
                      os.path.expanduser("~/Applications/FreeCAD*.app/Contents/Resources/bin/FreeCADCmd")])
    if path:
        return [path]
    if which("flatpak") and subprocess.call(["flatpak", "info", "org.freecad.FreeCAD"],
                                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) == 0:
        return ["flatpak", "run", "--command=FreeCADCmd", "org.freecad.FreeCAD"]
    if quiet:
        return None
    raise Stop("FreeCAD isn't installed, or isn't where it usually is: install FreeCAD 0.21 or "
               "newer (freecad.org), or give its FreeCADCmd in the menu (FREECAD)")


def find_blender(given="", quiet=False):
    if given:
        path = os.path.expanduser(given)
        if os.path.isfile(path) or shutil.which(path):
            return [path]
        if quiet:
            return None
        raise Stop(f"no Blender at {given}: fix BLENDER in the menu")
    path = which("blender", "blender.exe")
    if not path and WINDOWS:
        root = os.environ.get("ProgramFiles", "")
        path = first([os.path.join(root, "Blender Foundation", "Blender*", "blender.exe")]) if root else None
    if not path and sys.platform == "darwin":
        path = first(["/Applications/Blender*.app/Contents/MacOS/Blender"])
    if not path and which("flatpak") and subprocess.call(["flatpak", "info", "org.blender.Blender"],
                                                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) == 0:
        return ["flatpak", "run", "org.blender.Blender"]
    if path:
        return [path]
    if quiet:
        return None
    raise Stop("Blender isn't installed, or isn't where it usually is: install Blender 4 "
               "(blender.org), or give its path in the menu (BLENDER)")


def find_slicer(given="", quiet=False):
    """PrusaSlicer's command, as a list (print_check.py looks for it the same way)."""
    sys.path.insert(0, HERE)
    import print_check
    found = print_check.find_slicer(given)
    if found:
        return print_check.command(found)
    if quiet:
        return None
    if given:
        raise Stop(f"no PrusaSlicer at {given}: fix SLICER in the menu")
    raise Stop("PrusaSlicer isn't installed, or isn't where it usually is: install PrusaSlicer "
               "(prusa3d.com), or give its path in the menu (SLICER)")


# FreeCAD and Blender print a lot that says nothing: progress counters, the STEP writer's
# statistics. Without VERBOSE=1 only the build's and the renderer's own lines show.
NOISE = re.compile(r"\x1b\[[0-9;]*m|\t*\(\s*\d+ %\)|saving\.+|\t+")
QUIET = re.compile(r"^\s*$|\*{5}|Step File Name|WorkSession|Transfer|^\d+ %\)$|^Fra:|^Saved:|^ Time:|^Blender quit"
                   r"|^Read prefs|^Color management|^Warning: [^\n]*(Python|Color)|\| INFO: |^Timer '|^Blender \d")


def run(command, env=None, verbose=False):
    """Runs command, its output filtered unless verbose, and returns its exit status."""
    print("$ " + " ".join(shlex.quote(part) for part in command if "\n" not in part), flush=True)
    env = dict(os.environ, **(env or {}))
    if verbose:
        return subprocess.call(command, env=env)
    proc = subprocess.Popen(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, encoding="utf-8", errors="replace", bufsize=1)
    for line in proc.stdout:
        line = NOISE.sub("", line.rstrip("\r\n"))
        if not QUIET.search(line):
            print("  " + line, flush=True)
    return proc.wait()


# What freecadcmd runs: the build, with its outcome as the exit status (freecadcmd itself
# returns 0 whatever happens, and swallows SystemExit).
RUNNER = """
import os, sys, traceback
path = {path!r}
g = {{"__file__": path, "__name__": "__main__"}}
try:
    exec(compile(open(path, encoding="utf-8").read(), path, "exec"), g)
    status = 0 if g.get("ok") else 3
except SystemExit as e:
    print("stopped:", e)
    status = 2
except Exception:
    traceback.print_exc()
    status = 1
sys.stdout.flush()
sys.stderr.flush()
os._exit(status)
"""


def build(ch, pictures=False, parts=False):
    out = out_dir(ch)
    env = {"STATION_BOARD": ch["BOARD"], "STATION_READER": ch["READER"], "STATION_SIDE": ch["SIDE"],
           "STATION_RAKE": ch["RAKE"], "STATION_BED": ch["BED"], "STATION_OUT": out,
           "STATION_CUTS": "1" if pictures else "0", "STATION_PARTS": "1" if parts else "0"}
    print(f"make: building the {describe('BOARD', ch['BOARD'])} station, {ch['SIDE']} of the AMS, "
          f"screen {ch['RAKE']} degrees back, for " + ("both readers" if ch["READER"] == "both" else f"the {ch['READER']} reader"))
    status = run(find_freecad(ch["FREECAD"]) + ["-c", RUNNER.format(path=BUILD)], env, ch.get("VERBOSE") == "1")
    if status == 3:
        raise Stop(f"the files are in {out}, but the checks failed (see above): "
                   "something collides or doesn't fit", status)
    if status:
        raise Stop("the build stopped (see above)", status)
    print(f"make: done, the files are in {out}")
    for name in sorted(os.listdir(out)):
        if name.endswith((".stl", ".3mf")):
            print(f"        {name}")
    return out


def render(ch):
    """The pictures of the last build, into <its folder>/pictures."""
    out = out_dir(ch)
    if not os.path.isfile(os.path.join(out, "mesh", "view.json")):
        raise Stop(f"nothing built in {out} yet: build first (b, or python make.py all)")
    blender = find_blender(ch["BLENDER"])
    pictures = os.path.join(out, "pictures")
    want = set(ch["SHOTS"].split(",")) if ch.get("SHOTS") else None
    env = {"RENDER_DEVICE": ch["DEVICE"].upper(), "RES_PERCENT": ch["SIZE"]}
    done = 0
    for meshes, shots in SHOTS.items():
        shots = [s for s in shots if want is None or s in want]
        for mesh_dir in sorted(glob.glob(os.path.join(out, meshes))):
            if not shots:
                continue
            env["SHOTS"] = ",".join(shots)
            command = blender + ["-b", "--factory-startup", "--python-exit-code", "1", "-P", RENDER, "--", mesh_dir, pictures, ch["SAMPLES"]]
            if run(command, env, ch.get("VERBOSE") == "1"):
                raise Stop("Blender stopped (see above)")
            done += 1
    if not done:
        raise Stop("no meshes for those pictures: build with pictures first (a, or python make.py all)")
    print(f"make: the pictures are in {pictures}")


def glb(ch):
    """The 3D model that plays the station coming apart, for the wiki's viewer."""
    out = build(ch, parts=True)
    target = os.path.join(out, "spool_station.glb")
    if run(find_blender(ch["BLENDER"]) + ["-b", "--factory-startup", "--python-exit-code", "1", "-P", GLB, "--",
                                          os.path.join(out, "mesh_parts"), target], verbose=ch.get("VERBOSE") == "1"):
        raise Stop("Blender stopped (see above)")
    print(f"make: wrote {target}")


def check(ch):
    """The print check of the last build: every part sliced, to see it prints without support."""
    out = out_dir(ch)
    if not glob.glob(os.path.join(out, "spool_station_*.stl")):
        raise Stop(f"nothing built in {out} yet: build first (b, or python make.py build)")
    find_slicer(ch["SLICER"])
    given = ["--slicer", ch["SLICER"]] if ch["SLICER"] else []
    status = subprocess.call([sys.executable, CHECK] + given + (["--verbose"] if ch.get("VERBOSE") == "1" else []) + [out])
    if status == 3:
        raise Stop("a part needs support (see above)", status)
    if status:
        raise Stop("the print check stopped (see above)", status)


def every(ch, pictures=False, checked=False):
    """Every board on both sides, for the reader and tilt chosen: the sets in out/."""
    failed = []
    for board in STATION[0][2]:
        for side in STATION[2][2]:
            one = dict(ch, BOARD=board, SIDE=side, OUT="")
            build(one, pictures=pictures)
            if pictures:
                render(one)
            if checked:
                try:
                    check(one)
                except Stop as e:
                    if e.status != 3:
                        raise
                    failed.append(f"{board} {side}")
    if failed:
        raise Stop("these need support: " + ", ".join(failed), 3)


def show(ch):
    for name, prompt, _, _, _ in OPTIONS:
        print(f"{name}={ch[name]}    # {prompt}: {describe(name, ch[name])}")
    print(f"# the files go to {out_dir(ch)}")


HELP = """targets:
  menuconfig     the menu (config does the same)
  build          the printable files, STEP and STL, into out/<board>/<side>/ (the default)
  render         the pictures of the last build, into its pictures/ folder (Blender)
  all            build and render
  glb            build, and the 3D model of the station coming apart (spool_station.glb)
  check          slice the last build's parts (PrusaSlicer): do they print without support?
  every          every board on both sides (add render for their pictures, check to check them)
  show           the choices, and where the files go
  help           this
choices on the command line, for this run only (the menu's names):
  BOARD=es3c50p|es3c43p|e32r40t  READER=pn5180|rc522|both  SIDE=right|left  RAKE=10..40  BED=256
  FREECAD=path  BLENDER=path  SLICER=path  DEVICE=auto|OPTIX|CUDA|HIP|METAL|ONEAPI|CPU  SAMPLES=64
  SIZE=100  OUT=dir  SHOTS=station,side,...  VERBOSE=1
"""


def main(args):
    targets, given = [], {}
    for arg in args:
        name, sep, value = arg.partition("=")
        if sep:
            name = name.upper()
            if name not in defaults() and name not in EXTRA:
                raise Stop(f"no choice called {name} (python make.py help lists them)")
            given[name] = value if name in EXTRA else check_value(name, value)
        else:
            targets.append(arg.lower())
    ch = read_config()
    if os.path.exists(CONFIG):
        ch = {name: check_value(name, value) if value else value for name, value in ch.items()}
    ch.update(given)
    for target in targets or ["build"]:
        if target in ("menuconfig", "config", "menu"):
            menuconfig(ch)
        elif target == "build":
            build(ch)
        elif target == "render":
            render(ch)
        elif target == "all":
            build(ch, pictures=True)
            render(ch)
        elif target == "glb":
            glb(ch)
        elif target == "check":
            check(ch)
        elif target == "every":
            every(ch, pictures="render" in targets, checked="check" in targets)
            if "render" in targets or "check" in targets:
                return
        elif target == "show":
            show(ch)
        elif target in ("help", "-h", "--help"):
            print(__doc__)
            print(HELP)
        else:
            raise Stop(f"no target called {target} (python make.py help lists them)")


if __name__ == "__main__":
    try:
        main(sys.argv[1:])
    except Stop as e:
        if str(e):
            print(f"make: {e}", file=sys.stderr)
        sys.exit(e.status)
    except KeyboardInterrupt:
        sys.exit(130)
