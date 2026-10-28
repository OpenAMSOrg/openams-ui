#!/usr/bin/env python3
"""The menu of `make menuconfig` (make/Kconfig) and the choices it keeps
(make/config), through ESP-IDF's own Kconfig library and menuconfig.

  config.py defaults KCONFIG CONFIG   writes CONFIG: its choices kept, any new
                                      option at its default
  config.py menu KCONFIG CONFIG       the interactive menu
"""

import os
import sys


def main():
    if len(sys.argv) != 4 or sys.argv[1] not in ("defaults", "menu"):
        sys.exit(__doc__)
    action, kconfig, config = sys.argv[1:]
    os.environ["KCONFIG_CONFIG"] = config  # where menuconfig loads and saves
    try:
        import kconfiglib
    except ImportError:
        sys.exit("make: this Python (%s) has no ESP-IDF Kconfig library: run make from an "
                 "ESP-IDF shell (. $IDF_PATH/export.sh), or give it PYTHON=<ESP-IDF's python>"
                 % sys.executable)
    kconf = kconfiglib.Kconfig(kconfig)
    if action == "defaults":
        if os.path.exists(config):
            kconf.load_config(config)
        else:
            print("make: no %s yet, so the defaults (make menuconfig to change them)" % config)
        kconf.write_config(config)
        return
    import menuconfig
    menuconfig.menuconfig(kconf)


if __name__ == "__main__":
    main()
