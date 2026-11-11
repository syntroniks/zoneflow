#!/usr/bin/env bash
# The long soak run: 2000 chaos seeds of 14 simulated days, 32 liveness
# seeds of a simulated year, 1080 halt scenarios and 160 ESPHome seeds of
# 5 days. About two hours on 16 cores. Without the sanitizers by default
# (ZF_SOAK_SANITIZE=1 for them, about three times slower). Writes the
# summary to the file given, default soak-long-report.txt.
#   test/soak/run_long.sh [report] [--jobs N]
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
report="${1:-soak-long-report.txt}"
shift || true
ZF_SOAK_SANITIZE="${ZF_SOAK_SANITIZE:-0}" exec "${here}/run.sh" --long --report "${report}" "$@"
