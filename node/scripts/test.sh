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
               Only works on a Pi 5 with both installed.
  -h, --help   show this help

Without --pi only the hardware-independent tests are built
(-DPI5LA_TESTS_ONLY=ON), in node/capture/build-container-tests/ inside the dev
container and node/capture/build-host-tests/ elsewhere, so the two never share
a CMake cache. The repo's .venv/ (setup_host.sh) is used when it exists.

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
if REPO_ROOT="$(find_repo_root "$NODE_ROOT")" && [[ -d "$REPO_ROOT/.venv/bin" ]]; then
    PATH="$REPO_ROOT/.venv/bin:$PATH"
fi
command -v cmake >/dev/null 2>&1 || die "cmake is missing: run node/scripts/setup_host.sh, or use the dev container"

if ((is_pi_build)); then
    build_dir="$CAPTURE_DIR/build-pi-tests"
    cmake_args=(-DPI5LA_TESTS_ONLY=OFF)
elif is_dev_container; then
    build_dir="$CAPTURE_DIR/build-container-tests"
    cmake_args=(-DPI5LA_TESTS_ONLY=ON)
else
    build_dir="$CAPTURE_DIR/build-host-tests"
    cmake_args=(-DPI5LA_TESTS_ONLY=ON)
fi
reset_stale_build_dir "$build_dir" "$CAPTURE_DIR"
job_count="$(nproc 2>/dev/null || echo 2)"

# Forget the cached python3, so a .venv created since the last run is used.
cmake_args+=(-UPI5LA_PYTHON3 -UPython3_EXECUTABLE)

log_info "==> configure $build_dir"
cmake -S "$CAPTURE_DIR" -B "$build_dir" "${cmake_args[@]}"
log_info "==> build"
cmake --build "$build_dir" -j "$job_count"

# count_tests <ctest selection args...>: print how many tests match.
count_tests() {
    (cd "$build_dir" && ctest -N "$@") | sed -n 's/^Total Tests: *//p'
}

INTEGRATION_LABEL='^integration$'
ctest_args=(--output-on-failure)
case "$test_level" in
unit) ctest_args+=(-LE "$INTEGRATION_LABEL" --no-tests=error) ;;
integration)
    if [[ "$(count_tests -L "$INTEGRATION_LABEL")" == "0" ]]; then
        log_info "no integration tests yet"
        exit 0
    fi
    ctest_args+=(-L "$INTEGRATION_LABEL")
    ;;
all) ctest_args+=(--no-tests=error) ;;
esac

log_info "==> ctest ($test_level)"
cd "$build_dir"
ctest "${ctest_args[@]}"
