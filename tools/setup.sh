#!/usr/bin/env bash
# Le Pixophone checkout bootstrap — idempotent; safe to re-run any time.
#
#   ./tools/setup.sh
#
# Initialises the git submodules (JUCE, sokol, sokol-tools-bin). Without them
# the build fails. sokol-tools-bin is cloned shallow (its full history is
# ~600 MB of old shader-compiler binaries; .gitmodules marks it shallow).

set -euo pipefail

cd "$(dirname "$0")/.."

echo "== Le Pixophone setup =="
echo "-- Initialising submodules (JUCE, sokol, sokol-tools-bin)..."
git submodule update --init --recursive

echo ""
echo "-- Done. Build with:"
echo "     cmake -S . -B build"
echo "     cmake --build build --config Release"
