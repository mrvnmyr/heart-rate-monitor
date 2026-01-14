#!/usr/bin/env bash
set -eo pipefail

parentd="$(dirname "${BASH_SOURCE[0]}")"

"${parentd}/build.sh" >&2

set -x

if [[ "$1" = "analyze" ]]; then
    shift
    ./build/polarm --health-warnings --analyze-log ./log "$@"
else
    ./build/polarm --health-warnings "$@" 2>&1 | tee -a log
fi
