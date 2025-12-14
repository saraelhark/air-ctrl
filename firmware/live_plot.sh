#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$SCRIPT_DIR"

LOGGING_DIR="$REPO_ROOT/logging"
PLOT_PY="$LOGGING_DIR/live_plot.py"
VENV_DIR="$LOGGING_DIR/venv"

DEFAULT_LOGFILE="$REPO_ROOT/-LogFileLive"
DEFAULT_Y_COLS="temp_comp_c,hum_comp_rh,gas_raw_ohm,co2_eq_ppm,breath_voc_eq_ppm,gas_pct,iaq_acc"
DEFAULT_WINDOW=0

if [[ ! -f "$PLOT_PY" ]]; then
  echo "Error: $PLOT_PY not found" >&2
  exit 1
fi

if [[ ! -f "$VENV_DIR/bin/activate" ]]; then
  echo "Error: venv not found at $VENV_DIR" >&2
  exit 1
fi

source "$VENV_DIR/bin/activate"

# Convenience: allow passing logfile as first arg without --file
# Examples:
#   ./live_plot.sh /path/to/log.txt --y iaq,gas_raw_ohm
#   ./live_plot.sh --file /path/to/log.txt --y iaq
LOGFILE=""
if [[ ${1-} != "" && ${1-} != -* ]]; then
  LOGFILE="$1"
  shift
fi

HAS_FILE_ARG=0
HAS_Y_ARG=0
HAS_WINDOW_ARG=0
for arg in "$@"; do
  if [[ "$arg" == "--file" ]]; then
    HAS_FILE_ARG=1
  elif [[ "$arg" == "--y" ]]; then
    HAS_Y_ARG=1
  elif [[ "$arg" == "--window" ]]; then
    HAS_WINDOW_ARG=1
  fi
done

cmd=(python3 "$PLOT_PY")

if [[ -n "$LOGFILE" ]]; then
  cmd+=(--file "$LOGFILE")
elif [[ $HAS_FILE_ARG -eq 0 ]]; then
  cmd+=(--file "$DEFAULT_LOGFILE")
fi

if [[ $HAS_Y_ARG -eq 0 ]]; then
  cmd+=(--y "$DEFAULT_Y_COLS")
fi

if [[ $HAS_WINDOW_ARG -eq 0 ]]; then
  cmd+=(--window "$DEFAULT_WINDOW")
fi

cmd+=("$@")

exec "${cmd[@]}"
