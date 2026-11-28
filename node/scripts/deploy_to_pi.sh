#!/usr/bin/env bash
# Copy the Node install folder to a Raspberry Pi 5 with rsync over ssh.
#
# Build the Pi binaries first (build_pi.sh). The first run asks for the Pi's
# address, the ssh user and the install folder, and saves them in the
# gitignored node/.deploy.conf for next time.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd -P)"
# shellcheck source=lib/config.sh
source "$SCRIPT_DIR/lib/config.sh"

show_usage() {
    cat <<'EOF'
Usage: node/scripts/deploy_to_pi.sh [--dry-run] [--reconfigure]

Assembles the Node install folder and copies it to the Pi:

  <install_dir>/bin/      pi5la ratesweep bitprobe fdebugtest (from build-arm64/),
                          sr2es, uart-listen, test_pi.sh (when present)
  <install_dir>/web_ui/   capture test page (without its tests)
  <install_dir>/lib/      node_config.py
  <install_dir>/scripts/  setup_pi.sh and lib/
  <install_dir>/node.conf.example

Nothing on the Pi is deleted: its node.conf and captures/ stay as they are, and
no node.conf or credential is ever copied from this machine. Afterwards run
<install_dir>/scripts/setup_pi.sh on the Pi.

The Pi's address, ssh user and install folder are asked for in dialogs on the
first run and saved in node/.deploy.conf (PI_HOST, PI_USER, PI_INSTALL_DIR).
A relative install folder is relative to the ssh user's home folder on the Pi.
Without a terminal, node/.deploy.conf must already exist.

Options:
  --dry-run      print the staged layout and the rsync command; copy nothing
  --reconfigure  ask for the Pi's address, user and folder again
  -h, --help     show this help
EOF
}

is_dry_run=0
is_reconfigure=0
while (($#)); do
    case "$1" in
    --dry-run) is_dry_run=1 ;;
    --reconfigure) is_reconfigure=1 ;;
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
find_repo_root "$NODE_ROOT" >/dev/null || die "deploy_to_pi.sh runs from a repo checkout, not from an install folder"
DEPLOY_CONF="$NODE_ROOT/.deploy.conf"
CAPTURE_DIR="$NODE_ROOT/capture"
ARM64_BUILD_DIR="$CAPTURE_DIR/build-arm64"
PI_BINARIES=(pi5la ratesweep bitprobe fdebugtest)
DEFAULT_INSTALL_DIR="wiresploit-node"
UI_TITLE="Deploy to Pi"

# ------------------------------------------------------------- target

is_valid_host() {
    [[ "$1" =~ ^[A-Za-z0-9]([A-Za-z0-9.-]*[A-Za-z0-9])?$ ]]
}

is_valid_user() {
    [[ "$1" =~ ^[a-z_][a-z0-9_-]*$ ]]
}

is_valid_install_dir() {
    [[ -n "$1" && "$1" != *$'\n'* && "$1" != "~"* && "$1" != "/" ]]
}

pi_host="$(read_conf_value "$DEPLOY_CONF" PI_HOST)"
pi_user="$(read_conf_value "$DEPLOY_CONF" PI_USER)"
pi_install_dir="$(read_conf_value "$DEPLOY_CONF" PI_INSTALL_DIR)"

is_target_complete() {
    [[ -n "$pi_host" && -n "$pi_user" && -n "$pi_install_dir" ]]
}

ask_target() {
    local answer
    while true; do
        answer="$(ask_input "Pi address (hostname or IP):" "$pi_host")" || die "cancelled"
        pi_host="$(trim_space "$answer")"
        is_valid_host "$pi_host" && break
        show_message "'$pi_host' is not a hostname or IPv4 address."
    done
    while true; do
        answer="$(ask_input "ssh user on the Pi:" "$pi_user")" || die "cancelled"
        pi_user="$(trim_space "$answer")"
        is_valid_user "$pi_user" && break
        show_message "'$pi_user' is not a valid user name."
    done
    while true; do
        answer="$(ask_input "Install folder on the Pi. Absolute (e.g. /opt/wiresploit-node), or relative to the user's home folder:" \
            "${pi_install_dir:-$DEFAULT_INSTALL_DIR}")" || die "cancelled"
        pi_install_dir="$(trim_space "$answer")"
        pi_install_dir="${pi_install_dir%/}"
        is_valid_install_dir "$pi_install_dir" && break
        show_message "Give a folder that doesn't start with '~' (a relative path is already relative to the home folder)."
    done
    write_conf_value "$DEPLOY_CONF" PI_HOST "$pi_host"
    write_conf_value "$DEPLOY_CONF" PI_USER "$pi_user"
    write_conf_value "$DEPLOY_CONF" PI_INSTALL_DIR "$pi_install_dir"
    log_info "Saved to $DEPLOY_CONF."
}

if ((is_reconfigure)) || ! is_target_complete; then
    has_tty || die "no terminal for the dialogs: run this in a terminal once, or write $DEPLOY_CONF with PI_HOST=, PI_USER= and PI_INSTALL_DIR= lines"
    ask_target
elif has_tty; then
    if ! ask_yes_no "Deploy to the saved Pi?

  Pi:      $pi_host
  User:    $pi_user
  Folder:  $pi_install_dir

Choose No to change them." yes; then
        ask_target
    fi
else
    log_info "Using $DEPLOY_CONF (no terminal to ask in)."
fi
is_valid_host "$pi_host" || die "$DEPLOY_CONF: PI_HOST='$pi_host' is not a hostname or IPv4 address"
is_valid_user "$pi_user" || die "$DEPLOY_CONF: PI_USER='$pi_user' is not a valid user name"
is_valid_install_dir "$pi_install_dir" || die "$DEPLOY_CONF: PI_INSTALL_DIR='$pi_install_dir' is not usable"

# -------------------------------------------------------------- stage

command -v file >/dev/null 2>&1 || die "file is missing"
command -v rsync >/dev/null 2>&1 || die "rsync is missing"
for binary in "${PI_BINARIES[@]}"; do
    [[ -x "$ARM64_BUILD_DIR/$binary" ]] || die "missing $ARM64_BUILD_DIR/$binary: run node/scripts/build_pi.sh first"
    [[ "$(file -b "$ARM64_BUILD_DIR/$binary")" == *"ARM aarch64"* ]] ||
        die "$ARM64_BUILD_DIR/$binary is not an ARM aarch64 binary: run node/scripts/build_pi.sh"
done

stage_dir="$(mktemp -d)"
trap 'rm -rf "$stage_dir"' EXIT
chmod 755 "$stage_dir"

install -d -m 755 "$stage_dir/bin" "$stage_dir/lib" "$stage_dir/scripts/lib"
for binary in "${PI_BINARIES[@]}"; do
    install -m 755 "$ARM64_BUILD_DIR/$binary" "$stage_dir/bin/$binary"
done
install -m 755 "$NODE_ROOT/exporter/sr2es.py" "$stage_dir/bin/sr2es"
install -m 755 "$CAPTURE_DIR/tools/uart-listen.sh" "$stage_dir/bin/uart-listen"
if [[ -f "$CAPTURE_DIR/tools/test_pi.sh" ]]; then
    install -m 755 "$CAPTURE_DIR/tools/test_pi.sh" "$stage_dir/bin/test_pi.sh"
fi
rsync -a --chmod=Du=rwx,Dgo=rx,Fu=rw,Fgo=r \
    --exclude='tests/' --exclude='__pycache__/' --exclude='*.pyc' --exclude='.DS_Store' \
    "$CAPTURE_DIR/tools/web_ui/" "$stage_dir/web_ui/"
install -m 644 "$NODE_ROOT/lib/node_config.py" "$stage_dir/lib/node_config.py"
install -m 755 "$SCRIPT_DIR/setup_pi.sh" "$stage_dir/scripts/setup_pi.sh"
install -m 644 "$SCRIPT_DIR/lib/config.sh" "$stage_dir/scripts/lib/config.sh"
install -m 644 "$NODE_ROOT/node.conf.example" "$stage_dir/node.conf.example"

# --mkpath creates the install folder. There is no --delete, so the Pi's
# node.conf, captures/ and anything else already there are never removed. The
# excludes are a second guard: the stage never holds these files anyway.
remote="$pi_user@$pi_host:$pi_install_dir/"
rsync_command=(rsync -rlptz --mkpath --itemize-changes
    --exclude=/node.conf --exclude=/captures/ --exclude=/.deploy.conf
    "$stage_dir/" "$remote")

log_info "Staged install folder:"
(cd "$stage_dir" && find . -mindepth 1 -printf '  %M  %P\n' | sort -k2)
if ((is_dry_run)); then
    log_info ""
    log_info "Dry run: nothing copied. The copy would run (from a temporary staging folder):"
    log_info "  $(format_command "${rsync_command[@]}")"
    exit 0
fi

log_info "==> copy to $remote"
"${rsync_command[@]}"

setup_command="cd $(printf '%q' "$pi_install_dir") && ./scripts/setup_pi.sh"
log_info "Deployed. Next, on the Pi:"
log_info "  ssh -t $pi_user@$pi_host $(printf '%q' "$setup_command")"
if has_tty && ask_terminal_yes_no "Run setup_pi.sh on the Pi now?" "no"; then
    ssh -t "$pi_user@$pi_host" "$setup_command"
fi
