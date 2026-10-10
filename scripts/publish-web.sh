#!/usr/bin/env bash
# Rebuilds the browser preview (scripts/build-web.sh) and pushes a fresh
# build-web/site/ to the OpenAMSOrg/mmu-display-demo repo's main branch,
# which GitHub Pages serves at https://openamsorg.github.io/mmu-display-demo/.
#
# That repo holds ONLY the built site (index.html, the wasm module's glue
# .js, the .wasm, .nojekyll, a short README) — never this source tree. Each
# run replaces its history with a single fresh commit, so the demo repo never
# accumulates old wasm binaries.
#
# Usage: scripts/publish-web.sh
#   JOBS=<n>        forwarded to build-web.sh.
#   DEMO_REMOTE=... override the demo repo's git URL (default: the
#                   OpenAMSOrg/mmu-display-demo SSH remote).
#
# Requires an activated Emscripten SDK (see build-web.sh) and `gh`/git push
# access to OpenAMSOrg/mmu-display-demo.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

DEMO_REMOTE="${DEMO_REMOTE:-git@github.com:OpenAMSOrg/mmu-display-demo.git}"
PAGES_URL="https://openamsorg.github.io/mmu-display-demo/"

"$REPO_ROOT/scripts/build-web.sh"

SITE_DIR="$REPO_ROOT/build-web/site"
[ -f "$SITE_DIR/index.html" ] || { echo "publish-web.sh: no $SITE_DIR/index.html — build-web.sh did not run?" >&2; exit 1; }

cat > "$SITE_DIR/README.md" <<'EOF'
# MMU display — interactive preview

A browser build of the MMU display: a small touchscreen that clips onto one
AMS-style filament unit (Bambu AMS-style units, AFC, Happy Hare, OpenAMS,
...). This is the real UI running via WebAssembly, not a mockup — tap it
like a touchscreen.

**Try it:** https://openamsorg.github.io/mmu-display-demo/

This is a preview build for early feedback, not the shipping firmware UI.
Found something confusing, or have a suggestion? Use the "Send feedback"
link on the page, or open an issue here directly.
EOF

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

cp "$SITE_DIR/index.html" "$SITE_DIR/openams-ui-web.js" "$SITE_DIR/openams-ui-web.wasm" \
   "$SITE_DIR/.nojekyll" "$SITE_DIR/README.md" "$STAGE/"

(
    cd "$STAGE"
    git init -q
    git checkout -q -b main
    git config user.name "JR Lomas"
    git config user.email "lomas.jr@gmail.com"
    git add index.html openams-ui-web.js openams-ui-web.wasm .nojekyll README.md
    git commit -q -m "Publish the browser preview build"
    git remote add origin "$DEMO_REMOTE"
    git push -u --force origin main
)

echo "Pushed to $DEMO_REMOTE"
echo "GitHub Pages redeploys within a minute or two: $PAGES_URL"
