"""Per-machine settings for the Wiresploit Node.

Module: node_config
Purpose: Find the Node's root folder from a program's own location and load
    ``node.conf`` from it, so nothing depends on a home folder or a fixed path.
Owner: node

The Node root is the nearest folder above a program that holds
``node.conf.example``. In the repo that is ``node/``; on a Pi it is whatever
install folder ``deploy_to_pi.sh`` copied to. Moving the folder keeps working.

A setting is taken from, highest first:

1. the command-line flag (handled by each program's argparse),
2. an environment variable of the same name,
3. ``<root>/node.conf``,
4. the built-in default in ``DEFAULTS``.

``node.conf`` is plain ``KEY=value`` lines. ``#`` starts a comment line, and a
value may be wrapped in single or double quotes. The file is parsed, never
executed, so the shell scripts read the same file the same way.
"""

import os

CONFIG_FILE_NAME = "node.conf"
ROOT_MARKER_FILE_NAME = "node.conf.example"

# Relative paths are resolved against the Node root, never against ``~``.
DEFAULTS = {
    "NODE_NAME": "wiresploit-node",
    "PI5LA_BIN": "",  # empty: the first of PI5LA_BIN_CANDIDATES that exists
    "SIGROK_CLI": "sigrok-cli",
    "CAPTURES_DIR": "captures",
    "WEB_UI_HOST": "0.0.0.0",
    "WEB_UI_PORT": "9090",
    "ES_URL": "",
    "ES_API_KEY": "",
    "ES_USER": "",
    "ES_PASSWORD": "",
    "CORE_HOST": "",
    "CORE_PORT": "",
}

ES_CREDENTIAL_KEYS = ("ES_API_KEY", "ES_USER", "ES_PASSWORD")

# Where pi5la lives: an installed Node first, then a build in the repo.
PI5LA_BIN_CANDIDATES = ("bin/pi5la", "capture/build/pi5la", "capture/build-arm64/pi5la")


class NodeRootNotFoundError(RuntimeError):
    """Raised when no folder above a program holds ``node.conf.example``."""


def find_node_root(start_path):
    """Return the Node root folder that contains ``start_path``.

    Args:
        start_path: Any file or folder inside the Node, usually ``__file__``.

    Returns:
        The absolute path of the nearest folder above ``start_path`` that
        holds ``node.conf.example``.

    Raises:
        NodeRootNotFoundError: No such folder exists up to the filesystem root.
    """
    current = os.path.abspath(start_path)
    if not os.path.isdir(current):
        current = os.path.dirname(current)
    while True:
        if os.path.isfile(os.path.join(current, ROOT_MARKER_FILE_NAME)):
            return current
        parent = os.path.dirname(current)
        if parent == current:
            raise NodeRootNotFoundError(
                "no %s above %s" % (ROOT_MARKER_FILE_NAME, start_path))
        current = parent


def parse_config_text(text):
    """Parse ``KEY=value`` lines into a dict.

    Args:
        text: The contents of a ``node.conf`` file.

    Returns:
        A dict of the keys found. Blank lines, ``#`` comments and lines
        without ``=`` are skipped; matching outer quotes are removed.
    """
    values = {}
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        key, value = key.strip(), value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "'\"":
            value = value[1:-1]
        values[key] = value
    return values


class NodeConfig:
    """The merged settings of one Node, plus helpers to resolve its paths."""

    def __init__(self, root, values):
        self.root = root
        self._values = values

    def get(self, key):
        """Return the setting ``key`` as a string ("" when unset)."""
        return self._values.get(key, "")

    def get_path(self, key):
        """Return the setting ``key`` as an absolute path under the root.

        Absolute values are returned unchanged; relative ones are joined to
        the Node root. An unset value returns "".
        """
        value = self.get(key)
        if not value:
            return ""
        return value if os.path.isabs(value) else os.path.join(self.root, value)

    def get_pi5la_bin(self):
        """Return the pi5la binary to run.

        ``PI5LA_BIN`` wins when set. Otherwise the first existing entry of
        ``PI5LA_BIN_CANDIDATES``, or the first entry when none exists yet.
        """
        if self.get("PI5LA_BIN"):
            return self.get_path("PI5LA_BIN")
        candidates = [os.path.join(self.root, c) for c in PI5LA_BIN_CANDIDATES]
        for candidate in candidates:
            if os.path.isfile(candidate):
                return candidate
        return candidates[0]

    def export_es_credentials(self, environ=None):
        """Copy Elasticsearch credentials into the environment if unset there.

        Child processes (pi5la, then the sr2es it runs) read credentials from
        the environment, so this hands them the ``node.conf`` values without
        ever overriding a variable the user exported.

        Args:
            environ: The environment to update; ``os.environ`` when omitted.
        """
        environ = os.environ if environ is None else environ
        for key in ES_CREDENTIAL_KEYS:
            if self.get(key) and not environ.get(key):
                environ[key] = self.get(key)


def load_config(start_path, environ=None):
    """Load the settings for the Node that contains ``start_path``.

    Args:
        start_path: Any file inside the Node, usually ``__file__``.
        environ: The environment to read overrides from; ``os.environ`` when
            omitted. Only keys in ``DEFAULTS`` are taken from it.

    Returns:
        A ``NodeConfig`` with defaults, then ``node.conf``, then environment
        values applied in that order.

    Raises:
        NodeRootNotFoundError: ``start_path`` is not inside a Node folder.
    """
    root = find_node_root(start_path)
    environ = os.environ if environ is None else environ
    values = dict(DEFAULTS)
    config_path = os.path.join(root, CONFIG_FILE_NAME)
    if os.path.isfile(config_path):
        with open(config_path, encoding="utf-8") as config_file:
            values.update(parse_config_text(config_file.read()))
    for key in DEFAULTS:
        if environ.get(key):
            values[key] = environ[key]
    return NodeConfig(root, values)
