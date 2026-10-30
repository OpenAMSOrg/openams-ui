# The display firmware, by menu:
#
#   make menuconfig    pick the board, the tag writer and its reader, the
#                      tag library, the languages and the serial port
#                      (make/Kconfig)
#   make               build it          make flash / make monitor
#   make help          everything else
#
# make.py does the work, so that where there is no make (Windows) the same
# runs as python make.py <target>. A choice on the command line (make
# BOARD=es3c50p flash) goes to it as it is. Run it from an ESP-IDF shell
# (. $IDF_PATH/export.sh): it needs idf.py, and the Python that has ESP-IDF's
# menuconfig.

PYTHON ?= python

TARGETS := build flash monitor size idf-menuconfig clean menuconfig show help \
	e32r40t es3c50p es3c43p library libflash libupdate
CHOICES := BOARD TAG_WRITER TAG_READER LANGUAGES PORT BAUD LIBRARY_SOURCE TAG_LIBRARY \
	LIB CMAKE_DEFS IDF_PY

.DEFAULT_GOAL := build
# idf.py runs the build in parallel itself; make flash monitor runs in order.
.NOTPARALLEL:
.PHONY: $(TARGETS)

$(TARGETS):
	@$(PYTHON) make.py $@ $(foreach v,$(CHOICES),$(if $(filter command line,$(origin $(v))),'$(v)=$($(v))'))
