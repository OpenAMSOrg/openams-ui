# The spool station's printable files, by menu:
#
#   make menuconfig    pick the display, the tag reader, the side of the AMS and the
#                      screen's tilt, then build from the menu
#   make               build the files      make render / make all (and the pictures)
#   make check         check they print without support (PrusaSlicer)
#   make help          everything else
#
# make.py does the work, so that where there is no make (Windows) the same runs as
# python make.py <target>. A choice on the command line (make BOARD=e32r40t SIDE=left)
# goes to it as it is. It needs FreeCAD (and Blender for the pictures), nothing else.

PYTHON ?= python3

TARGETS := build render all check glb every menuconfig config show help
CHOICES := BOARD READER SIDE RAKE BED FREECAD BLENDER SLICER DEVICE SAMPLES SIZE OUT SHOTS VERBOSE

.DEFAULT_GOAL := build
.NOTPARALLEL:
.PHONY: $(TARGETS)

$(TARGETS):
	@$(PYTHON) make.py $@ $(foreach v,$(CHOICES),$(if $(filter command line,$(origin $(v))),'$(v)=$($(v))'))
