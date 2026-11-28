"use strict";

const log = document.getElementById("log");
const stateEl = document.getElementById("job-state");
const esPill = document.getElementById("es-pill");
const startCapture = document.getElementById("start-capture");
const startLive = document.getElementById("start-live");
const stopBtn = document.getElementById("stop-job");
const captureNote = document.getElementById("capture-note");
const shipToggle = document.getElementById("ship-toggle");
const shipFields = document.getElementById("ship-fields");
const shipProtocol = document.getElementById("ship-protocol");
let events = null;
let hasEsCredentials = true;
let captureTimer = null;

/* While a job runs, nothing else asks the server what is on disk, so bursts --
   and the "shipped" badge each one gets once the cluster confirms it -- would
   not appear until the capture was stopped. Poll instead. */
const CAPTURE_POLL_MS = 2000;

function pollCaptures(on) {
    if (on && !captureTimer) captureTimer = setInterval(loadCaptures, CAPTURE_POLL_MS);
    if (!on && captureTimer) {
        clearInterval(captureTimer);
        captureTimer = null;
    }
}

const PRESETS = {
    uart: { rate: "2M", channels: "8", duration: "", silence_timeout: "1s", dma_buf: "" },
    i2c: { rate: "10M", channels: "8", duration: "", silence_timeout: "1s", dma_buf: "" },
    spi: { rate: "50M", channels: "8", duration: "5s", silence_timeout: "500ms", dma_buf: "" },
};

function captureMode() {
    const picked = document.querySelector('input[name="mode"]:checked');
    return picked ? picked.value : "continuous";
}

/* Fields inside a hidden block are not submitted, so switching a section off
   is the same as leaving it out. Checkboxes report checked, not "on". */
function formValues(form) {
    const out = {};
    for (const el of form.elements) {
        if (!el.name || el.closest(".hidden")) continue;
        out[el.name] = el.type === "checkbox" ? el.checked : el.value;
    }
    return out;
}

function write(text, cls) {
    const span = document.createElement("span");
    span.className = cls || "";
    span.textContent = text + "\n";
    log.appendChild(span);
    log.scrollTop = log.scrollHeight;
}

function setState(job) {
    if (!job) {
        stateEl.textContent = "idle";
        stateEl.className = "pill";
        stopBtn.disabled = true;
        startCapture.disabled = startLive.disabled = false;
        return;
    }
    const running = job.state === "running";
    stateEl.textContent = running
        ? `${job.kind} · ${job.elapsed.toFixed(1)}s`
        : `${job.kind} ${job.state} · exit ${job.exit_code} · ${job.elapsed.toFixed(1)}s`;
    stateEl.className = "pill " + (running ? "running"
        : job.exit_code === 0 ? "done" : "failed");
    stopBtn.disabled = !running;
    startCapture.disabled = startLive.disabled = running;
    pollCaptures(running);
    if (!running) loadCaptures();
}

function classFor(event) {
    if (event.type === "decoded") return "decoded";
    if (event.type === "status") return "status";
    if (/^shipped /.test(event.text)) return "decoded";
    if (/warning|note:/i.test(event.text)) return "warn";
    return /error|cannot|invalid|DROPPED|overflow|refused|not accepted/i.test(event.text)
        ? "err" : "";
}

function follow() {
    if (events) events.close();
    events = new EventSource("/api/events");
    events.onmessage = (msg) => {
        const event = JSON.parse(msg.data);
        write(event.text, classFor(event));
        if (event.type === "status" && event.text.startsWith("exit code")) {
            events.close();
            events = null;
            refreshStatus();
        }
    };
    events.onerror = () => { /* server closed the stream; status polling takes over */ };
}

async function refreshStatus() {
    const res = await fetch("/api/status");
    const data = await res.json();
    setState(data.job);
    if (data.env) setEsCredentials(data.env.es_credentials);
    return data.job;
}

/* The shipper inherits the server's environment, so whether this page can ship
   at all was decided when the server was started. Say so up front. */
function setEsCredentials(present) {
    hasEsCredentials = !!present;
    document.getElementById("es-missing-creds").classList.toggle("hidden", hasEsCredentials);
    esPill.classList.toggle("hidden", hasEsCredentials || !shipToggle.checked);
    esPill.className = "pill failed" + (hasEsCredentials || !shipToggle.checked ? " hidden" : "");
    esPill.textContent = "no ES credentials";
}

async function post(url, body) {
    const res = await fetch(url, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body || {}),
    });
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || `request failed (${res.status})`);
    return data;
}

async function startJob(url, form, extra) {
    log.textContent = "";
    try {
        const data = await post(url, Object.assign(formValues(form), extra || {}));
        (data.warnings || []).forEach((w) => write("warning: " + w, "warn"));
        setState(data.job);
        follow();
    } catch (e) {
        write(e.message, "err");
        refreshStatus();
    }
}

startCapture.addEventListener("click", () =>
    startJob("/api/capture", document.getElementById("capture-form"),
             { monitor: captureMode() === "monitor" }));

startLive.addEventListener("click", () =>
    startJob("/api/live", document.getElementById("live-form")));

stopBtn.addEventListener("click", async () => {
    stopBtn.disabled = true;
    await post("/api/stop");
});

document.getElementById("clear-log").addEventListener("click", () => {
    log.textContent = "";
});

/* --- capture mode ----------------------------------------------------- */

/* Monitor mode has its own burst settings, and is the only mode that can ship:
   shipping sends one burst at a time, so there have to be bursts. */
function updateMode() {
    const monitor = captureMode() === "monitor";
    document.querySelectorAll(".m-monitor").forEach((el) =>
        el.classList.toggle("hidden", !monitor));
    startCapture.textContent = monitor ? "Start monitoring" : "Start capture";
    updateCaptureNote();
    updateShip();
}

/* A silence timeout or a duration ends the session; while monitoring that is
   rarely what is wanted, so say so rather than letting it stop unexplained. */
function updateCaptureNote() {
    const form = document.getElementById("capture-form");
    const monitor = captureMode() === "monitor";
    const silence = form.elements.silence_timeout.value.trim();
    const duration = form.elements.duration.value.trim();

    if (monitor && (silence || duration)) {
        captureNote.textContent = silence
            ? `Will stop by itself after ${silence} of silence — clear it to monitor until you press Stop.`
            : `Will stop by itself after ${duration} — clear it to monitor until you press Stop.`;
        captureNote.className = "state warn-text";
        return;
    }
    captureNote.className = "state";
    captureNote.textContent = monitor
        ? "Sampling never stops; only the bursts are written. Runs until you press Stop."
        : "Runs until you press Stop unless you set a limit.";
}

["silence_timeout", "duration"].forEach((name) => {
    const el = document.getElementById("capture-form").elements[name];
    el.addEventListener("input", updateCaptureNote);
});

document.querySelectorAll('input[name="mode"]').forEach((radio) =>
    radio.addEventListener("change", updateMode));

/* --- Elasticsearch ---------------------------------------------------- */

function updateShip() {
    const on = shipToggle.checked && captureMode() === "monitor";
    shipFields.classList.toggle("hidden", !on);

    const raw = shipProtocol.value === "raw";
    document.querySelectorAll(".s-uart").forEach((el) =>
        el.classList.toggle("hidden", raw));

    const note = document.getElementById("ship-protocol-note");
    note.classList.toggle("hidden", !on);
    note.className = "pill" + (on ? "" : " hidden");
    note.textContent = raw ? "one document per burst, .sr files kept"
                           : "one document per decoded message";
    setEsCredentials(hasEsCredentials);
}

shipToggle.addEventListener("change", updateShip);
shipProtocol.addEventListener("change", updateShip);

/* --- presets ---------------------------------------------------------- */

/*
 * Presets carry a silence timeout, which ends the whole capture once the bus
 * goes quiet. That is useful for a one-shot recording and wrong for monitoring,
 * where the point is to sit and watch until someone stops it -- so in monitor
 * mode the preset leaves it empty.
 */
document.querySelectorAll("[data-preset]").forEach((btn) => {
    btn.addEventListener("click", () => {
        const preset = PRESETS[btn.dataset.preset];
        const form = document.getElementById("capture-form");
        const monitor = captureMode() === "monitor";
        for (const [key, value] of Object.entries(preset)) {
            if (!form.elements[key]) continue;
            const stopper = key === "silence_timeout" || key === "duration";
            form.elements[key].value = monitor && stopper ? "" : value;
        }
    });
});

/* Protocol pickers show only the fields that protocol needs. */
document.querySelectorAll("select.protocol").forEach((select) => {
    const update = () => {
        const form = select.closest("form");
        for (const p of ["uart", "i2c", "spi"]) {
            form.querySelectorAll(".p-" + p).forEach((el) =>
                el.classList.toggle("hidden", p !== select.value));
        }
    };
    select.addEventListener("change", update);
    update();
});

/* --- captures --------------------------------------------------------- */

const tbody = document.querySelector("#captures tbody");
const capturesEmpty = document.getElementById("captures-empty");
const decodeBox = document.getElementById("decode-box");
const decodeName = document.getElementById("decode-name");
const decodeOut = document.getElementById("decode-out");

function humanSize(bytes) {
    if (bytes >= 1e9) return (bytes / 1e9).toFixed(2) + " GB";
    if (bytes >= 1e6) return (bytes / 1e6).toFixed(1) + " MB";
    if (bytes >= 1e3) return (bytes / 1e3).toFixed(1) + " kB";
    return bytes + " B";
}

function cell(row, text, cls) {
    const td = document.createElement("td");
    if (cls) td.className = cls;
    if (text !== null && text !== undefined) td.textContent = text;
    row.appendChild(td);
    return td;
}

function encodeRef(name) {
    return name.split("/").map(encodeURIComponent).join("/");
}

function captureRow(name, label, size, when, opts) {
    opts = opts || {};
    const tr = document.createElement("tr");
    if (opts.rowClass) tr.className = opts.rowClass;

    const nameCell = cell(tr, label);
    if (opts.badge) {
        const badge = document.createElement("span");
        badge.className = "badge " + opts.badge.cls;
        badge.textContent = opts.badge.text;
        nameCell.append(" ", badge);
    }
    cell(tr, size, "num");
    cell(tr, when, "num");

    const actions = cell(tr, null, "actions-cell");
    if (opts.onDisk === false) {
        const gone = document.createElement("span");
        gone.className = "state";
        gone.textContent = "uploaded, file removed";
        actions.appendChild(gone);
        return tr;
    }

    const dl = document.createElement("a");
    dl.href = "/api/captures/" + encodeRef(name);
    dl.textContent = "Download";
    actions.appendChild(dl);

    const decode = document.createElement("button");
    decode.className = "link";
    decode.textContent = "Decode";
    decode.addEventListener("click", () => {
        decodeBox.classList.remove("hidden");
        decodeName.textContent = name;
        decodeOut.textContent = "";
        decodeBox.scrollIntoView({ behavior: "smooth", block: "nearest" });
    });
    actions.appendChild(decode);

    const del = document.createElement("button");
    del.className = "link danger";
    del.textContent = "Delete";
    del.addEventListener("click", async () => {
        if (!confirm("Delete " + name + "?")) return;
        await fetch("/api/captures/" + encodeRef(name), { method: "DELETE" });
        loadCaptures();
    });
    actions.appendChild(del);

    return tr;
}

async function loadCaptures() {
    const res = await fetch("/api/captures");
    const data = await res.json();
    tbody.textContent = "";
    capturesEmpty.classList.toggle("hidden", data.captures.length > 0);

    for (const item of data.captures) {
        if (item.kind !== "monitor") {
            tbody.appendChild(captureRow(item.name, item.name, humanSize(item.size),
                                         new Date(item.mtime * 1000).toLocaleString()));
            continue;
        }

        const shipped = item.bursts.filter((b) => b.shipped).length;
        const head = document.createElement("tr");
        head.className = "session";
        const title = cell(head, `${item.name} — ${item.bursts.length} burst(s)`);
        if (shipped) {
            const badge = document.createElement("span");
            badge.className = "badge shipped";
            badge.textContent = `${shipped} shipped`;
            title.append(" ", badge);
        }
        cell(head, humanSize(item.size), "num");
        cell(head, new Date(item.mtime * 1000).toLocaleString(), "num");

        const actions = cell(head, null, "actions-cell");
        const del = document.createElement("button");
        del.className = "link danger";
        del.textContent = "Delete session";
        del.addEventListener("click", async () => {
            if (!confirm("Delete the whole session " + item.name + "?")) return;
            await fetch("/api/sessions/" + encodeURIComponent(item.name), { method: "DELETE" });
            loadCaptures();
        });
        actions.appendChild(del);
        tbody.appendChild(head);

        for (const burst of item.bursts) {
            const file = burst.name.split("/")[1];
            const gap = burst.gap_before_s != null
                ? `, ${burst.gap_before_s.toFixed(1)}s after the previous` : "";
            tbody.appendChild(captureRow(
                burst.name,
                `↳ ${file} (${(burst.duration_s || 0).toFixed(3)}s${gap})`,
                burst.on_disk === false ? "—" : humanSize(burst.size),
                burst.start_time ? new Date(burst.start_time * 1000).toLocaleTimeString() : "",
                {
                    rowClass: "burst" + (burst.on_disk === false ? " gone" : ""),
                    onDisk: burst.on_disk,
                    badge: burst.shipped
                        ? { cls: "shipped",
                            text: burst.shipped_docs != null
                                ? `shipped ${burst.shipped_docs} doc(s)` : "shipped" }
                        : null,
                }));
        }
    }
}

document.getElementById("refresh-captures").addEventListener("click", loadCaptures);
document.getElementById("close-decode").addEventListener("click", () =>
    decodeBox.classList.add("hidden"));

document.getElementById("run-decode").addEventListener("click", async () => {
    const body = formValues(document.getElementById("decode-form"));
    body.name = decodeName.textContent;
    decodeOut.textContent = "decoding...";
    try {
        const data = await post("/api/decode", body);
        decodeOut.textContent = data.text.trim() || "(no output: check the channel numbers)";
    } catch (e) {
        decodeOut.textContent = e.message;
    }
});

updateMode();
refreshStatus().then((job) => { if (job && job.state === "running") follow(); });
loadCaptures();
