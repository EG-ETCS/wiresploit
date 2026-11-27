#!/usr/bin/env python3
"""
Web UI for pi5la.

Runs on the Pi and drives the existing binaries: pi5la for capture and live
streaming, sigrok-cli for decoding. No authentication by design, so keep it on
a trusted network (see README, "Web UI").

    python3 app.py --pi5la ~/build-pi5la/pi5la --captures ~/captures

Only one job runs at a time, because a capture holds /dev/pio0. Child
processes are started with argument lists (never a shell) built by args.py, in
their own session, so a stop can signal the whole pipeline.
"""

import argparse
import glob
import json
import os
import queue
import signal
import subprocess
import threading
import time

from flask import Flask, Response, jsonify, render_template, request, send_from_directory

import args as argbuild

POSIX = os.name == "posix"  # the Pi; the Windows branches exist only for local smoke tests
MAX_LINES = 2000            # kept per job for late-joining browsers
MAX_DECODE_BYTES = 256 * 1024
DECODE_TIMEOUT_S = 120
STOP_GRACE_S = 10


class Job:
    """One running (or finished) capture or live decode."""

    def __init__(self, kind, argv, out_name=None):
        self.id = "%d" % (time.time() * 1000)
        self.kind = kind                 # "capture" or "live"
        self.argv = argv
        self.out_name = out_name
        self.started = time.time()
        self.finished = None
        self.exit_code = None
        self.state = "running"           # running | finished | failed
        self.lines = []
        self.procs = []
        self._subscribers = []
        self._lock = threading.Lock()

    # --- output ---------------------------------------------------------
    def emit(self, kind, text):
        event = {"type": kind, "text": text, "t": round(time.time() - self.started, 3)}
        with self._lock:
            self.lines.append(event)
            del self.lines[:-MAX_LINES]
            subscribers = list(self._subscribers)
        for q in subscribers:
            try:
                q.put_nowait(event)
            except queue.Full:
                pass

    def subscribe(self):
        q = queue.Queue(maxsize=1000)
        with self._lock:
            backlog = list(self.lines)
            self._subscribers.append(q)
        return q, backlog

    def unsubscribe(self, q):
        with self._lock:
            if q in self._subscribers:
                self._subscribers.remove(q)

    # --- state ----------------------------------------------------------
    def status(self):
        return {
            "id": self.id,
            "kind": self.kind,
            "state": self.state,
            "argv": self.argv,
            "out_name": self.out_name,
            "elapsed": round((self.finished or time.time()) - self.started, 3),
            "exit_code": self.exit_code,
        }

    def is_running(self):
        return self.state == "running"

    def signal_all(self, sig):
        for proc in self.procs:
            if proc.poll() is None:
                try:
                    if POSIX:
                        os.killpg(os.getpgid(proc.pid), sig)
                    else:
                        proc.kill()   # Windows has no signals; smoke tests only
                except (ProcessLookupError, PermissionError, OSError):
                    pass


class Runner:
    """Holds the one allowed job."""

    def __init__(self, pi5la, sigrok, captures_dir):
        self.pi5la = pi5la
        self.sigrok = sigrok
        self.captures_dir = captures_dir
        self.job = None
        self.lock = threading.Lock()

    def current(self):
        return self.job

    def start(self, job, pipeline):
        """pipeline() starts the processes and returns reader threads."""
        with self.lock:
            if self.job is not None and self.job.is_running():
                raise RuntimeError("a %s is already running; stop it first" % self.job.kind)
            self.job = job
        threads = pipeline(job)
        threading.Thread(target=self._reap, args=(job, threads), daemon=True).start()
        return job

    def _reap(self, job, threads):
        for t in threads:
            t.join()
        codes = []
        for proc in job.procs:
            try:
                codes.append(proc.wait(timeout=STOP_GRACE_S))
            except subprocess.TimeoutExpired:
                proc.kill()
                codes.append(proc.wait())
        job.exit_code = codes[0] if codes else None
        job.finished = time.time()
        # pi5la: 0 ok, 1 error/interrupted, 2 dropped samples. 130/-2 = SIGINT.
        job.state = "finished" if job.exit_code in (0, 2, 130, -2) else "failed"
        job.emit("status", "exit code %s" % job.exit_code)

    def stop(self):
        job = self.job
        if job is None or not job.is_running():
            return False
        job.emit("status", "stopping: sending SIGINT")
        job.signal_all(signal.SIGINT)
        threading.Thread(target=self._force_stop, args=(job,), daemon=True).start()
        return True

    def _force_stop(self, job):
        deadline = time.time() + STOP_GRACE_S
        while time.time() < deadline:
            if not any(p.poll() is None for p in job.procs):
                return
            time.sleep(0.2)
        job.emit("status", "still running after %ds: killing it" % STOP_GRACE_S)
        job.signal_all(getattr(signal, "SIGKILL", signal.SIGTERM))


def _spawn(argv, **kwargs):
    """Popen with the child in its own session, so a stop can signal the group."""
    if POSIX:
        kwargs["start_new_session"] = True
    return subprocess.Popen(argv, **kwargs)


def _pump(job, stream, kind):
    """Forward a child's output into the job, line by line."""
    def run():
        try:
            for raw in iter(stream.readline, b""):
                job.emit(kind, raw.decode("utf-8", "replace").rstrip("\r\n"))
        finally:
            try:
                stream.close()
            except OSError:
                pass
    t = threading.Thread(target=run, daemon=True)
    t.start()
    return t


def create_app(runner):
    app = Flask(__name__)

    @app.get("/")
    def index():
        return render_template("index.html")

    # --- capture --------------------------------------------------------
    @app.post("/api/capture")
    def start_capture():
        form = request.get_json(silent=True) or {}
        monitor = argbuild._flag(form.get("monitor"))
        name = argbuild.new_monitor_dir() if monitor else argbuild.new_capture_name()
        out_path = os.path.join(runner.captures_dir, name)
        try:
            argv, warnings = argbuild.capture_command(form, runner.pi5la, out_path)
        except argbuild.InvalidInput as e:
            return jsonify(error=str(e)), 400

        job = Job("monitor" if monitor else "capture", argv, out_name=name)

        def pipeline(job):
            proc = _spawn(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            job.procs.append(proc)
            job.emit("status", "started: " + " ".join(argv))
            return [_pump(job, proc.stdout, "out")]

        try:
            runner.start(job, pipeline)
        except RuntimeError as e:
            return jsonify(error=str(e)), 409
        except OSError as e:
            return jsonify(error="cannot run %s: %s" % (runner.pi5la, e)), 500
        return jsonify(job=job.status(), warnings=warnings)

    # --- live decode ----------------------------------------------------
    @app.post("/api/live")
    def start_live():
        form = request.get_json(silent=True) or {}
        try:
            capture_argv, decode_argv, warnings = argbuild.live_commands(
                form, runner.pi5la, runner.sigrok)
        except argbuild.InvalidInput as e:
            return jsonify(error=str(e)), 400

        job = Job("live", capture_argv + ["|"] + decode_argv)

        def pipeline(job):
            producer = _spawn(capture_argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                consumer = _spawn(decode_argv, stdin=producer.stdout,
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            except OSError:
                producer.kill()
                raise
            producer.stdout.close()      # the consumer owns the read end now
            job.procs += [producer, consumer]
            job.emit("status", "started: %s | %s" % (" ".join(capture_argv),
                                                     " ".join(decode_argv)))
            return [_pump(job, producer.stderr, "status"),
                    _pump(job, consumer.stdout, "decoded")]

        try:
            runner.start(job, pipeline)
        except RuntimeError as e:
            return jsonify(error=str(e)), 409
        except OSError as e:
            return jsonify(error="cannot start the pipeline: %s" % e), 500
        return jsonify(job=job.status(), warnings=warnings)

    @app.post("/api/stop")
    def stop():
        return jsonify(stopped=runner.stop())

    def es_credentials_present():
        """
        Whether this server could ship to Elasticsearch at all.

        pi5la's shipper inherits this process's environment, so credentials must
        already be exported when the server is started. Asking here lets the page
        say so before a capture is launched, rather than after every burst fails.
        """
        return bool(os.environ.get("ES_API_KEY") or
                    (os.environ.get("ES_USER") and os.environ.get("ES_PASSWORD")))

    @app.get("/api/status")
    def status():
        job = runner.current()
        env = {"es_credentials": es_credentials_present()}
        if job is None:
            return jsonify(job=None, env=env)
        return jsonify(job=job.status(), lines=job.lines[-200:], env=env)

    @app.get("/api/events")
    def events():
        job = runner.current()
        if job is None:
            return jsonify(error="no job"), 404
        q, backlog = job.subscribe()

        def stream():
            try:
                for event in backlog:
                    yield "data: %s\n\n" % json.dumps(event)
                while True:
                    try:
                        event = q.get(timeout=15)
                    except queue.Empty:
                        yield ": keepalive\n\n"
                        continue
                    yield "data: %s\n\n" % json.dumps(event)
                    if event["type"] == "status" and event["text"].startswith("exit code"):
                        break
            finally:
                job.unsubscribe(q)

        return Response(stream(), mimetype="text/event-stream",
                        headers={"Cache-Control": "no-cache", "X-Accel-Buffering": "no"})

    # --- captures -------------------------------------------------------
    def read_index(session_dir):
        """A monitor session's index.json, or None if it isn't written yet."""
        try:
            with open(os.path.join(session_dir, "index.json"), "r") as f:
                return json.load(f)
        except (OSError, ValueError):
            return None

    def list_bursts(session_dir, name, index):
        """
        Every burst of a session, from the per-burst sidecars.

        A burst that has been shipped to Elasticsearch has had its .sr deleted
        and only its sidecar is left, so listing the files on disk would make it
        disappear from the page. The sidecars are what span the whole session --
        index.json only ever describes the run that wrote it.
        """
        seen, bursts = set(), []
        for meta_path in sorted(glob.glob(os.path.join(session_dir, "burst-*.sr.meta.json"))):
            try:
                with open(meta_path) as f:
                    meta = json.load(f)
            except (OSError, ValueError):
                continue
            sr_name = os.path.basename(meta_path)[:-len(".meta.json")]
            sr_path = os.path.join(session_dir, sr_name)
            here = os.path.isfile(sr_path)
            seen.add(sr_name)
            bursts.append({
                "name": "%s/%s" % (name, sr_name),
                "size": os.stat(sr_path).st_size if here else 0,
                "start_time": meta.get("start_time"),
                "duration_s": meta.get("duration_s"),
                "gap_before_s": meta.get("gap_before_s"),
                "protocol": meta.get("protocol"),
                "shipped": bool(meta.get("shipped")),
                "shipped_docs": meta.get("shipped_docs"),
                "on_disk": here,
            })

        # Sessions recorded before sidecars existed are described by index.json.
        for b in index.get("bursts", []):
            sr_name = b.get("file", "")
            if sr_name in seen or not os.path.isfile(os.path.join(session_dir, sr_name)):
                continue
            bursts.append({
                "name": "%s/%s" % (name, sr_name),
                "size": os.stat(os.path.join(session_dir, sr_name)).st_size,
                "start_time": b.get("start_time"),
                "duration_s": b.get("duration_s"),
                "gap_before_s": b.get("gap_before_s"),
                "shipped": False,
                "on_disk": True,
            })
        bursts.sort(key=lambda b: b["name"])
        return bursts

    @app.get("/api/captures")
    def list_captures():
        items = []
        for name in sorted(os.listdir(runner.captures_dir), reverse=True):
            path = os.path.join(runner.captures_dir, name)
            if name.endswith(".sr") and os.path.isfile(path):
                st = os.stat(path)
                items.append({"kind": "file", "name": name, "size": st.st_size,
                              "mtime": st.st_mtime})
            elif argbuild.MONITOR_DIR_RE.fullmatch(name) and os.path.isdir(path):
                index = read_index(path) or {}
                bursts = list_bursts(path, name, index)
                items.append({"kind": "monitor", "name": name,
                              "mtime": os.stat(path).st_mtime,
                              "size": sum(b["size"] for b in bursts),
                              "samplerate_hz": index.get("samplerate_hz"),
                              "channels": index.get("channels"),
                              "bursts": bursts})
        return jsonify(captures=items)

    @app.get("/api/captures/<path:ref>")
    def download(ref):
        try:
            safe = argbuild.safe_capture_ref(ref)
        except argbuild.InvalidInput as e:
            return jsonify(error=str(e)), 400
        return send_from_directory(runner.captures_dir, safe, as_attachment=True)

    def remove(safe):
        """Delete one capture, or a whole monitor session directory."""
        path = os.path.join(runner.captures_dir, safe)
        try:
            if os.path.isdir(path):
                for entry in os.listdir(path):
                    os.remove(os.path.join(path, entry))
                os.rmdir(path)
            else:
                os.remove(path)
        except FileNotFoundError:
            return jsonify(error="no such capture"), 404
        return jsonify(deleted=safe)

    @app.delete("/api/captures/<path:ref>")
    def delete(ref):
        try:
            safe = argbuild.safe_capture_ref(ref)
        except argbuild.InvalidInput as e:
            return jsonify(error=str(e)), 400
        return remove(safe)

    @app.delete("/api/sessions/<name>")
    def delete_session(name):
        if not argbuild.MONITOR_DIR_RE.fullmatch(name or ""):
            return jsonify(error="invalid session name"), 400
        return remove(name)

    @app.post("/api/decode")
    def decode():
        form = request.get_json(silent=True) or {}
        try:
            safe = argbuild.safe_capture_ref(form.get("name", ""))
            channels = int(form.get("channels", 8))
            base = int(form.get("base_gpio", 2))
            argv = argbuild.decode_command(os.path.join(runner.captures_dir, safe), form,
                                           channels=channels, base_gpio=base,
                                           sigrok=runner.sigrok)
        except (argbuild.InvalidInput, ValueError) as e:
            return jsonify(error=str(e)), 400
        if not os.path.exists(os.path.join(runner.captures_dir, safe)):
            return jsonify(error="no such capture"), 404
        try:
            done = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                  timeout=DECODE_TIMEOUT_S)
        except subprocess.TimeoutExpired:
            return jsonify(error="decoding timed out after %ds" % DECODE_TIMEOUT_S), 504
        except OSError as e:
            return jsonify(error="cannot run %s: %s" % (runner.sigrok, e)), 500
        text = done.stdout[:MAX_DECODE_BYTES].decode("utf-8", "replace")
        if len(done.stdout) > MAX_DECODE_BYTES:
            text += "\n... truncated ..."
        return jsonify(argv=argv, exit_code=done.returncode, text=text)

    return app


def main():
    home = os.path.expanduser("~")
    p = argparse.ArgumentParser(description="web UI for pi5la")
    p.add_argument("--pi5la", default=os.path.join(home, "build-pi5la", "pi5la"),
                   help="path to the pi5la binary")
    p.add_argument("--sigrok", default="sigrok-cli", help="sigrok-cli command")
    p.add_argument("--captures", default=os.path.join(home, "captures"),
                   help="directory for .sr files")
    p.add_argument("--host", default="0.0.0.0", help="bind address (127.0.0.1 for local only)")
    p.add_argument("--port", type=int, default=9090, help="listen port (default 9090)")
    opts = p.parse_args()

    os.makedirs(opts.captures, exist_ok=True)
    runner = Runner(opts.pi5la, opts.sigrok, os.path.abspath(opts.captures))
    if not os.path.exists(runner.pi5la):
        print("warning: %s not found; captures will fail" % runner.pi5la)
    print("pi5la web UI on http://%s:%d  (captures in %s)" % (opts.host, opts.port,
                                                              runner.captures_dir))
    print("No authentication: keep this on a trusted network.")
    create_app(runner).run(host=opts.host, port=opts.port, threaded=True)


if __name__ == "__main__":
    main()
