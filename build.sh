#!/usr/bin/env bash
set -eo pipefail

# cd to parent dir of current script
cd "$(dirname "${BASH_SOURCE[0]}")"

set -x

meson setup --reconfigure build
meson compile -C build
