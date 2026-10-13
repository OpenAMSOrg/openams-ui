# The tag writer's license and sources

Everything in this directory is under the GNU General Public License v3
(`LICENSE` here); the rest of the repository stays under its MIT license. An
image built with the tag writer (`MMU_SPOOL_TAG_WRITER`, the default) is
therefore a GPLv3 work as a whole; one built without it is the MIT display
alone.

The format codecs are C++ ports of the klipper_rf PN5180 reader's codecs,
several of which are based on other GPLv3 projects; each file names its
sources in its header:

| Codec | Based on |
|---|---|
| Bambu (`bambu_codec`, `bambu_view`) | openams (GPLv3), OpenRFID (GPLv3) |
| Creality, Snapmaker, QIDI, SpoolEase, OpenSpool | OpenRFID (GPLv3) |
| Anycubic | OpenRFID and ACE-RFID (GPLv3) |
| Elegoo | OpenRFID and canvas-rfid-tool (GPLv3/MIT) |
| OpenTag3D | the OpenTag3D specification (GPLv3) |
| TigerTag | the TigerTag reference database (CC0) and SDK (Apache-2.0) |

Some codecs also carry lookup tables generated from other projects' published
data:

| Table | In | Source | Terms |
|---|---|---|---|
| TigerTag versions, materials, brands, diameters, aspects, units | `src/tigertag.h`, `src/tigertag.cpp` | TigerTag reference database, [TigerTag-Project/TigerTag-RFID-Guide](https://github.com/TigerTag-Project/TigerTag-RFID-Guide) `database/*.json`, revision `0126f51046a379fc4527985780ee4dd8c47f8a9d` | public domain (CC0 1.0) |
| OpenPrintTag fields and enums | `src/openprinttag.cpp`, `src/openprinttag.h` | OpenPrintTag specification, [OpenPrintTag/openprinttag-specification](https://github.com/OpenPrintTag/openprinttag-specification), revision `060e420892d66c062c8af440c97701aab96bac79` | MIT, Copyright 2025 Prusa Research a.s. (notice below) |
| QIDI materials and colours | `src/qidi.cpp` | QIDI's filament list, `officiall_filas_list.cfg` in [QIDITECH/QIDI_Q2](https://github.com/QIDITECH/QIDI_Q2) | GPLv3 |
| Creality materials | `src/creality.cpp` | OpenRFID `tag/creality/constants.py` | GPLv3 |
| Snapmaker materials | `src/snapmaker.cpp` | OpenRFID `tag/snapmaker/constants.py` | GPLv3 |

## OpenPrintTag specification notice

Copyright 2025 PRUSA RESEARCH A.S.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
