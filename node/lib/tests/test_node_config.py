"""Unit tests for lib/node_config.py. Run: python3 -m pytest node/lib/tests"""

import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

import node_config  # noqa: E402


def make_node(root, conf_text=None):
    """Create a minimal Node folder: the root marker, plus node.conf if given."""
    os.makedirs(os.path.join(root, "bin"), exist_ok=True)
    with open(os.path.join(root, node_config.ROOT_MARKER_FILE_NAME), "w") as f:
        f.write("# marker\n")
    if conf_text is not None:
        with open(os.path.join(root, node_config.CONFIG_FILE_NAME), "w") as f:
            f.write(conf_text)
    return root


class TestParse(unittest.TestCase):
    def test_comments_blanks_and_quotes(self):
        text = "# comment\n\nA=1\nB = 'two words'\nC=\"x=y\"\nnot a setting\nD=\n"
        self.assertEqual(node_config.parse_config_text(text),
                         {"A": "1", "B": "two words", "C": "x=y", "D": ""})


class TestRoot(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_found_from_nested_file(self):
        root = make_node(os.path.join(self.tmp, "node"))
        deep = os.path.join(root, "capture", "build")
        os.makedirs(deep)
        self.assertEqual(node_config.find_node_root(os.path.join(deep, "sr2es")), root)

    def test_missing_marker_raises(self):
        with self.assertRaises(node_config.NodeRootNotFoundError):
            node_config.find_node_root(self.tmp)


class TestPrecedence(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.root = make_node(self.tmp, "WEB_UI_PORT=8000\nNODE_NAME=bench-1\n")

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_default_then_file_then_environment(self):
        config = node_config.load_config(self.root, environ={"WEB_UI_PORT": "7000"})
        self.assertEqual(config.get("SIGROK_CLI"), "sigrok-cli")   # default
        self.assertEqual(config.get("NODE_NAME"), "bench-1")       # node.conf
        self.assertEqual(config.get("WEB_UI_PORT"), "7000")        # environment wins

    def test_unknown_environment_keys_ignored(self):
        config = node_config.load_config(self.root, environ={"HOME": "/elsewhere"})
        self.assertEqual(config.get("HOME"), "")

    def test_no_conf_file_uses_defaults(self):
        os.remove(os.path.join(self.root, node_config.CONFIG_FILE_NAME))
        config = node_config.load_config(self.root, environ={})
        self.assertEqual(config.get("WEB_UI_PORT"), "9090")


class TestPaths(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_relative_paths_follow_the_folder(self):
        """Copying the Node elsewhere moves every default path with it."""
        first = make_node(os.path.join(self.tmp, "a"), "CAPTURES_DIR=captures\n")
        moved = os.path.join(self.tmp, "somewhere", "else")
        shutil.copytree(first, moved)
        config = node_config.load_config(moved, environ={})
        self.assertEqual(config.get_path("CAPTURES_DIR"), os.path.join(moved, "captures"))

    def test_absolute_path_kept(self):
        root = make_node(self.tmp, "CAPTURES_DIR=/data/captures\n")
        config = node_config.load_config(root, environ={})
        self.assertEqual(config.get_path("CAPTURES_DIR"), "/data/captures")

    def test_pi5la_bin_prefers_installed_then_repo_build(self):
        root = make_node(self.tmp)
        config = node_config.load_config(root, environ={})
        self.assertEqual(config.get_pi5la_bin(), os.path.join(root, "bin", "pi5la"))
        build = os.path.join(root, "capture", "build")
        os.makedirs(build)
        open(os.path.join(build, "pi5la"), "w").close()
        self.assertEqual(config.get_pi5la_bin(), os.path.join(build, "pi5la"))
        open(os.path.join(root, "bin", "pi5la"), "w").close()
        self.assertEqual(config.get_pi5la_bin(), os.path.join(root, "bin", "pi5la"))


class TestCredentials(unittest.TestCase):
    def test_exported_only_where_unset(self):
        tmp = tempfile.mkdtemp()
        try:
            root = make_node(tmp, "ES_API_KEY=from-file\nES_USER=file-user\n")
            environ = {"ES_USER": "env-user"}
            node_config.load_config(root, environ=environ).export_es_credentials(environ)
            self.assertEqual(environ["ES_API_KEY"], "from-file")
            self.assertEqual(environ["ES_USER"], "env-user")
            self.assertNotIn("ES_PASSWORD", environ)
        finally:
            shutil.rmtree(tmp)


if __name__ == "__main__":
    unittest.main()
