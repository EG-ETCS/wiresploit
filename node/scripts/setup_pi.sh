#!/usr/bin/env bash
# Prepare a Raspberry Pi 5 to run the Wiresploit Node from its install folder.
#
# Run it on the Pi, from the folder deploy_to_pi.sh copied:
#   <install_dir>/scripts/setup_pi.sh
# It installs packages and collects settings into <install_dir>/node.conf. It
# builds nothing: the binaries arrive ready-made in <install_dir>/bin/.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd -P)"
# shellcheck source=lib/config.sh
source "$SCRIPT_DIR/lib/config.sh"

PI_PACKAGES=(libzip4 sigrok-cli python3 python3-flask whiptail)
PIO_DEVICE="${PI5LA_PIO_DEVICE:-/dev/pio0}"
GPIO_GROUP="gpio"
UART_CONFIG_LINE="dtparam=uart0=on"
# Settings asked for on screen; everything else in node.conf is left alone.
DIALOG_KEYS=(NODE_NAME WEB_UI_HOST WEB_UI_PORT ES_URL ES_API_KEY CORE_HOST CORE_PORT)
SECRET_KEYS=(ES_API_KEY ES_PASSWORD)

show_usage() {
    cat <<'EOF'
Usage: <install_dir>/scripts/setup_pi.sh [--config FILE] [--check] [--simulate]

Runs on the Pi, from the Node install folder:
  1. installs libzip4 sigrok-cli python3 python3-flask whiptail with apt
     (shows the command and asks before running sudo apt-get)
  2. checks /dev/pio0 and that you are in the gpio group (offers to add you)
  3. asks for each setting in a dialog, pre-filled with the current value or
     the default, shows a summary, then writes node.conf (mode 600)
  4. optionally enables UART0 on GPIO14/15 in config.txt, for the loopback test
  5. prints the hardware-safety rules

Running it again is safe: it starts from the settings already in node.conf.

Options:
  --config FILE  take the settings from FILE (KEY=value, like node.conf.example)
                 instead of dialogs. Keys not in FILE keep their current value.
                 Without a terminal, root steps are printed, not run.
  --check        only report what is missing; change nothing. Exit status 1
                 when something is missing.
  --simulate     change nothing on the system: apt runs with -s (simulate), the
                 gpio group and config.txt changes are printed only. node.conf
                 is still written. For testing this script off a Pi.
  -h, --help     show this help

Environment (for testing): PI5LA_PIO_DEVICE (default /dev/pio0),
PI_CONFIG_TXT (default /boot/firmware/config.txt, else /boot/config.txt).
EOF
}

config_source=""
is_check_only=0
is_simulation=0
while (($#)); do
    case "$1" in
    --config)
        (($# >= 2)) || die "--config needs a file"
        config_source="$2"
        shift
        ;;
    --check) is_check_only=1 ;;
    --simulate) is_simulation=1 ;;
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
CONF_FILE="$NODE_ROOT/$NODE_CONF_FILE_NAME"
EXAMPLE_FILE="$NODE_ROOT/$NODE_ROOT_MARKER_FILE_NAME"
if [[ -n "${PI_CONFIG_TXT:-}" ]]; then
    CONFIG_TXT="$PI_CONFIG_TXT"
elif [[ -f /boot/firmware/config.txt ]]; then
    CONFIG_TXT=/boot/firmware/config.txt
else
    CONFIG_TXT=/boot/config.txt
fi
TARGET_USER="${SUDO_USER:-$(id -un)}"

if [[ -n "$config_source" && ! -f "$config_source" ]]; then
    die "--config: no such file: $config_source"
fi
if ((!is_check_only)) && [[ -z "$config_source" ]] && ! has_tty; then
    die "no terminal for the settings dialogs: run this in a terminal (ssh -t), or pass --config FILE"
fi

is_secret_key() {
    local key="$1" secret
    for secret in "${SECRET_KEYS[@]}"; do
        [[ "$key" == "$secret" ]] && return 0
    done
    return 1
}

is_dialog_key() {
    local key="$1" known
    for known in "${DIALOG_KEYS[@]}"; do
        [[ "$key" == "$known" ]] && return 0
    done
    return 1
}

# is_port <value>: true for 1..65535.
is_port() {
    [[ "$1" =~ ^[0-9]{1,5}$ ]] && (($1 >= 1 && $1 <= 65535))
}

# format_setting <key> <value>: print a setting for a summary, secrets masked.
format_setting() {
    local key="$1" value="$2"
    if is_secret_key "$key"; then
        if [[ -n "$value" ]]; then
            value="(set, hidden)"
        else
            value="(not set)"
        fi
    elif [[ -z "$value" ]]; then
        value="(empty)"
    fi
    printf '  %-12s %s\n' "$key" "$value"
}

# find_missing_packages: print the PI_PACKAGES that are not installed.
find_missing_packages() {
    local package
    for package in "${PI_PACKAGES[@]}"; do
        is_package_installed "$package" || printf '%s\n' "$package"
    done
}

is_in_gpio_group() {
    id -nG "$TARGET_USER" 2>/dev/null | tr ' ' '\n' | grep -qx "$GPIO_GROUP"
}

is_uart_enabled() {
    [[ -f "$CONFIG_TXT" ]] && grep -Eq "^[[:space:]]*${UART_CONFIG_LINE}[[:space:]]*$" "$CONFIG_TXT"
}

print_safety_warning() {
    cat <<'EOF'

==================== HARDWARE SAFETY: READ BEFORE WIRING ====================
 Pi 5 GPIO is 3.3 V and NOT 5 V tolerant. Overvoltage destroys the RP1 chip,
 which is soldered to the board: the whole Pi is lost.
 - An external target needs a 74LVC245 buffer, series resistors and a shared
   ground between the target and the Pi.
 - The only safe bare-wire test is a loopback jumper between the Pi's own
   header pins (for example GPIO 20 -> GPIO 2).
=============================================================================
EOF
}

# ------------------------------------------------------------ --check

run_check() {
    local problem_count=0 missing key value mode
    log_info "Node folder: $NODE_ROOT"

    missing="$(find_missing_packages | tr '\n' ' ')"
    if [[ -n "$missing" ]]; then
        log_info "[missing] packages: $missing"
        problem_count=$((problem_count + 1))
    else
        log_info "[ok]      packages: ${PI_PACKAGES[*]}"
    fi

    if [[ -e "$PIO_DEVICE" ]]; then
        log_info "[ok]      $PIO_DEVICE: $(ls -l "$PIO_DEVICE")"
    else
        log_info "[missing] $PIO_DEVICE: the rp1-pio driver is not loaded (Pi 5, kernel 6.18+)"
        problem_count=$((problem_count + 1))
    fi

    if is_in_gpio_group; then
        log_info "[ok]      $TARGET_USER is in group $GPIO_GROUP"
    else
        log_info "[missing] $TARGET_USER is not in group $GPIO_GROUP"
        problem_count=$((problem_count + 1))
    fi

    if [[ -f "$CONF_FILE" ]]; then
        mode="$(stat -c %a "$CONF_FILE")"
        if [[ "$mode" == "600" ]]; then
            log_info "[ok]      $CONF_FILE (mode 600):"
        else
            log_info "[unsafe]  $CONF_FILE has mode $mode, expected 600:"
            problem_count=$((problem_count + 1))
        fi
        for key in "${DIALOG_KEYS[@]}"; do
            value="$(read_conf_value "$CONF_FILE" "$key")"
            format_setting "$key" "$value"
        done
    else
        log_info "[missing] $CONF_FILE: run setup_pi.sh to create it"
        problem_count=$((problem_count + 1))
    fi

    if is_uart_enabled; then
        log_info "[ok]      UART0 on GPIO14/15 enabled in $CONFIG_TXT"
    elif [[ -f "$CONFIG_TXT" ]]; then
        log_info "[info]    UART0 not enabled in $CONFIG_TXT (only needed for the UART loopback test)"
    else
        log_info "[info]    no $CONFIG_TXT (not a Pi?)"
    fi

    print_safety_warning
    ((problem_count == 0))
}

if ((is_check_only)); then
    run_check
    exit
fi

# ------------------------------------------------------ 1. packages

log_info "==> 1/5 packages: ${PI_PACKAGES[*]}"
if ! command -v dpkg-query >/dev/null 2>&1 || ! command -v apt-get >/dev/null 2>&1; then
    log_warn "apt is not available: install these yourself: ${PI_PACKAGES[*]}"
else
    mapfile -t missing_packages < <(find_missing_packages)
    if ((${#missing_packages[@]} == 0)); then
        log_info "All installed."
    elif ((is_simulation)); then
        log_info "Missing: ${missing_packages[*]}. Simulating the install:"
        log_info "    $(format_command apt-get install -s "${missing_packages[@]}")"
        apt-get install -s "${missing_packages[@]}" ||
            log_warn "apt-get simulation failed (no package lists? run apt-get update)"
    else
        log_info "Missing: ${missing_packages[*]}"
        if confirm_privileged "apt-get update" \
            "$(format_command apt-get install -y "${missing_packages[@]}")"; then
            run_as_root apt-get update
            run_as_root apt-get install -y "${missing_packages[@]}"
        fi
    fi
fi

# ---------------------------------------------------- 2. pio access

log_info "==> 2/5 PIO access"
if [[ -e "$PIO_DEVICE" ]]; then
    log_info "$PIO_DEVICE: $(ls -l "$PIO_DEVICE")"
else
    log_warn "$PIO_DEVICE does not exist: the rp1-pio driver is not loaded. pi5la needs a"
    log_warn "Pi 5 with kernel 6.18+: update with 'sudo apt full-upgrade' and reboot."
fi
if [[ "$TARGET_USER" == "root" ]]; then
    log_warn "running as root: pi5la should run as a normal user in group $GPIO_GROUP."
elif is_in_gpio_group; then
    log_info "$TARGET_USER is in group $GPIO_GROUP."
elif ! getent group "$GPIO_GROUP" >/dev/null 2>&1; then
    log_warn "group $GPIO_GROUP does not exist on this system."
else
    log_info "$TARGET_USER is not in group $GPIO_GROUP, so pi5la can't open $PIO_DEVICE."
    group_command=(usermod -aG "$GPIO_GROUP" "$TARGET_USER")
    if ((is_simulation)); then
        log_info "Simulation: would ask to run: sudo $(format_command "${group_command[@]}")"
    elif confirm_privileged "$(format_command "${group_command[@]}")"; then
        run_as_root "${group_command[@]}"
        log_info "Added. Log out and back in (or reboot) for it to take effect."
    fi
fi

# ----------------------------------------------------- 3. settings

log_info "==> 3/5 settings ($CONF_FILE)"

declare -A settings=()
for key in "${DIALOG_KEYS[@]}"; do
    if has_conf_key "$CONF_FILE" "$key"; then
        settings[$key]="$(read_conf_value "$CONF_FILE" "$key")"
    else
        settings[$key]="$(read_conf_value "$EXAMPLE_FILE" "$key")"
    fi
done

# Keys from --config that aren't asked for on screen (e.g. ES_USER), still
# limited to the keys node.conf.example documents.
declare -A extra_settings=()

# ask_setting <key> <prompt>: show one input dialog for <key>, pre-filled.
ask_setting() {
    local key="$1" prompt="$2" answer
    answer="$(ask_input "$prompt" "${settings[$key]}")" || die "cancelled: node.conf not changed"
    settings[$key]="$(trim_space "$answer")"
}

# ask_port_setting <key> <prompt> [optional]: like ask_setting, until the
# answer is a valid port (or blank when optional).
ask_port_setting() {
    local key="$1" prompt="$2" is_optional="${3:-}"
    while true; do
        ask_setting "$key" "$prompt"
        [[ -n "$is_optional" && -z "${settings[$key]}" ]] && return
        is_port "${settings[$key]}" && return
        show_message "${settings[$key]:-(empty)} is not a port number (1-65535)."
    done
}

ask_settings() {
    local answer
    ask_setting NODE_NAME "Node name, shown in captures and logs:"
    ask_setting WEB_UI_HOST "Capture test page (web_ui) bind address. It has no login: 127.0.0.1 keeps it on this Pi; 0.0.0.0 opens it to the network."
    ask_port_setting WEB_UI_PORT "Capture test page (web_ui) port:"
    ask_setting ES_URL "Elasticsearch URL, e.g. https://es.example:9200. Leave empty to only write NDJSON files."
    if [[ -n "${settings[ES_URL]}" ]]; then
        local key_state="No API key is set."
        [[ -n "${settings[ES_API_KEY]}" ]] && key_state="An API key is set: leave blank to keep it, or type - to remove it."
        answer="$(ask_password "Elasticsearch API key. $key_state")" || die "cancelled: node.conf not changed"
        if [[ "$answer" == "-" ]]; then
            settings[ES_API_KEY]=""
        elif [[ -n "$answer" ]]; then
            settings[ES_API_KEY]="$answer"
        fi
    fi
    ask_setting CORE_HOST "Wiresploit Core address (hostname or IP). Optional: leave empty for now."
    ask_port_setting CORE_PORT "Wiresploit Core port. Optional: leave empty for now." optional
}

# read_settings_file <file>: take the documented keys from <file>.
read_settings_file() {
    local file="$1" key value
    while IFS= read -r key; do
        value="$(read_conf_value "$file" "$key")"
        if is_dialog_key "$key"; then
            settings[$key]="$value"
        elif has_conf_key "$EXAMPLE_FILE" "$key"; then
            extra_settings[$key]="$value"
        else
            log_warn "$file: unknown setting $key ignored (see node.conf.example)"
        fi
    done < <(list_conf_keys "$file")
    is_port "${settings[WEB_UI_PORT]}" || die "$file: WEB_UI_PORT=${settings[WEB_UI_PORT]} is not a port number"
    [[ -z "${settings[CORE_PORT]}" ]] || is_port "${settings[CORE_PORT]}" ||
        die "$file: CORE_PORT=${settings[CORE_PORT]} is not a port number"
}

format_summary() {
    local key
    for key in "${DIALOG_KEYS[@]}"; do
        format_setting "$key" "${settings[$key]}"
    done
    for key in "${!extra_settings[@]}"; do
        format_setting "$key" "${extra_settings[$key]}"
    done
}

if [[ -n "$config_source" ]]; then
    read_settings_file "$config_source"
    log_info "Settings from $config_source:"
    format_summary
else
    ask_settings
    ask_yes_no "Write these settings to $CONF_FILE?

$(format_summary)" yes || die "cancelled: node.conf not changed"
fi

if [[ ! -f "$CONF_FILE" ]]; then
    # Start from the example, so node.conf keeps the comments for each key.
    (umask 077 && cp "$EXAMPLE_FILE" "$CONF_FILE")
fi
chmod 600 "$CONF_FILE"
for key in "${DIALOG_KEYS[@]}"; do
    write_conf_value "$CONF_FILE" "$key" "${settings[$key]}"
done
for key in "${!extra_settings[@]}"; do
    write_conf_value "$CONF_FILE" "$key" "${extra_settings[$key]}"
done
log_info "Wrote $CONF_FILE (mode $(stat -c %a "$CONF_FILE"))."

captures_dir="$(read_conf_value "$CONF_FILE" CAPTURES_DIR captures)"
[[ "$captures_dir" == /* ]] || captures_dir="$NODE_ROOT/$captures_dir"
mkdir -p "$captures_dir"
log_info "Captures go to $captures_dir."

# ------------------------------------------------------ 4. uart

log_info "==> 4/5 UART0 on GPIO14/15 (UART loopback test only)"
if is_uart_enabled; then
    log_info "Already enabled in $CONFIG_TXT."
elif [[ ! -f "$CONFIG_TXT" ]]; then
    log_info "No $CONFIG_TXT (not a Pi?): skipped."
elif [[ -n "$config_source" ]] && ! has_tty; then
    log_info "Skipped. To enable it, add '$UART_CONFIG_LINE' to $CONFIG_TXT and reboot."
elif ask_yes_no "Enable UART0 on GPIO14 (TX) / GPIO15 (RX) for the UART loopback test?

This adds '$UART_CONFIG_LINE' to $CONFIG_TXT. It takes effect after a reboot." no; then
    if ((is_simulation)); then
        log_info "Simulation: would ask to append '$UART_CONFIG_LINE' to $CONFIG_TXT"
    elif confirm_privileged "$(format_command tee -a "$CONFIG_TXT") <<< '$UART_CONFIG_LINE'"; then
        printf '\n# Added by setup_pi.sh: UART0 on GPIO14/15 for the pi5la loopback test\n%s\n' \
            "$UART_CONFIG_LINE" | run_as_root tee -a "$CONFIG_TXT" >/dev/null
        log_info "Added. Reboot for it to take effect: sudo reboot"
    fi
else
    log_info "Skipped."
fi

# ------------------------------------------------------ 5. safety

log_info "==> 5/5 hardware safety"
print_safety_warning
log_info "Done. Check anytime with: $SCRIPT_DIR/setup_pi.sh --check"
