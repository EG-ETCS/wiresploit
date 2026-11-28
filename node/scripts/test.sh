#!/usr/bin/env bash
# Build and run the Node tests with CMake and ctest.
#
# Every phase gate runs this. It works on any Linux machine with the host
# packages (setup_host.sh) and in the dev container (node/docker/Dockerfile).
# shellcheck source-path=SCRIPTDIR
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd -P)"
# SC1091: plain `shellcheck` does not follow it; `shellcheck -x` does.
# shellcheck source=lib/config.sh disable=SC1091
source "$SCRIPT_DIR/lib/config.sh"

show_usage() {
    cat <<'EOF'
Usage: node/scripts/test.sh [unit|integration|all] [--pi]

Configures and builds node/capture, then runs ctest --output-on-failure.

  unit         every test not labelled "integration"
  integration  the tests labelled "integration" (sample captures, no Pi)
  all          every test (the default)

Options:
  --pi         full build with piolib and pioasm, in node/capture/build-pi-tests/.
               Only works on a Pi 5 with both installed. Without it the
               hardware-independent tests are built in
               node/capture/build-host-tests/ (-DPI5LA_TESTS_ONLY=ON).
  -h, --help   show this help

Exit status is non-zero when a build or test fails, or when unit/all ran no
tests at all.
EOF
}

test_level="all"
is_pi_build=0
while (($#)); do
    case "$1" in
    unit | integration | all) test_level="$1" ;;
    --pi) is_pi_build=1 ;;
    -h | --help)
        show_usage
        exit 0
        ;;
    *)
        show_usage >&2
        die "unknown argument: $1"
        ;;
    esac
    shift
done

NODE_ROOT="$(find_node_root "$SCRIPT_DIR")" || die "can't find the Node folder (node.conf.example) above $SCRIPT_DIR"
CAPTURE_DIR="$NODE_ROOT/capture"
[[ -f "$CAPTURE_DIR/CMakeLists.txt" ]] || die "no $CAPTURE_DIR/CMakeLists.txt: run this from a repo checkout"
command -v cmake >/dev/null 2>&1 || die "cmake is missing: run node/scripts/setup_host.sh, or use the dev container"

if ((is_pi_build)); then
    build_dir="$CAPTURE_DIR/build-pi-tests"
    cmake_args=(-DPI5LA_TESTS_ONLY=OFF)
else
    build_dir="$CAPTURE_DIR/build-host-tests"
    cmake_args=(-DPI5LA_TESTS_ONLY=ON)
fi
job_count="$(nproc 2>/dev/null || echo 2)"

log_info "==> configure $build_dir"
cmake -S "$CAPTURE_DIR" -B "$build_dir" "${cmake_args[@]}"
log_info "==> build"
cmake --build "$build_dir" -j "$job_count"

# count_tests <ctest selection args...>: print how many tests match.
count_tests() {
    (cd "$build_dir" && ctest -N "$@") | sed -n 's/^Total Tests: *//p'
}

ctest_args=(--output-on-failure)
case "$test_level" in
unit) ctest_args+=(-LE integration --no-tests=error) ;;
integration)
    if [[ "$(count_tests -L integration)" == "0" ]]; then
        log_info "no integration tests yet"
        exit 0
    fi
    ctest_args+=(-L integration)
    ;;
all) ctest_args+=(--no-tests=error) ;;
esac

log_info "==> ctest ($test_level)"
cd "$build_dir"
ctest "${ctest_args[@]}"
