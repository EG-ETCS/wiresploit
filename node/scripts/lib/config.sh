# shellcheck shell=bash
# Constants here are used by the scripts that source this file.
# shellcheck disable=SC2034
#
# Shared helpers for the Node scripts in node/scripts/. Source this file, don't
# run it. It works the same in the repo (Node root = node/) and in an install
# folder on a Pi (Node root = that folder).
#
# - find_node_root, find_repo_root: locate folders from a script's own place.
# - read_conf_value, write_conf_value, list_conf_keys: node.conf-style files.
#   KEY=value lines, '#' comment lines, optional matching outer quotes. Files
#   are parsed, never sourced, exactly like node/lib/node_config.py does.
# - ask_input, ask_password, ask_yes_no, show_message: whiptail dialogs, with a
#   plain terminal prompt when whiptail is missing.
# - confirm_privileged, run_as_root: show root commands, ask, then run them.

NODE_ROOT_MARKER_FILE_NAME="node.conf.example"
NODE_CONF_FILE_NAME="node.conf"
UI_TITLE="${UI_TITLE:-Wiresploit Node}"

# ---------------------------------------------------------------- messages

log_info() {
    printf '%s\n' "$*"
}

log_warn() {
    printf 'warning: %s\n' "$*" >&2
}

die() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

# Print a command as one copy-pasteable, shell-quoted line.
format_command() {
    local quoted
    printf -v quoted '%q ' "$@"
    printf '%s\n' "${quoted% }"
}

# ----------------------------------------------------------------- folders

# find_node_root <path>: print the nearest folder at or above <path> that holds
# node.conf.example. Returns 1 when there is none.
find_node_root() {
    local dir="$1"
    [[ -d "$dir" ]] || dir="$(dirname "$dir")"
    dir="$(cd "$dir" && pwd -P)" || return 1
    while true; do
        if [[ -f "$dir/$NODE_ROOT_MARKER_FILE_NAME" ]]; then
            printf '%s\n' "$dir"
            return 0
        fi
        [[ "$dir" == "/" ]] && return 1
        dir="$(dirname "$dir")"
    done
}

# find_repo_root <node_root>: print the repo root (the folder holding node/).
# Returns 1 when <node_root> is an install folder rather than a checkout.
find_repo_root() {
    local parent
    parent="$(cd "$1/.." && pwd -P)" || return 1
    if [[ -f "$parent/node/capture/CMakeLists.txt" && -f "$parent/mkdocs.yml" ]]; then
        printf '%s\n' "$parent"
        return 0
    fi
    return 1
}

# ------------------------------------------------------------ config files

# trim_space <text>: print <text> without leading and trailing whitespace.
trim_space() {
    local text="$1"
    text="${text#"${text%%[![:space:]]*}"}"
    text="${text%"${text##*[![:space:]]}"}"
    printf '%s' "$text"
}

# parse_conf_line <line>: set CONF_KEY and CONF_VALUE from one KEY=value line.
# Returns 1 for blank lines, comments and lines without '='.
parse_conf_line() {
    local line
    line="$(trim_space "$1")"
    CONF_KEY=""
    CONF_VALUE=""
    [[ -z "$line" || "$line" == \#* || "$line" != *=* ]] && return 1
    CONF_KEY="$(trim_space "${line%%=*}")"
    CONF_VALUE="$(trim_space "${line#*=}")"
    if ((${#CONF_VALUE} >= 2)); then
        local first="${CONF_VALUE:0:1}" last="${CONF_VALUE: -1}"
        if [[ "$first" == "$last" && ("$first" == "'" || "$first" == '"') ]]; then
            CONF_VALUE="${CONF_VALUE:1:${#CONF_VALUE}-2}"
        fi
    fi
    return 0
}

# is_conf_key <key>: true when <key> is a valid setting name.
is_conf_key() {
    [[ "$1" =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]]
}

# has_conf_key <file> <key>: true when <file> sets <key>.
has_conf_key() {
    local file="$1" key="$2" line
    [[ -f "$file" ]] || return 1
    while IFS= read -r line || [[ -n "$line" ]]; do
        if parse_conf_line "$line" && [[ "$CONF_KEY" == "$key" ]]; then
            return 0
        fi
    done <"$file"
    return 1
}

# read_conf_value <file> <key> [default]: print the value of <key> (the last
# one wins, as in node_config.py), or <default> when the file or key is absent.
read_conf_value() {
    local file="$1" key="$2" value="${3-}" line
    if [[ -f "$file" ]]; then
        while IFS= read -r line || [[ -n "$line" ]]; do
            if parse_conf_line "$line" && [[ "$CONF_KEY" == "$key" ]]; then
                value="$CONF_VALUE"
            fi
        done <"$file"
    fi
    printf '%s' "$value"
}

# list_conf_keys <file>: print each key set in <file> once, in file order.
list_conf_keys() {
    local file="$1" line
    local -A seen=()
    [[ -f "$file" ]] || return 0
    while IFS= read -r line || [[ -n "$line" ]]; do
        if parse_conf_line "$line" && [[ -z "${seen[$CONF_KEY]-}" ]]; then
            seen[$CONF_KEY]=1
            printf '%s\n' "$CONF_KEY"
        fi
    done <"$file"
}

# format_conf_value <value>: print <value> as it must be written so that
# parse_conf_line reads it back unchanged.
format_conf_value() {
    local value="$1"
    local first="${value:0:1}" last="${value: -1}"
    if [[ "$value" != "$(trim_space "$value")" ]] ||
        { ((${#value} >= 2)) && [[ "$first" == "$last" && ("$first" == "'" || "$first" == '"') ]]; }; then
        printf '"%s"' "$value"
    else
        printf '%s' "$value"
    fi
}

# write_conf_value <file> <key> <value>: set <key> in <file>, keeping every
# other line (comments included). The file is rewritten atomically, mode 600.
write_conf_value() {
    local file="$1" key="$2" value="$3" line tmp is_written=0
    is_conf_key "$key" || die "invalid setting name: $key"
    [[ "$value" != *$'\n'* && "$value" != *$'\r'* ]] || die "$key: a value can't hold a line break"
    tmp="$(mktemp "$file.XXXXXX")" || die "can't write next to $file"
    {
        if [[ -f "$file" ]]; then
            while IFS= read -r line || [[ -n "$line" ]]; do
                if parse_conf_line "$line" && [[ "$CONF_KEY" == "$key" ]]; then
                    ((is_written)) && continue
                    printf '%s=%s\n' "$key" "$(format_conf_value "$value")"
                    is_written=1
                else
                    printf '%s\n' "$line"
                fi
            done <"$file"
        fi
        ((is_written)) || printf '%s=%s\n' "$key" "$(format_conf_value "$value")"
    } >"$tmp"
    chmod 600 "$tmp"
    mv -f "$tmp" "$file"
}

# ----------------------------------------------------------------- dialogs

# has_tty: true when stdin and stdout are a terminal, so dialogs can run.
has_tty() {
    [[ -t 0 && -t 1 ]]
}

has_whiptail() {
    command -v whiptail >/dev/null 2>&1
}

# ask_input <prompt> [default]: print the answer. Returns 1 on Cancel.
ask_input() {
    local prompt="$1" default="${2-}" answer
    if has_whiptail; then
        whiptail --title "$UI_TITLE" --inputbox "$prompt" 12 76 "$default" 3>&1 1>&2 2>&3
        return
    fi
    printf '%s\n[%s]: ' "$prompt" "$default" >&2
    IFS= read -r answer </dev/tty || return 1
    printf '%s' "${answer:-$default}"
}

# ask_password <prompt>: print the answer (hidden while typed). Returns 1 on
# Cancel.
ask_password() {
    local prompt="$1" answer
    if has_whiptail; then
        whiptail --title "$UI_TITLE" --passwordbox "$prompt" 12 76 3>&1 1>&2 2>&3
        return
    fi
    printf '%s\n: ' "$prompt" >&2
    IFS= read -r -s answer </dev/tty || return 1
    printf '\n' >&2
    printf '%s' "$answer"
}

# ask_yes_no <question> [yes|no]: true on Yes. The second argument is the
# answer picked when the user just presses Enter (default: yes).
ask_yes_no() {
    local question="$1" default="${2:-yes}" answer
    if has_whiptail; then
        if [[ "$default" == "no" ]]; then
            whiptail --title "$UI_TITLE" --defaultno --yesno "$question" 22 76
        else
            whiptail --title "$UI_TITLE" --yesno "$question" 22 76
        fi
        return
    fi
    ask_terminal_yes_no "$question" "$default"
}

# ask_terminal_yes_no <question> [yes|no]: like ask_yes_no, always as a plain
# terminal prompt, so the question stays visible in the scrollback.
ask_terminal_yes_no() {
    local question="$1" default="${2:-yes}" answer hint="[Y/n]"
    [[ "$default" == "no" ]] && hint="[y/N]"
    printf '%s %s ' "$question" "$hint" >&2
    IFS= read -r answer </dev/tty || return 1
    answer="${answer:-$default}"
    [[ "$answer" =~ ^[Yy] ]]
}

# show_message <text>: show <text> and wait for OK.
show_message() {
    if has_whiptail; then
        whiptail --title "$UI_TITLE" --msgbox "$1" 20 76
    else
        printf '%s\n' "$1"
    fi
}

# --------------------------------------------------------- root commands

# confirm_privileged <command line...>: show the commands that need root (each
# argument is one command line, e.g. from format_command) and ask once before
# they run. Never runs anything itself. Returns 1 when the user says no, or
# when there is no terminal to ask in (the commands are then printed to run by
# hand).
confirm_privileged() {
    local command_line prefix=""
    ((EUID == 0)) || prefix="sudo "
    log_info "This step needs root:"
    for command_line in "$@"; do
        log_info "    $prefix$command_line"
    done
    if ! has_tty; then
        log_warn "no terminal to ask in; skipped. Run the command(s) above yourself."
        return 1
    fi
    if ask_terminal_yes_no "Run now?" "yes"; then
        return 0
    fi
    log_info "Skipped. Run the command(s) above yourself when ready."
    return 1
}

# run_as_root <command...>: run the command as root, with sudo when needed.
# Call confirm_privileged first.
run_as_root() {
    if ((EUID == 0)); then
        "$@"
    elif command -v sudo >/dev/null 2>&1; then
        sudo "$@"
    else
        log_warn "sudo is not installed; run as root: $(format_command "$@")"
        return 1
    fi
}

# is_package_installed <package>: true when the Debian package is installed.
is_package_installed() {
    dpkg-query -W -f='${Status}' "$1" 2>/dev/null | grep -q 'install ok installed'
}
