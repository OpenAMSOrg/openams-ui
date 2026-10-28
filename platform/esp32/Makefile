# The display firmware, by menu:
#
#   make menuconfig    pick the board, the tag writer and its reader, the
#                      languages and the serial port (make/Kconfig)
#   make               build it          make flash / make monitor
#   make help          everything else
#
# The choices are kept in make/config (not in git) and turn into the same -D
# options idf.py takes by hand (docs/guide/flash-and-setup.md), so both ways
# keep working. Each board builds in its own directory, build-<board>, since
# the board picks the chip. Run it from an ESP-IDF shell (. $IDF_PATH/export.sh):
# it needs idf.py, and the Python that has ESP-IDF's menuconfig.

IDF_PY ?= idf.py
PYTHON ?= python
BAUD ?= 460800
CMAKE_DEFS ?=

KCONFIG := make/Kconfig
CONFIG := make/config

.DEFAULT_GOAL := build
# idf.py runs the build in parallel itself; make flash monitor runs in order.
.NOTPARALLEL:

# The choices, as CONFIG_* variables. make writes the defaults the first time,
# and adds any option make/Kconfig gains later.
include $(CONFIG)
$(CONFIG): $(KCONFIG) make/config.py
	@$(PYTHON) make/config.py defaults $(KCONFIG) $(CONFIG)

unquote = $(patsubst "%",%,$(1))

# Each can also be given on the command line: make BOARD=es3c50p flash.
BOARD := $(if $(CONFIG_BOARD_ES3C50P),es3c50p,e32r40t)
TAG_WRITER := $(if $(CONFIG_TAG_WRITER),ON,OFF)
TAG_READER := $(if $(CONFIG_TAG_READER_MFRC522),MFRC522,PN5180)
LANGUAGES := $(or $(if $(CONFIG_LANGUAGES_JA),ja),$(if $(CONFIG_LANGUAGES_KO),ko),\
	$(if $(CONFIG_LANGUAGES_ZH_HANS),zh-Hans),$(if $(CONFIG_LANGUAGES_ALL),all),\
	$(if $(CONFIG_LANGUAGES_CUSTOM),$(call unquote,$(CONFIG_LANGUAGES_LIST))),latin)
PORT := $(or $(call unquote,$(CONFIG_PORT)),/dev/ttyUSB0)

# The board picks the chip and the tag writer the ESP-IDF settings file, so
# each pair has a directory of its own; the reader and the languages do not.
BUILD_DIR := build-$(BOARD)$(if $(filter OFF,$(TAG_WRITER)),-display)
# idf.py runs CMake again only when one of these differs from what the
# directory was configured with, so passing them every time costs nothing.
DEFS := -DMMU_BOARD=$(BOARD) -DMMU_SPOOL_TAG_WRITER=$(TAG_WRITER) -DMMU_TAG_READER=$(TAG_READER) \
	'-DMMU_LANGUAGES=$(LANGUAGES)' $(CMAKE_DEFS)
IDF := $(IDF_PY) -B $(BUILD_DIR) $(DEFS)

.PHONY: build flash monitor size idf-menuconfig clean idf menuconfig show help \
	e32r40t es3c50p libflash

# idf.py will not use a build directory that another Python (another ESP-IDF
# install or environment) or another checkout set up, and its advice, idf.py
# fullclean, would clean build/ rather than this one. The directory holds
# only build output, so it starts again.
idf:
	@command -v $(firstword $(IDF_PY)) >/dev/null || { echo "make: $(firstword $(IDF_PY)) \
	not found: run make from an ESP-IDF shell (. \$$IDF_PATH/export.sh)"; exit 1; }
	@cache=$(BUILD_DIR)/CMakeCache.txt; [ -f $$cache ] || exit 0; \
	python=$$(sed -n 's/^PYTHON:[A-Z]*=//p' $$cache); \
	home=$$(sed -n 's/^CMAKE_HOME_DIRECTORY:[A-Z]*=//p' $$cache); \
	active=$$($(PYTHON) -c 'import sys; print(sys.executable)'); \
	if [ -n "$$python" ] && [ "$$python" != "$$active" ]; then \
	    echo "make: $(BUILD_DIR) was set up with another Python ($$python): starting it again"; \
	    rm -rf $(BUILD_DIR); \
	elif [ -n "$$home" ] && [ "$$(realpath -m "$$home")" != "$$(realpath .)" ]; then \
	    echo "make: $(BUILD_DIR) was set up for another checkout ($$home): starting it again"; \
	    rm -rf $(BUILD_DIR); fi

build: idf
	$(IDF) build

flash: idf
	$(IDF) -p $(PORT) -b $(BAUD) flash

monitor: idf
	$(IDF) -p $(PORT) monitor

size: idf
	$(IDF) size

# ESP-IDF's own settings for the chosen board (its sdkconfig.* file).
idf-menuconfig: idf
	$(IDF) menuconfig

clean:
	rm -rf $(BUILD_DIR)

menuconfig:
	@$(PYTHON) make/config.py menu $(KCONFIG) $(CONFIG)
	@$(MAKE) --no-print-directory show

show:
	@echo "board       $(BOARD)"
	@echo "tag writer  $(TAG_WRITER)$(if $(filter ON,$(TAG_WRITER)), ($(TAG_READER)))"
	@echo "languages   $(LANGUAGES)"
	@echo "port        $(PORT)"
	@echo "builds in   $(BUILD_DIR)"

# One board with the other choices: make es3c50p, make e32r40t.
e32r40t es3c50p:
	@$(MAKE) --no-print-directory BOARD=$@ build

# The whole tag library into the board's library partition (tools/tagtool.py).
libflash:
	@[ -n "$(LIB)" ] || { echo "make: give the library checkout: make libflash LIB=<dir>"; exit 1; }
	$(PYTHON) ../../tools/tagtool.py --port $(PORT) libflash --lib $(LIB)

help:
	@echo "make menuconfig     choose what to build (kept in $(CONFIG))"
	@echo "make                build it into $(BUILD_DIR)"
	@echo "make flash          flash it to $(PORT); make flash monitor to watch it start"
	@echo "make monitor        the serial console"
	@echo "make size           how much of the flash and RAM it uses"
	@echo "make idf-menuconfig ESP-IDF's own settings for this board"
	@echo "make clean          delete $(BUILD_DIR)"
	@echo "make e32r40t        build that board (or es3c50p) with the other choices"
	@echo "make libflash LIB=<Bambu-Lab-RFID-Library checkout>"
	@echo "                    the whole tag library into the board's tag library"
	@echo "make show           what the choices are"
	@echo "Any choice on the command line overrides the menu: BOARD=es3c50p,"
	@echo "TAG_WRITER=OFF, TAG_READER=MFRC522, LANGUAGES=ja, PORT=/dev/ttyUSB1;"
	@echo "CMAKE_DEFS=-DMMU_APP_VERSION=1.2.0 adds more -D options."
