# AGENTS.md

Shared context for AI coding agents, and for the people who work with them, in
the Wiresploit repo. Any tool that reads `AGENTS.md` picks this file up. It is
kept short on purpose: the details live in the pages it links to.

Read this file first, then the `AGENTS.md` of the component you are changing.

## What Wiresploit is

Wiresploit is an IoT/OT reconnaissance and monitoring system for security
analysts, built as a university team project. It records a Device Under Test's
(DUT's) network traffic and onboard buses (UART, SPI, I2C, wireless), timestamps
every event against one shared clock, groups related events into Communication
Blocks (CB) on a single timeline, and lets the analyst run actions on the DUT
after an explicit confirmation.

It has two kinds of part:

- **Core**: the central system. It stores, correlates and analyses events, and
  serves the web application the analyst uses. The System Architecture document
  calls its processing layer the **Brain**.
- **Node**: a device attached to the DUT. A Node has two functions:
  **Capture** (record bus traffic) and **Actions** (perform configured actions
  on the DUT). The first Node is pi5la, an 8-channel logic analyzer on a
  Raspberry Pi 5.

## Repo map

| Path | What it holds |
|---|---|
| `docs/` | The MkDocs site, configured by `mkdocs.yml` |
| `docs/01_Requirements/` | BRD, system requirements, System Architecture. BR IDs come from here |
| `docs/02_Engineering/` | Coding rules, design system, agent context, testing |
| `docs/03_Documentation/Node/` | Node docs: capture guide, monitor mode, findings, decisions |
| `node/` | Node code. Read `node/AGENTS.md` before changing it |
| `node/capture/` | Capture function: the pi5la C core, its tools and the local test page |
| `node/exporter/` | `sr2es.py`: decodes captures and sends them to Elasticsearch |
| `node/actions/` | Actions function (placeholder, not started) |
| `node/scripts/` | Setup, test, build, deploy and docs scripts |
| `.devcontainer/node/` | Dev container for Node work |
| `AGENTS.local.md.example` | Template for your personal, gitignored agent notes |

Core's code lives on its own branches for now.

## Terminology

Use the same word for the same thing everywhere: code, docs, commit messages and
UI text. The full table is in the coding rules, §1.5.

| Term | Meaning |
|---|---|
| Core | The central Wiresploit system |
| Backend | Only the component named Backend in the approved architecture |
| Node | A deployed Wiresploit device with Capture and Actions functions |
| Capture, Actions | The Node's two functions |
| Online, Ready, Running, Unreachable | The defined Node states |
| DUT | Device Under Test |
| Communication Block (CB), Snapshot Block (SB) | Timeline units, as defined in the BRD |
| bus, bus_name, channel | Bus identifier, user label, sample or wire channel index |
| burst, seq_num, burst record | A captured group of samples, its sequence number, its record |
| event, packet | A normalised decoded event; a raw or network packet |
| info file, capture plan, session folder | Burst metadata file, capture configuration, one session's folder |
| protocol plug-in, engine | Node-side decoder; Core-side analysis program |

**Banned as Wiresploit component names:** `server`, `sniffer`, `probe`, subject
to approved terminology exceptions. Say Core, Node, Capture or Actions instead.
The rule is about naming parts of Wiresploit; it does not rename existing tools
such as `bitprobe`.

## Branch workflow

- Feature branches are named `feature/<area>/<BR-ID>-<short-name>`, for example
  `feature/node/BR-MON-01-capture-pipeline`. Use a real BR ID from the BRD.
  Never invent one.
- Each task gets its own branch, cut from the feature branch and named
  `<area>/<task>`, for example `node/hygiene`.
- A task branch is merged back into the feature branch with `git merge --no-ff`
  once its tests pass, so each task stays visible in the history.
- The team opens one pull request from the feature branch when the feature is
  done.
- Commit only to your own task branch. Don't rewrite shared history.

## Standards

- Coding rules: [`docs/02_Engineering/01_Coding_Rules.md`](docs/02_Engineering/01_Coding_Rules.md).
  Naming, docstrings, errors, tests, Black and Ruff.
- Design system: [`docs/02_Engineering/02_Design_System.md`](docs/02_Engineering/02_Design_System.md).
  Read it before writing any UI, report or user-facing text.
- Testing: [`docs/02_Engineering/04_Testing.md`](docs/02_Engineering/04_Testing.md).
  Unit and integration tests must pass before a task branch is merged.
- How this context is organised: [`docs/02_Engineering/03_Agent_Context.md`](docs/02_Engineering/03_Agent_Context.md).

Writing style for docs, comments and UI text: plain, exact, sentence case, with
units (`12 events`, `+4.1 ms`, `10 MSa/s`). No hype and no exclamation marks.

## Where component context lives

| Component | Agent context | Decisions | Human docs |
|---|---|---|---|
| Node | `node/AGENTS.md` | `docs/03_Documentation/Node/decisions.md` | `docs/03_Documentation/Node/` |
| Core, web application | Not written yet. Copy `node/AGENTS.md` as a template | | |

## The update rule

A pull request that changes behaviour, a build step or a decision also updates
the matching `AGENTS.md` or `decisions.md` in the same pull request. That is how
one person's agent learnings reach everyone. If a reviewer finds the context out
of date, the pull request is not done.

## Personal context

Your own preferences, machine details and hardware notes go in `AGENTS.local.md`
at the repo root. It is gitignored. Start from `AGENTS.local.md.example`. Claude
Code users can put `@AGENTS.md` and `@AGENTS.local.md` in their own gitignored
`CLAUDE.local.md`. The repo has no `CLAUDE.md` files, because the team uses
several AI tools.

## Never commit

- Credentials, API keys or tokens. Keep them, and every other setting that
  differs per machine, in environment variables or in the gitignored
  `node.conf` (mode 600): `node/node.conf` in the repo, `<install_dir>/node.conf`
  on a Pi.
- IP addresses, hostnames, user names or home-folder paths of anyone's machine.
  Write `<pi-host>`, `<install_dir>` and repo-relative paths instead.
- Build output, `__pycache__/`, `.venv/`, `site/` or editor settings.

## Hardware safety

Raspberry Pi 5 GPIO is 3.3 V and not 5 V tolerant. Overvoltage destroys the RP1
chip, which is soldered to the board, so the Pi is lost. Any external target
must go through a 74LVC245 buffer with series resistors and a shared ground. The
only safe bare-wire setup is a loopback jumper between Pi header pins. State
this in any instructions that ask someone to connect hardware. Details:
`node/AGENTS.md` and the Node capture guide.
