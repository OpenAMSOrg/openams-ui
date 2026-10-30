#!/usr/bin/env python3
"""The display firmware, by menu, on Linux, macOS and Windows alike:

  python make.py menuconfig      pick the board, the tag writer and its reader,
                                 the tag library, the languages and the serial
                                 port (make/Kconfig)
  python make.py                 build it
  python make.py flash monitor   flash it, then watch it start
  python make.py help            everything else

`make <target>` runs the same: the Makefile hands everything over to this file,
which is what runs where there is no make (Windows). Run either from an ESP-IDF
shell (Linux and macOS: . $IDF_PATH/export.sh; Windows: the ESP-IDF PowerShell
or Command Prompt from the Start menu), which has idf.py and the Python with
ESP-IDF's menuconfig.

The choices are kept in make/config (not in git) and turn into the same -D
options idf.py takes by hand (docs/guide/flash-and-setup.md), so both ways keep
working. Each board builds in its own directory, build-<board>, since the board
picks the chip. A choice given as NAME=value overrides the menu
(python make.py flash BOARD=es3c50p PORT=COM3).
"""

import os
import shlex
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
KCONFIG = os.path.join("make", "Kconfig")
CONFIG = os.path.join("make", "config")
LIBRARY_URL = "https://github.com/queengooborg/Bambu-Lab-RFID-Library.git"
DEFAULT_LIBRARY = os.path.join("..", "..", "third_party", "Bambu-Lab-RFID-Library")
WINDOWS = os.name == "nt"

# The choices a NAME=value argument (or make NAME=value) can override.
OVERRIDES = ("BOARD", "TAG_WRITER", "TAG_READER", "LANGUAGES", "PORT", "BAUD",
             "LIBRARY_SOURCE", "TAG_LIBRARY", "LIB", "CMAKE_DEFS", "IDF_PY")


class Stop(Exception):
    """Ends the run with a message (or with a command's exit status)."""

    def __init__(self, message="", status=1):
        super().__init__(message)
        self.status = status


def esp_idf_shell():
    if WINDOWS:
        return ("open the ESP-IDF PowerShell or ESP-IDF Command Prompt from the Start "
                "menu (or run %IDF_PATH%\\export.ps1), then run this again")
    return "run . $IDF_PATH/export.sh first"


def kconfiglib():
    try:
        import kconfiglib as lib
    except ImportError:
        raise Stop("this Python (%s) has no ESP-IDF Kconfig library: %s"
                   % (sys.executable, esp_idf_shell()))
    return lib


# --------------------------------------------------------------- the choices

def write_defaults():
    """make/config, its choices kept and any option make/Kconfig gained since
    at its default: when it is missing or older than make/Kconfig."""
    if os.path.exists(CONFIG) and os.path.getmtime(CONFIG) >= os.path.getmtime(KCONFIG):
        return
    kconf = kconfiglib().Kconfig(KCONFIG)
    if os.path.exists(CONFIG):
        kconf.load_config(CONFIG)
    else:
        print("make: no %s yet, so the defaults (menuconfig to change them)" % CONFIG)
    kconf.write_config(CONFIG)


def menu():
    os.environ["KCONFIG_CONFIG"] = CONFIG  # where menuconfig loads and saves
    kconf = kconfiglib().Kconfig(KCONFIG)
    import menuconfig
    menuconfig.menuconfig(kconf)


def read_config():
    """make/config's CONFIG_NAME=value lines: "y", or a string unquoted."""
    values = {}
    with open(CONFIG, encoding="utf-8") as config:
        for line in config:
            name, sep, value = line.strip().partition("=")
            if not sep or not name.startswith("CONFIG_"):
                continue
            if len(value) >= 2 and value[0] == value[-1] == '"':
                value = value[1:-1].replace('\\"', '"').replace("\\\\", "\\")
            values[name] = value
    return values


class Choices:
    def __init__(self, given):
        c = read_config()
        on = lambda name: c.get(name) == "y"  # noqa: E731

        self.board = given.get("BOARD") or (
            "es3c50p" if on("CONFIG_BOARD_ES3C50P") else "es3c43p" if on("CONFIG_BOARD_ES3C43P")
            else "e32r40t")
        self.tag_writer = given.get("TAG_WRITER") or ("ON" if on("CONFIG_TAG_WRITER") else "OFF")
        self.reader = given.get("TAG_READER") or (
            "MFRC522" if on("CONFIG_TAG_READER_MFRC522") else "PN5180")
        self.languages = given.get("LANGUAGES") or (
            "ja" if on("CONFIG_LANGUAGES_JA") else "ko" if on("CONFIG_LANGUAGES_KO")
            else "zh-Hans" if on("CONFIG_LANGUAGES_ZH_HANS")
            else "all" if on("CONFIG_LANGUAGES_ALL")
            else c.get("CONFIG_LANGUAGES_LIST") if on("CONFIG_LANGUAGES_CUSTOM")
            else None) or "latin"
        port = given.get("PORT", c.get("CONFIG_PORT", ""))
        # make/Kconfig's default is a Linux name: on Windows idf.py finds the
        # board's COM port itself.
        self.port = "" if WINDOWS and port.startswith("/dev/") else port
        self.baud = given.get("BAUD") or "460800"
        # Where the tag writer's library is: openams (served by the printer
        # host) or board (its own flash, 5 and 4.3 inch only).
        self.library_source = given.get("LIBRARY_SOURCE") or (
            "board" if self.board in ("es3c50p", "es3c43p") and on("CONFIG_TAG_LIBRARY_BOARD")
            else "openams")
        # The board's own Bambu Lab tag library, written by flash when the
        # menu says so (LIB=<dir> says where the checkout is).
        self.tag_library = given.get("TAG_LIBRARY") or (
            "ON" if self.tag_writer == "ON" and self.library_source == "board"
            and on("CONFIG_TAG_LIBRARY") else "OFF")
        self.library_dir = os.path.expanduser(
            given.get("LIB") or c.get("CONFIG_TAG_LIBRARY_DIR") or DEFAULT_LIBRARY)
        # The board picks the chip and the tag writer the ESP-IDF settings
        # file, so each pair has a directory of its own; the reader and the
        # languages do not.
        self.build_dir = "build-%s%s" % (self.board, "-display" if self.tag_writer == "OFF" else "")
        # idf.py runs CMake again only when one of these differs from what the
        # directory was configured with, so passing them every time costs
        # nothing. No shell sits in between, so "en;ja" needs no quoting.
        self.defs = ["-DMMU_BOARD=" + self.board, "-DMMU_SPOOL_TAG_WRITER=" + self.tag_writer,
                     "-DMMU_TAG_READER=" + self.reader, "-DMMU_TAG_LIBRARY=" + self.library_source,
                     "-DMMU_LANGUAGES=" + self.languages] + shlex.split(given.get("CMAKE_DEFS", ""))
        self.idf_py = given.get("IDF_PY")


# --------------------------------------------------------------- running things

def run(command, cwd=None):
    print(" ".join(shlex.quote(part) for part in command), flush=True)
    status = subprocess.call(command, cwd=cwd)
    if status:
        raise Stop(status=status)


def idf(ch, *args):
    """idf.py for this board's directory and choices. ESP-IDF's own script,
    run by this Python: on Windows idf.py is not a program PATH can find."""
    if ch.idf_py:
        command = shlex.split(ch.idf_py)
    else:
        idf_path = os.environ.get("IDF_PATH")
        if not idf_path:
            raise Stop("IDF_PATH is not set, so this is not an ESP-IDF shell: " + esp_idf_shell())
        script = os.path.join(idf_path, "tools", "idf.py")
        if not os.path.isfile(script):
            raise Stop("no idf.py at %s: %s" % (script, esp_idf_shell()))
        command = [sys.executable, script]
    start_again_if_foreign(ch)
    run(command + ["-B", ch.build_dir] + ch.defs + list(args))


def same_path(a, b):
    return os.path.normcase(os.path.realpath(a)) == os.path.normcase(os.path.realpath(b))


def start_again_if_foreign(ch):
    """idf.py will not use a build directory that another Python (another
    ESP-IDF install or environment) or another checkout set up, and its advice,
    idf.py fullclean, would clean build/ rather than this one. The directory
    holds only build output, so it starts again."""
    cache = os.path.join(ch.build_dir, "CMakeCache.txt")
    if not os.path.isfile(cache):
        return
    python = home = ""
    with open(cache, encoding="utf-8", errors="replace") as lines:
        for line in lines:
            if line.startswith("PYTHON:"):
                python = line.split("=", 1)[1].strip()
            elif line.startswith("CMAKE_HOME_DIRECTORY:"):
                home = line.split("=", 1)[1].strip()
    why = None
    if python and not ch.idf_py and not same_path(python, sys.executable):
        why = "another Python (%s)" % python
    elif home and not same_path(home, HERE):
        why = "another checkout (%s)" % home
    if why:
        print("make: %s was set up with %s: starting it again" % (ch.build_dir, why))
        shutil.rmtree(ch.build_dir)


def port_args(ch):
    return ["-p", ch.port] if ch.port else []


def tagtool(ch, *args):
    if not ch.port:
        raise Stop("writing the tag library needs the board's serial port: set it in "
                   "menuconfig, or give PORT=%s" % ("COM3" if WINDOWS else "/dev/ttyUSB0"))
    run([sys.executable, os.path.join("..", "..", "tools", "tagtool.py"), "--port", ch.port,
         "libflash", "--lib", ch.library_dir,
         "--stamp", os.path.join(ch.build_dir, "taglib-flashed")] + list(args))


def library(ch):
    """The library checkout, cloned the first time it is needed."""
    if not os.path.isdir(ch.library_dir):
        print("make: cloning the Bambu Lab tag library into %s (about 200 MB)" % ch.library_dir)
        run(["git", "clone", "--depth", "1", LIBRARY_URL, ch.library_dir])


# --------------------------------------------------------------- the targets

def build(ch):
    idf(ch, "build")


def flash(ch):
    # The firmware, then the tag library: after the partition table it goes in.
    idf(ch, *(port_args(ch) + ["-b", ch.baud, "flash"]))
    if ch.tag_library == "ON":
        library(ch)
        tagtool(ch, "--partitions", "partitions_es3c50p.csv")


def monitor(ch):
    idf(ch, *(port_args(ch) + ["monitor"]))


def size(ch):
    idf(ch, "size")


def idf_menuconfig(ch):
    # ESP-IDF's own settings for the chosen board (its sdkconfig.* file).
    idf(ch, "menuconfig")


def clean(ch):
    if os.path.isdir(ch.build_dir):
        shutil.rmtree(ch.build_dir)
        print("make: deleted " + ch.build_dir)


def menuconfig(ch):
    menu()
    show(Choices(ch.given))


def show(ch):
    if ch.tag_writer != "ON":
        where = "-"
    elif ch.library_source == "board":
        where = "on the board" + (
            " (flash writes %s)" % ch.library_dir if ch.tag_library == "ON" else "")
    else:
        where = "from OpenAMS"
    print("board       " + ch.board)
    print("tag writer  " + ch.tag_writer + (" (%s)" % ch.reader if ch.tag_writer == "ON" else ""))
    print("tag library " + where)
    print("languages   " + ch.languages)
    print("port        " + (ch.port or "found by idf.py"))
    print("builds in   " + ch.build_dir)


def one_board(board):
    # One board with the other choices: make es3c50p, make es3c43p, make e32r40t.
    def target(ch):
        build(Choices(dict(ch.given, BOARD=board)))
    return target


def libflash(ch):
    # The library into the board's tag library now, changed or not; its
    # partition comes from the board's own table (tools/tagtool.py). Only an
    # image that keeps its own library has one to write.
    if ch.library_source != "board":
        raise Stop("this image takes its tag library from OpenAMS; menuconfig -> Tag "
                   "library -> On the board (5 and 4.3 inch only) keeps one on the board")
    library(ch)
    tagtool(ch, "--force")


def libupdate(ch):
    # The newest tags into the checkout; flash then writes them.
    library(ch)
    run(["git", "-C", ch.library_dir, "pull", "--ff-only"])


def help_(ch):
    me = "make" if os.environ.get("MAKELEVEL") else "python make.py"
    for target, what in (
            ("menuconfig", "choose what to build (kept in %s)" % CONFIG),
            ("", "build it into " + ch.build_dir),
            ("flash", "flash it to %s; flash monitor to watch it start"
             % (ch.port or "the board idf.py finds")),
            ("monitor", "the serial console"),
            ("size", "how much of the flash and RAM it uses"),
            ("idf-menuconfig", "ESP-IDF's own settings for this board"),
            ("clean", "delete " + ch.build_dir),
            ("e32r40t", "build that board (or es3c50p, es3c43p) with the other choices"),
            ("libflash", "the Bambu Lab tag library into the board now, for an image that "
                         "keeps its own (flash writes it when it changed, if the menu says so)"),
            ("libupdate", "the newest tags into the library checkout"),
            ("show", "what the choices are")):
        print("%-28s %s" % ((me + " " + target).strip(), what))
    print("Any choice given as NAME=value overrides the menu: BOARD=es3c50p, TAG_WRITER=OFF,\n"
          "TAG_READER=MFRC522, LIBRARY_SOURCE=board, TAG_LIBRARY=ON, LIB=<dir>, LANGUAGES=ja,\n"
          "PORT=%s, BAUD=921600; CMAKE_DEFS=-DMMU_APP_VERSION=1.2.0 adds more -D options."
          % ("COM3" if WINDOWS else "/dev/ttyUSB1"))


TARGETS = {
    "build": build, "flash": flash, "monitor": monitor, "size": size,
    "idf-menuconfig": idf_menuconfig, "clean": clean, "menuconfig": menuconfig, "show": show,
    "e32r40t": one_board("e32r40t"), "es3c50p": one_board("es3c50p"),
    "es3c43p": one_board("es3c43p"),
    "library": library, "libflash": libflash, "libupdate": libupdate, "help": help_,
}


def main(argv):
    os.chdir(HERE)
    targets, given = [], {}
    for arg in argv:
        name, sep, value = arg.partition("=")
        if sep and name in OVERRIDES:
            given[name] = value
        elif sep:
            raise Stop("%s is not a choice: %s" % (name, ", ".join(OVERRIDES)))
        elif arg in TARGETS:
            targets.append(arg)
        else:
            raise Stop("no target %r: python make.py help lists them" % arg)
    write_defaults()
    for target in targets or ["build"]:
        ch = Choices(given)
        ch.given = given
        TARGETS[target](ch)


if __name__ == "__main__":
    try:
        main(sys.argv[1:])
    except Stop as stop:
        if str(stop):
            print("make: %s" % stop, file=sys.stderr)
        sys.exit(stop.status)
    except KeyboardInterrupt:
        sys.exit(130)
