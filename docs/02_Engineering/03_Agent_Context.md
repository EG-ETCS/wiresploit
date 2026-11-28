# Agent context

How the repo gives AI coding agents the context they need, and how that context
stays correct. The team uses several AI tools, so the shared context is written
once, in `AGENTS.md` files, and read by every tool.

## The layers

| Layer | File | Shared | Holds |
|---|---|---|---|
| 1. Team-wide | `AGENTS.md` (repo root) | Yes | What Wiresploit is, repo map, terminology and banned words, branch workflow, links to the standards, the update rule |
| 2. Component | `node/AGENTS.md`, later `core/AGENTS.md` and others | Yes | How to build and test, layers, critical constraints, hardware safety, conventions, file map |
| 3. Decisions | `docs/03_Documentation/<Component>/decisions.md` | Yes | Each technical decision, why it was made, and when |
| 4. Personal | `AGENTS.local.md` (repo root) | No, gitignored | How you want agents to work, your machine, your Pi |

An agent working on the Node reads layers 1, 2 and 4. Layer 3 and the rest of
the docs site are there for when it needs the reason behind a rule.

Layers 1 and 2 are short on purpose. They hold what an agent must know before
changing anything, and link to the full pages for everything else:

- [Coding rules](01_Coding_Rules.md)
- [Design system](02_Design_System.md)
- [Testing](04_Testing.md)

## The update rule

A pull request that changes behaviour, a build step or a decision also updates
the matching `AGENTS.md` or `decisions.md` **in the same pull request**.

This is how one person's agent learnings reach everyone. If you or your agent
find out something the next person needs (a command that changed, a constraint
that bit you, a choice between two designs), it goes into the shared files, not
into one person's chat history. Reviewers check it like they check tests.

## Personal context

Your own notes go in `AGENTS.local.md` at the repo root. It is gitignored, so it
can hold what must not be shared: your Pi's address, your user name, local
paths, the way you like an agent to work.

1. Copy `AGENTS.local.md.example` to `AGENTS.local.md`.
2. Fill in its three sections: how you want agents to work, your environment,
   your Pi.
3. Point your tool at it (below).

Personal notes are never a substitute for the shared files. Anything the team
needs moves into an `AGENTS.md` or `decisions.md` through a pull request.

## Using AGENTS.md with your tool

Many coding agents read `AGENTS.md` from the repo on their own. Check your
tool's documentation for how it finds the file and whether it also reads
`AGENTS.md` files in subfolders; if it doesn't, tell it to read
`node/AGENTS.md` when you work on the Node.

**Claude Code** reads `CLAUDE.md` files rather than `AGENTS.md`. The repo has no
`CLAUDE.md` files, so that every tool reads the same source. Instead, create a
gitignored `CLAUDE.local.md` at the repo root containing:

```
@AGENTS.md
@AGENTS.local.md
```

Each `@` line imports that file. Add `@node/AGENTS.md` too if you mostly work on
the Node.

## What never goes into shared context

- Credentials, API keys and tokens, in any committed file. Keep them in
  environment variables or in the gitignored `node/node.conf` (mode 600).
- IP addresses, hostnames, user names and home-folder paths. Write `<pi-host>`
  for the Pi's address, `<install_dir>` for the Node's install folder on a Pi,
  and repo-relative paths for everything in the repo.
- Paths in Windows form, or paths that only exist on one machine.
- Instructions that only suit one person ("ask me before…"). Those go in
  `AGENTS.local.md`.

## Writing good context

- Write for an agent that knows nothing about the project: plain, exact, short.
- Put each fact in one place and link to it. Two copies drift apart.
- State constraints as rules with their reason: "Never poll the FIFO word by
  word: each call is a 10 µs PCIe round trip." The reason stops a well-meaning
  "fix".
- Keep hardware safety prominent anywhere a reader could connect hardware.
- Give commands exactly as they are run, with placeholders in angle brackets.

## Adding context for a new component

Copy `node/AGENTS.md` to `<component>/AGENTS.md` and keep its headings: what
this is, build and test, paths and settings, architecture, critical constraints,
safety, conventions, file map, documentation, status. Then add a row for it to
the "Where component context lives" table in the root `AGENTS.md`, and start a
`decisions.md` beside the component's docs.
