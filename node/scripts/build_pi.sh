#!/usr/bin/env bash
# Cross-compile the Pi 5 (aarch64) binaries into node/capture/build-arm64/.
#
# Run it in the dev container (node/docker/Dockerfile), which has the arm64
# cross compiler, arm64 libzip, piolib in /opt/arm64-prefix and pioasm.
# deploy_to_pi.sh ships the result.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd -P)"
# shellcheck source=lib/config.sh
source "$SCRIPT_DIR/lib/config.sh"

show_usage() {
    cat <<'EOF'
Usage: node/scripts/build_pi.sh [--clean]

Cross-compiles the full pi5la build (pi5la, ratesweep, bitprobe, fdebugtest)
for the Raspberry Pi 5 into node/capture/build-arm64/, using
node/capture/cmake/aarch64-linux-gnu.cmake, then checks with `file` that the
binaries are ARM aarch64.

Options:
  --clean      delete node/capture/build-arm64/ first
  -h, --help   show this help

Environment:
  PI5LA_ARM64_PREFIX  where the arm64 piolib is installed (default /opt/arm64-prefix)
  PIOASM_EXECUTABLE   pioasm to use (default: pioasm on PATH)
EOF
}

is_clean=0
while (($#)); do
    case "$1" in
    --clean) is_clean=1 ;;
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
BUILD_DIR="$CAPTURE_DIR/build-arm64"
TOOLCHAIN_FILE="$CAPTURE_DIR/cmake/aarch64-linux-gnu.cmake"
ARM64_PREFIX="${PI5LA_ARM64_PREFIX:-/opt/arm64-prefix}"
PI_BINARIES=(pi5la ratesweep bitprobe fdebugtest)

# Check the cross toolchain first, with a hint instead of a CMake error.
missing=()
command -v cmake >/dev/null 2>&1 || missing+=("cmake")
command -v file >/dev/null 2>&1 || missing+=("file")
command -v aarch64-linux-gnu-gcc >/dev/null 2>&1 || missing+=("aarch64-linux-gnu-gcc (crossbuild-essential-arm64)")
pioasm="${PIOASM_EXECUTABLE:-$(command -v pioasm || true)}"
[[ -n "$pioasm" && -x "$pioasm" ]] || missing+=("pioasm (pico-sdk tools/pioasm)")
[[ -f "$ARM64_PREFIX/include/piolib/piolib.h" ]] || missing+=("arm64 piolib in $ARM64_PREFIX")
[[ -f /usr/lib/aarch64-linux-gnu/pkgconfig/libzip.pc ]] || missing+=("libzip-dev:arm64")
if ((${#missing[@]})); then
    printf '  - %s\n' "${missing[@]}" >&2
    die "the cross toolchain is incomplete (above). Run this in the dev container: see node/docker/Dockerfile."
fi

if ((is_clean)); then
    log_info "==> remove $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

log_info "==> configure $BUILD_DIR"
cmake -S "$CAPTURE_DIR" -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
    -DPI5LA_ARM64_PREFIX="$ARM64_PREFIX" \
    -DPIOASM_EXECUTABLE="$pioasm" \
    -DPI5LA_TESTS_ONLY=OFF
log_info "==> build"
cmake --build "$BUILD_DIR" -j "$(nproc 2>/dev/null || echo 2)"

log_info "==> check the binaries are for the Pi"
for binary in "${PI_BINARIES[@]}"; do
    description="$(file -b "$BUILD_DIR/$binary")"
    log_info "$binary: $description"
    [[ "$description" == *"ARM aarch64"* ]] || die "$BUILD_DIR/$binary is not an ARM aarch64 binary"
done
log_info "OK: Pi binaries in $BUILD_DIR. Ship them with node/scripts/deploy_to_pi.sh."
