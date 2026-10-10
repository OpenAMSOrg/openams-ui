#!/bin/bash
# Download the six Noto Sans CJK faces the CJK device fonts are built from,
# and verify each SHA-256 before gen_fonts.py ever sees the file.
#
# The files are large (~30 MB) and carry no change of their own, so they are
# gitignored (see .gitignore) and fetched by this script. The license travels
# with them and IS committed: assets/fonts/cjk/NotoSansCJK-OFL.txt.
#
# The commit is pinned so the subset the fonts are generated from cannot
# change under a moving upstream.  Regenerate the hashes here and in
# tools/gen_fonts.py's docstring only on a deliberate bump.
#
# Usage: bash tools/fetch_cjk_fonts.sh
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
dest="$root/assets/fonts/cjk"
base=https://raw.githubusercontent.com/notofonts/noto-cjk/f8d157532fbfaeda587e826d4cd5b21a49186f7c/Sans/SubsetOTF

# file name <TAB> subset <TAB> weight <TAB> sha256
manifest='
NotoSansJP-Medium.otf	JP	Medium	f396a3b57256e4515be9cb41f7aac54766d654890082a9f1b5c2451b5c093d8a
NotoSansJP-Regular.otf	JP	Regular	dff723ba59d57d136764a04b9b2d03205544f7cd785a711442d6d2d085ac5073
NotoSansKR-Medium.otf	KR	Medium	b46988ef13e8bac08f3933af686eaf770972994f9b6d335be0184d60169b5431
NotoSansKR-Regular.otf	KR	Regular	69975a0ac8472717870aefeab0a4d52739308d90856b9955313b2ad5e0148d68
NotoSansSC-Medium.otf	SC	Medium	7633f5a016d4dd95e685a69633d818aabc4644c4b08e26bd35b1b30c45ed5dda
NotoSansSC-Regular.otf	SC	Regular	faa6c9df652116dde789d351359f3d7e5d2285a2b2a1f04a2d7244df706d5ea9
'

mkdir -p "$dest"
printf '%s\n' "$manifest" | while IFS='	' read -r name subset weight sha; do
    [ -n "${name:-}" ] || continue
    out="$dest/$name"
    url="$base/$subset/NotoSans$subset-$weight.otf"
    if [ ! -f "$out" ]; then
        echo "fetching $name"
        curl -fsSL "$url" -o "$out.tmp"
        mv "$out.tmp" "$out"
    else
        echo "have $name"
    fi
    got=$(sha256sum "$out" | cut -d' ' -f1)
    if [ "$got" != "$sha" ]; then
        echo "SHA-256 mismatch for $name" >&2
        echo "  expected $sha" >&2
        echo "  got      $got" >&2
        exit 1
    fi
    echo "  ok $sha"
done

echo "the CJK faces are in assets/fonts/cjk/ (license: NotoSansCJK-OFL.txt)"
