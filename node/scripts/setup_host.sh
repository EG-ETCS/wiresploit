#!/usr/bin/env bash
# Set up a Debian/Ubuntu laptop to build, test and document the Node without
# Docker: the build packages, plus a Python venv in .venv/ at the repo root.
#
# The dev container (node/docker/Dockerfile) is the easier path and is the only
# one that cross-compiles for the Pi (build_pi.sh).
# shellcheck source-path=SCRIPTDIR
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd -P)"
# SC1091: plain `shellcheck` does not follow it; `shellcheck -x` does.
# shellcheck source=lib/config.sh disable=SC1091
source "$SCRIPT_DIR/lib/config.sh"

# Same as node/docker/Dockerfile, minus the arm64 cross-compile packages: those
# need `dpkg --add-architecture arm64` plus a piolib built from source, which
# only the container sets up. Keep the two lists in sync.
HOST_PACKAGES=(
    build-essential cmake pkg-config libzip-dev
    python3 python3-venv
    sigrok-cli doxygen shellcheck whiptail git rsync openssh-client file
)

show_usage() {
    cat <<'EOF'
Usage: node/scripts/setup_host.sh [--check]

Installs what the Node needs on a laptop without Docker:
  1. Debian packages: build-essential cmake pkg-config libzip-dev python3
     python3-venv sigrok-cli doxygen shellcheck whiptail git rsync
     openssh-client file. Shows the apt command and asks before running it.
  2. A Python venv in .venv/ at the repo root with requirements-docs.txt,
     node/capture/tools/web_ui/requirements.txt and
     node/docker/requirements-dev.txt (pytest, black, ruff, flask).

Then: node/scripts/test.sh, node/scripts/docs.sh build.
Cross-compiling for the Pi (build_pi.sh) needs the dev container.

Options:
  --check      only report what is missing; change nothing
  -h, --help   show this help
EOF
}

is_check_only=0
while (($#)); do
    case "$1" in
    --check) is_check_only=1 ;;
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

refuse_sudo setup_host.sh
NODE_ROOT="$(find_node_root "$SCRIPT_DIR")" || die "can't find the Node folder (node.conf.example) above $SCRIPT_DIR"
REPO_ROOT="$(find_repo_root "$NODE_ROOT")" || die "setup_host.sh needs a repo checkout"
VENV_DIR="$REPO_ROOT/.venv"
REQUIREMENT_FILES=(
    "$REPO_ROOT/requirements-docs.txt"
    "$NODE_ROOT/capture/tools/web_ui/requirements.txt"
    "$NODE_ROOT/docker/requirements-dev.txt"
)
VENV_TOOLS=(mkdocs pytest black ruff flask)

# ----------------------------------------------------------- 1. packages

missing_packages=()
if command -v dpkg-query >/dev/null 2>&1; then
    for package in "${HOST_PACKAGES[@]}"; do
        is_package_installed "$package" || missing_packages+=("$package")
    done
    if ((${#missing_packages[@]})); then
        log_info "Missing packages: ${missing_packages[*]}"
    else
        log_info "Packages: all installed."
    fi
else
    log_warn "not a Debian-based system: install these yourself: ${HOST_PACKAGES[*]}"
fi

# --------------------------------------------------------------- 2. venv

missing_tools=()
for tool in "${VENV_TOOLS[@]}"; do
    [[ -x "$VENV_DIR/bin/$tool" ]] || missing_tools+=("$tool")
done
if [[ ! -d "$VENV_DIR" ]]; then
    log_info "Python venv: $VENV_DIR does not exist."
elif ((${#missing_tools[@]})); then
    log_info "Python venv: $VENV_DIR lacks: ${missing_tools[*]}"
else
    log_info "Python venv: $VENV_DIR is complete."
fi

if ((is_check_only)); then
    ((${#missing_packages[@]} == 0 && ${#missing_tools[@]} == 0))
    exit
fi

if ((${#missing_packages[@]})); then
    if confirm_privileged "apt-get update" \
        "$(format_command apt-get install -y "${missing_packages[@]}")"; then
        run_as_root apt-get update
        run_as_root apt-get install -y "${missing_packages[@]}"
    else
        die "install the packages above, then run this again"
    fi
fi

if ((${#missing_tools[@]})); then
    command -v python3 >/dev/null 2>&1 || die "python3 is missing"
    if [[ ! -x "$VENV_DIR/bin/python" ]]; then
        log_info "==> python3 -m venv $VENV_DIR"
        python3 -m venv "$VENV_DIR"
    fi
    pip_args=()
    for requirement_file in "${REQUIREMENT_FILES[@]}"; do
        pip_args+=(-r "$requirement_file")
    done
    log_info "==> pip install ${pip_args[*]}"
    "$VENV_DIR/bin/pip" install "${pip_args[@]}"
fi

log_info "Host ready. Next: node/scripts/test.sh, node/scripts/docs.sh build"
