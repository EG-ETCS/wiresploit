#!/usr/bin/env bash
# Build or preview the documentation site (mkdocs.yml at the repo root).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd -P)"
# shellcheck source=lib/config.sh
source "$SCRIPT_DIR/lib/config.sh"

show_usage() {
    cat <<'EOF'
Usage: node/scripts/docs.sh build|serve [mkdocs options...]

  build   run Doxygen for node/capture (once node/capture/Doxyfile exists),
          then `mkdocs build` into site/
  serve   `mkdocs serve`: live preview on http://127.0.0.1:8000
          (in a `docker run` container add: --dev-addr 0.0.0.0:8000)

Extra options are passed to mkdocs. Uses the repo's .venv/ when it exists
(see setup_host.sh), otherwise the mkdocs on PATH (the dev container's).

  -h, --help   show this help
EOF
}

(($#)) || {
    show_usage >&2
    exit 1
}
docs_command="$1"
shift
case "$docs_command" in
build | serve) ;;
-h | --help)
    show_usage
    exit 0
    ;;
*)
    show_usage >&2
    die "unknown command: $docs_command"
    ;;
esac

NODE_ROOT="$(find_node_root "$SCRIPT_DIR")" || die "can't find the Node folder (node.conf.example) above $SCRIPT_DIR"
REPO_ROOT="$(find_repo_root "$NODE_ROOT")" || die "docs.sh needs a repo checkout (mkdocs.yml)"

if [[ -x "$REPO_ROOT/.venv/bin/mkdocs" ]]; then
    PATH="$REPO_ROOT/.venv/bin:$PATH"
fi
command -v mkdocs >/dev/null 2>&1 || die "mkdocs is missing: run node/scripts/setup_host.sh, or use the dev container"

cd "$REPO_ROOT"
if [[ "$docs_command" == "build" ]]; then
    DOXYFILE_DIR="$NODE_ROOT/capture"
    if [[ -f "$DOXYFILE_DIR/Doxyfile" ]]; then
        command -v doxygen >/dev/null 2>&1 || die "doxygen is missing: run node/scripts/setup_host.sh, or use the dev container"
        log_info "==> doxygen $DOXYFILE_DIR/Doxyfile"
        (cd "$DOXYFILE_DIR" && doxygen Doxyfile)
    fi
    log_info "==> mkdocs build"
    exec mkdocs build "$@"
fi
exec mkdocs serve "$@"
