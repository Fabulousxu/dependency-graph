#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
BIN_DIR="$ROOT_DIR/build/release/bin"
PERF_BIN_DIR="$ROOT_DIR/build/relwithdebinfo/bin"
REPORT_DIR="${REPORT_DIR:-$ROOT_DIR/reports}"
REPO_CACHE_DIR="${REPO_CACHE_DIR:-$ROOT_DIR/data/repos}"

DEP5M_REPO_CONFIG="${DEP5M_REPO_CONFIG:-$ROOT_DIR/data/repos/repoconfig-dep5m.json}"
DEP11M_REPO_CONFIG="${DEP11M_REPO_CONFIG:-$ROOT_DIR/data/repos/repoconfig-dep11m.json}"
DEP28M_REPO_CONFIG="${DEP28M_REPO_CONFIG:-$ROOT_DIR/data/repos/repoconfig-dep28m.json}"

DEP5M_DATA_DIR="${DEP5M_DATA_DIR:-$ROOT_DIR/data/xpg/dep5m}"
DEP11M_DATA_DIR="${DEP11M_DATA_DIR:-$ROOT_DIR/data/xpg/dep11m}"
DEP28M_DATA_DIR="${DEP28M_DATA_DIR:-$ROOT_DIR/data/xpg/dep28m}"

usage() {
  cat <<EOF
Usage:
  $0 prepare <preset>
  $0 console <preset>
  $0 test [name] <preset> <sample-size>
  $0 benchmark [name] <preset> <sample-size>
  $0 perf <preset> <depth> <format> <mode> <time>

Presets:
  dep5m
  dep11m
  dep28m

Tests:
  query-dependencies

Benchmarks:
  query-dependencies

Formats:
  tree
  flat

Mode:
  cpu
  gpu

Examples:
  $0 prepare dep5m
  $0 console dep5m
  $0 test dep5m 100
  $0 test query-dependencies dep5m 100
  $0 benchmark dep5m 200
  $0 benchmark query-dependencies dep5m 200
  $0 perf dep28m 10 tree cpu 120
  $0 perf dep28m 10 flat gpu 120
EOF
}

preset_data_dir() {
  case "$1" in
    dep5m) echo "$DEP5M_DATA_DIR" ;;
    dep11m) echo "$DEP11M_DATA_DIR" ;;
    dep28m) echo "$DEP28M_DATA_DIR" ;;
  esac
}

preset_repo_config() {
  case "$1" in
    dep5m) echo "$DEP5M_REPO_CONFIG" ;;
    dep11m) echo "$DEP11M_REPO_CONFIG" ;;
    dep28m) echo "$DEP28M_REPO_CONFIG" ;;
  esac
}

require_preset() {
  case "${1:-}" in
    dep5m|dep11m|dep28m)
      ;;
    "")
      echo "Missing preset." >&2
      usage >&2
      exit 1
      ;;
    *)
      echo "Unknown preset: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
}

require_positive_integer() {
  if [[ -z "${2:-}" ]]; then
    echo "Missing $1." >&2
    usage >&2
    exit 1
  fi
  if ! [[ "$2" =~ ^[1-9][0-9]*$ ]]; then
    echo "Invalid $1: $2" >&2
    usage >&2
    exit 1
  fi
}

require_format() {
  case "${1:-}" in
    tree|flat)
      ;;
    "")
      echo "Missing format." >&2
      usage >&2
      exit 1
      ;;
    *)
      echo "Invalid format: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
}

require_mode() {
  case "${1:-}" in
    cpu|gpu)
      ;;
    "")
      echo "Missing mode mode." >&2
      usage >&2
      exit 1
      ;;
    *)
      echo "Invalid mode mode: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
}

parse_named_job_args() {
  case "${2:-}" in
    dep5m|dep11m|dep28m)
      job_name="$1"
      preset="$2"
      sample_size="${3:-}"
      ;;
    *)
      job_name="${2:-}"
      preset="${3:-}"
      sample_size="${4:-}"
      ;;
  esac

  require_preset "$preset"
  require_positive_integer sample-size "$sample_size"
}

case "${1:-}" in
  prepare)
    preset="${2:-}"
    require_preset "$preset"
    printf 'q\n' | "$BIN_DIR/console" \
      --open-mode create \
      --data-directory "$(preset_data_dir "$preset")" \
      --repository-config "$(preset_repo_config "$preset")" \
      --cache-directory "$REPO_CACHE_DIR"
    ;;

  console)
    preset="${2:-}"
    require_preset "$preset"
    "$BIN_DIR/console" \
      --open-mode load \
      --data-directory "$(preset_data_dir "$preset")"
    ;;

  test)
    parse_named_job_args "query-dependencies" "${2:-}" "${3:-}" "${4:-}"
    case "$job_name" in
      query-dependencies)
        mkdir -p "$REPORT_DIR/tests"
        "$BIN_DIR/query_dependencies_correctness_test" \
          --sample-size "$sample_size" \
          --max-depth 10 \
          --repository-config "$(preset_repo_config "$preset")" \
          --cache-directory "$REPO_CACHE_DIR" \
          --temp-directory "$ROOT_DIR/temp-$preset" \
          --output "$REPORT_DIR/tests/query_dependencies_correctness_test_${preset}_report.json"
        ;;
      *)
        echo "Unknown test: $job_name" >&2
        usage >&2
        exit 1
        ;;
    esac
    ;;

  benchmark)
    parse_named_job_args "query-dependencies" "${2:-}" "${3:-}" "${4:-}"
    case "$job_name" in
      query-dependencies)
        mkdir -p "$REPORT_DIR/benchmarks"
        "$BIN_DIR/query_dependencies_benchmark" \
          --sample-size "$sample_size" \
          --max-depth 10 \
          --repository-config "$(preset_repo_config "$preset")" \
          --cache-directory "$REPO_CACHE_DIR" \
          --temp-directory "$ROOT_DIR/temp-$preset" \
          --output "$REPORT_DIR/benchmarks/query_dependencies_benchmark_${preset}_report.json"
        ;;
      *)
        echo "Unknown benchmark: $job_name" >&2
        usage >&2
        exit 1
        ;;
    esac
    ;;

  perf)
    preset="${2:-}"
    depth="${3:-}"
    format="${4:-}"
    mode="${5:-}"
    time="${6:-}"
    require_preset "$preset"
    require_positive_integer "depth" "$depth"
    require_format "$format"
    require_mode "$mode"
    require_positive_integer "time" "$time"
    use_gpu=()
    if [[ "$mode" == "gpu" ]]; then
      use_gpu=(--use-gpu)
    fi
    mkdir -p "$REPORT_DIR/profile"
    profile_prefix="$REPORT_DIR/profile/query-dependencies-perf-${preset}-depth${depth}-${format}-$mode"
    perf record -F 999 -g --call-graph dwarf \
      -o "${profile_prefix}.data" -- \
      "$PERF_BIN_DIR/query_dependencies_profile" \
        --data-directory "$(preset_data_dir "$preset")" \
        --test-time "$time" \
        --depth "$depth" \
        --format "$format" \
        "${use_gpu[@]}"
    perf script -i "${profile_prefix}.data" > "${profile_prefix}.perf"
    stackcollapse-perf.pl "${profile_prefix}.perf" > "${profile_prefix}.folded"
    flamegraph.pl "${profile_prefix}.folded" > "${profile_prefix}.svg"
    echo "Generated flamegraph: ${profile_prefix}.svg"
#    rm -f "${profile_prefix}.data" "${profile_prefix}.perf" "${profile_prefix}.folded"
    ;;

  ""|-h|--help|help)
    usage
    ;;

  *)
    echo "Unknown command: $1" >&2
    usage >&2
    exit 1
    ;;
esac
