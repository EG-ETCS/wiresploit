# Capture test page (`web_ui`)

A small Flask page for trying pi5la captures by hand on a Pi: start and stop a
capture, watch its output, then list, download and decode the `.sr` files.

**This is a local testing tool, not the Wiresploit product UI.** Real capture
commands come from Wiresploit Core. Don't build features on top of this page.

- It has **no authentication**. Bind it to `127.0.0.1`, or keep it on a trusted
  bench network only.
- Defaults (pi5la binary, captures folder, bind address and port) come from
  `node.conf` in the Node folder. See `node/node.conf.example`. Every
  command-line flag overrides them.

```bash
python3 <install_dir>/web_ui/app.py              # uses node.conf
python3 <install_dir>/web_ui/app.py --host 127.0.0.1 --port 9090
```

Tests: `tests/test_args.py` checks the form-to-argument translation in
`args.py`, and runs on any machine.
