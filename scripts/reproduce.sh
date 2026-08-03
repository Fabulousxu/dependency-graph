#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
RELEASE_DIR="$ROOT_DIR/build/release"
RELWITHDEBINFO_DIR="$ROOT_DIR/build/relwithdebinfo"
PRESET_DATA_DIR="${PRESET_DATA_DIR:-$ROOT_DIR/data}"
REPO_CACHE_DIR="${REPO_CACHE_DIR:-$ROOT_DIR/data/repos}"
MEMGRAPH_BIN="${MEMGRAPH_BIN:-$HOME/opt/memgraph-3.12.0/usr/lib/memgraph/memgraph}"
PERF_MEMGRAPH_BIN="${PERF_MEMGRAPH_BIN:-$HOME/opt/memgraph-3.12.0-source/build/memgraph}"

memgraph_pid=""

preset_bolt_port() {
  case "$1" in
    dep5m) echo 7688 ;;
    dep11m) echo 7689 ;;
    dep28m) echo 7690 ;;
  esac
}

preset_metrics_port() {
  case "$1" in
    dep5m) echo 9092 ;;
    dep11m) echo 9093 ;;
    dep28m) echo 9094 ;;
  esac
}

usage() {
  cat <<EOF
Usage:
  $0 prepare [memgraph] --preset <preset>
  $0 console [memgraph] --preset <preset>
  $0 test --preset <preset> --sample-size <sample-size> [--test-memgraph]
  $0 benchmark --preset <preset> --sample-size <sample-size> [--test-memgraph]
  $0 perf --preset <preset> --depth <depth> --format <format> --mode <mode> --time <time>

Presets:
  dep5m
  dep11m
  dep28m

Tests:
  query-dependencies

Benchmarks:
  query-dependencies

Query Formats:
  tree
  flat

Query Mode:
  cpu
  gpu
  memgraph
  memgraph_query_module

Examples:
  $0 prepare --preset dep5m
  $0 prepare memgraph --preset dep5m
  $0 prepare --preset dep5m memgraph
  $0 console --preset dep5m
  $0 console memgraph --preset dep5m
  $0 console --preset dep5m memgraph
  $0 test --preset dep5m
  $0 test --preset dep5m --sample-size 100 --test-memgraph
  $0 benchmark --preset dep5m --sample-size 200
  $0 benchmark --preset dep5m --sample-size 200 --test-memgraph
  $0 perf --preset dep28m --depth 10 --format tree --mode cpu --time 120
  $0 perf --preset dep28m --depth 10 --format flat --mode gpu --time 120
  $0 perf --preset dep5m --depth 10 --format tree --mode memgraph --time 120
  $0 perf --preset dep5m --depth 10 --format tree --mode memgraph_query_module --time 120
EOF
}

parse_options() {
  command="$1"
  memgraph_mode=false
  preset=""
  sample_size=""
  depth=""
  test_memgraph=false
  query_format=""
  query_mode=""
  time=""

  shift
  while [[ $# -gt 0 ]]; do
    case "$1" in
      memgraph)
        memgraph_mode=true
        shift
        ;;
      --preset)
        preset="$2"
        case "$preset" in
          dep5m|dep11m|dep28m) ;;
          *)
            echo "Unknown preset: $preset" >&2
            usage >&2
            exit 1
            ;;
        esac
        shift 2
        ;;
      --sample-size)
        sample_size="$2"
        if ! [[ "$sample_size" =~ ^[1-9][0-9]*$ ]]; then
          echo "Invalid sample-size: $sample_size" >&2
          usage >&2
          exit 1
        fi
        shift 2
        ;;
      --test-memgraph)
        test_memgraph=true
        shift
        ;;
      --depth)
        depth="$2"
        if ! [[ "$depth" =~ ^[1-9][0-9]*$ ]]; then
          echo "Invalid depth: $depth" >&2
          usage >&2
          exit 1
        fi
        shift 2
        ;;
      --format)
        query_format="$2"
        case "$query_format" in
          tree|flat) ;;
          *)
            echo "Invalid format: $query_format" >&2
            usage >&2
            exit 1
            ;;
        esac
        shift 2
        ;;
      --mode)
        query_mode="${2:-}"
        case "$query_mode" in
          cpu|gpu|memgraph|memgraph_query_module) ;;
          *)
            echo "Invalid mode: $query_mode" >&2
            usage >&2
            exit 1
            ;;
        esac
        shift 2
        ;;
      --time)
        test_time="${2:-}"
        if ! [[ "$test_time" =~ ^[1-9][0-9]*$ ]]; then
          echo "Invalid time: $test_time" >&2
          usage >&2
          exit 1
        fi
        shift 2
        ;;
      --help|-h|help)
        usage
        exit 0
        ;;
      *)
        echo "Unknown option: $1" >&2
        usage >&2
        exit 1
        ;;
    esac
  done
}

wait_memgraph() {
  local host="$1"
  local port="$2"
  local timeout="${3:-30}"
  local deadline=$((SECONDS + timeout))

  until (exec 3<>"/dev/tcp/$host/$port") >/dev/null 2>&1; do
    if ((SECONDS >= deadline)); then
      echo "Timed out waiting for Memgraph on $host:$port." >&2
      return 1
    fi
    sleep 1
  done
}

start_memgraph() {
  local preset="$1"
  local perf_output="${2:-}"
  local data_dir="$PRESET_DATA_DIR/memgraph/$preset"
  local bolt_port="$(preset_bolt_port "$preset")"
  local logs_dir="$data_dir/logs"
  local stderr_file="$logs_dir/memgraph.stderr.log"
  local memgraph_bin="$MEMGRAPH_BIN"
  local query_modules_dir="$RELEASE_DIR/query_modules"
  if [[ -n "$perf_output" ]]; then
    memgraph_bin="$PERF_MEMGRAPH_BIN"
    query_modules_dir="$RELWITHDEBINFO_DIR/query_modules"
  fi

  mkdir -p "$data_dir" "$logs_dir"
  local memgraph_cmd=(
    "$memgraph_bin"
    --data-directory="$data_dir"
    --bolt-address=127.0.0.1
    --bolt-port="$bolt_port"
    --metrics-port="$(preset_metrics_port "$preset")"
    --storage-properties-on-edges=true
    --query-modules-directory="$query_modules_dir"
    --log-file="$logs_dir/memgraph.log"
    --also-log-to-stderr=false
    --telemetry-enabled=false
  )

  echo "Starting Memgraph on 127.0.0.1:$bolt_port..."
  if [[ -n "$perf_output" ]]; then
    mkdir -p "$(dirname "$perf_output")"
    setsid perf record -F 199 -g --call-graph fp --user-callchains -o "$perf_output" -- \
      "${memgraph_cmd[@]}" >"$logs_dir/memgraph.stdout.log" 2>"$logs_dir/memgraph.stderr.log" &
    memgraph_pid="$!"
  else
    setsid "${memgraph_cmd[@]}" >"$logs_dir/memgraph.stdout.log" 2>"$logs_dir/memgraph.stderr.log" &
    memgraph_pid="$!"
  fi

  wait_memgraph 127.0.0.1 "$bolt_port" 30 || {
    echo "Memgraph stderr:" >&2
    tail -80 "$stderr_file" >&2 || true
    echo "Memgraph log:" >&2
    local latest_log
    latest_log="$(ls -t "$logs_dir"/memgraph*.log 2>/dev/null | head -n 1 || true)"
    if [[ -n "$latest_log" ]]; then
      tail -80 "$latest_log" >&2 || true
    else
      tail -80 "$logs_dir/memgraph.log" >&2 || true
    fi
    if [[ -n "$perf_output" ]]; then
      stop_memgraph INT
    else
      stop_memgraph
    fi
    exit 1
  }
  echo "Memgraph started on 127.0.0.1:$bolt_port."
}

stop_memgraph() {
  local signal="${1:-TERM}"
  [[ -n "$memgraph_pid" ]] || return 0
  if kill -0 "$memgraph_pid" 2>/dev/null; then
    kill -s "$signal" -- "-$memgraph_pid" 2>/dev/null || true
  fi
  for _ in {1..10}; do
    if ! kill -0 "$memgraph_pid" 2>/dev/null; then break; fi
    sleep 1
  done
  if kill -0 "$memgraph_pid" 2>/dev/null; then
    kill -KILL -- "-$memgraph_pid" 2>/dev/null || true
  fi
  wait "$memgraph_pid" 2>/dev/null || true
  memgraph_pid=""
}

trap stop_memgraph EXIT

generate_flamegraph() {
  local output="$1"
  perf script -i "$output.data" > "$output.perf"
  stackcollapse-perf.pl "$output.perf" > "$output.folded"
  flamegraph.pl "$output.folded" > "$output.svg"
  echo "Generated flamegraph: $output.svg"
}

perf_run() {
  local output="$1"
  local status
  shift
  mkdir -p "$(dirname "$output")"
  if perf record -F 199 -g --call-graph dwarf -o "${output}.data" -- "$@"; then
    status=0
  else
    status=$?
  fi
  return "$status"
}

case "${1:-}" in
  prepare)
    shift
    parse_options prepare "$@"
    if [[ "$memgraph_mode" != true ]]; then
      rm -rf "$PRESET_DATA_DIR/xpgraph/$preset"
      printf 'q\n' | "$RELEASE_DIR/bin/console" \
        --open-mode create \
        --data-directory "$PRESET_DATA_DIR/xpgraph/$preset" \
        --repository-config "$PRESET_DATA_DIR/repos/repoconfig-$preset.json" \
        --cache-directory "$REPO_CACHE_DIR"
    else
      rm -rf "$PRESET_DATA_DIR/memgraph/$preset"
      start_memgraph "$preset"
      set +e
      printf 'q\n' | "$RELEASE_DIR/bin/console" \
        --open-mode create \
        --use-memgraph \
        --host 127.0.0.1 \
        --port "$(preset_bolt_port "$preset")" \
        --repository-config "$PRESET_DATA_DIR/repos/repoconfig-$preset.json" \
        --cache-directory "$REPO_CACHE_DIR"
      status=$?
      set -e
      stop_memgraph
      exit "$status"
    fi
    ;;

  console)
    shift
    parse_options console "$@"
    if [[ "$memgraph_mode" != true ]]; then
      "$RELEASE_DIR/bin/console" \
        --open-mode load \
        --data-directory "$PRESET_DATA_DIR/xpgraph/$preset"
    else
      start_memgraph "$preset"
      set +e
      "$RELEASE_DIR/bin/console" \
        --open-mode load \
        --use-memgraph \
        --host 127.0.0.1 \
        --port "$(preset_bolt_port "$preset")"
      status=$?
      set -e
      stop_memgraph
      exit "$status"
    fi
    ;;

  test)
    shift
    parse_options test "$@"
    test_memgraph_args=()
    if [[ "$test_memgraph" == true ]]; then
      start_memgraph "$preset"
      test_memgraph_args=(
        --test-memgraph
        --host 127.0.0.1
        --port "$(preset_bolt_port "$preset")"
      )
    fi
    set +e
    "$RELEASE_DIR/bin/query_dependencies_correctness_test" \
      --sample-size "$sample_size" \
      --max-depth 10 \
      --repository-config "$PRESET_DATA_DIR/repos/repoconfig-$preset.json" \
      --data-directory "$PRESET_DATA_DIR/xpgraph/$preset" \
      --output "$ROOT_DIR/reports/tests/query_dependencies_correctness_test_${preset}_report.json" \
      "${test_memgraph_args[@]}"
    status=$?
    set -e
    if [[ "$test_memgraph" == true ]]; then stop_memgraph; fi
    exit "$status"
    ;;

  benchmark)
    shift
    parse_options benchmark "$@"
    test_memgraph_args=()
    if [[ "$test_memgraph" == true ]]; then
      start_memgraph "$preset"
      test_memgraph_args=(
        --test-memgraph
        --host 127.0.0.1
        --port "$(preset_bolt_port "$preset")"
      )
    fi
    set +e
    "$RELEASE_DIR/bin/query_dependencies_benchmark" \
      --sample-size "$sample_size" \
      --max-depth 10 \
      --repository-config "$PRESET_DATA_DIR/repos/repoconfig-$preset.json" \
      --data-directory "$PRESET_DATA_DIR/xpgraph/$preset" \
      --output "$ROOT_DIR/reports/benchmarks/query_dependencies_benchmark_${preset}_report.json" \
      "${test_memgraph_args[@]}"
    status=$?
    set -e
    if [[ "$test_memgraph" == true ]]; then stop_memgraph; fi
    exit "$status"
    ;;

  perf)
    shift
    parse_options perf "$@"
    output="$ROOT_DIR/reports/profiling/query_dependencies_perf_${preset}_depth${depth}_${query_format}_${query_mode}"
    if [[ "$query_mode" == "memgraph" || "$query_mode" == "memgraph_query_module" ]]; then
      client_output="${output}_client"
      backend_output="${output}_backend"
      use_query_modules=()
      if [[ "$query_mode" == "memgraph_query_module" ]]; then
        use_query_modules=(--use-query-modules)
      fi
      set +e
      start_memgraph "$preset" "${backend_output}.data"
      perf_run "$client_output" "$RELWITHDEBINFO_DIR/bin/query_dependencies_profiling" \
        --data-directory "$PRESET_DATA_DIR/xpgraph/$preset" \
        --test-memgraph \
        --host 127.0.0.1 \
        --port "$(preset_bolt_port "$preset")" \
        --depth "$depth" \
        --format "$query_format" \
        "${use_query_modules[@]}" \
        --test-time "$test_time"
      client_status=$?
      if kill -0 "$memgraph_pid" 2>/dev/null; then
        kill -s INT -- "-$memgraph_pid" 2>/dev/null
      fi
      wait "$memgraph_pid"
      backend_status=$?
      memgraph_pid=""
      if [[ -f "${client_output}.data" ]]; then
        generate_flamegraph "$client_output"
      fi
      if [[ -f "${backend_output}.data" ]]; then
        generate_flamegraph "$backend_output"
      fi
      set -e
      if ((client_status != 0)); then exit "$client_status"; fi
      if ((backend_status != 0 && backend_status != 130)); then
        echo "Backend perf exited with status $backend_status after writing ${backend_output}.data." >&2
        exit "$backend_status"
      fi
      exit 0
    fi
    use_gpu=()
    if [[ "$query_mode" == "gpu" ]]; then use_gpu=(--use-gpu); fi
    set +e
    perf_run "$output" "$RELWITHDEBINFO_DIR/bin/query_dependencies_profiling" \
      --data-directory "$PRESET_DATA_DIR/xpgraph/$preset" \
      --depth "$depth" \
      --format "$query_format" \
      "${use_gpu[@]}" \
      --test-time "$test_time"
    status=$?
    if [[ -f "${output}.data" ]]; then
      generate_flamegraph "$output"
    fi
    set -e
    exit "$status"
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
