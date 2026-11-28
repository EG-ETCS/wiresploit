# Coding rules

The rules for all Wiresploit code. They apply to every component: Core, the web
application and the Node. Python is the main language; the Node's C code follows
the same naming, comment and test principles, plus the C conventions in
`node/AGENTS.md`.

Agents and people read the same rules. The short version for agents is in the
root `AGENTS.md`; this page is the full text.

## Quick reference

| Thing | Rule |
|---|---|
| Function name | `snake_case`, starts with a verb: `build_event()`, `parse_frame()` |
| Boolean name | Starts with `is_`, `has_` or `should_`: `is_node_ready` |
| Class name | `PascalCase`, role suffix when applicable: `SessionManager`, `EventBuilder` |
| Constant | `UPPER_SNAKE_CASE`: `MAX_RETRY_COUNT` |
| File name | `snake_case.py`, named after its main responsibility: `session_manager.py` |
| Units | Include units when necessary: `timeout_s`, `rate_hz`, `duration_ns`, `size_bytes` |
| Magic values | Use named constants or enums instead of unexplained numbers or strings |
| Line length | **88 characters** (Black default) |
| Whitespace and layout | Formatted by **Black**. Run `black .` before every commit |
| Docstrings | Required for public functions, classes and modules |
| C doc comments | `/** */` with `@param` and `@return` on every public declaration in a `.h` ([Writing doc comments](#writing-doc-comments)) |
| Module header | Contains `Module`, `Purpose` and `Owner` |
| Implements | Include requirement references where applicable, using actual BR IDs |
| TODO | `# TODO(owner): description` or a ticket reference |
| Banned words | `server`, `sniffer`, `probe` as Wiresploit component names, subject to approved terminology exceptions |
| Wire and Python case | `snake_case` everywhere. `camelCase` only inside TypeScript |

---

## Index
1. [Naming](#1-naming)
2. [Functions](#2-functions)
3. [Classes and modules](#3-classes-and-modules)
4. [Comments and docstrings](#4-comments-and-docstrings)
5. [Errors and logging](#5-errors-and-logging)
6. [Formatting, imports and constants](#6-formatting-imports-and-constants)
7. [Tests](#7-tests)
8. [Code review and definition of done](#8-code-review-and-definition-of-done)
9. [Enforcement with tools](#9-enforcement-with-tools)
10. [References](#10-references)


---

## 1. Naming

Follow PEP 8 and the Google Python Style Guide.

| Element | Convention | Example |
|---|---|---|
| Variables, functions, methods | `snake_case` | `node_id`, `start_session()` |
| Classes | `PascalCase` | `SessionManager` |
| Exceptions | `PascalCase`, ending in `Error` | `ClockDriftError` |
| Constants | `UPPER_SNAKE_CASE` | `MAX_RETRY_COUNT` |
| Modules, files, packages | `snake_case` | `event_builder.py` |
| Private details | One leading underscore | `_build_path()` |

### 1.1 Functions and methods

Start with a verb that tells the reader what kind of operation it is.

| Prefix | Meaning | Example |
|---|---|---|
| `get_` | Return something already known | `get_session_path()` |
| `find_` | Search; may return `None` | `find_burst_by_seq()` |
| `compute_` / `derive_` | Calculate | `derive_sample_rate()` |
| `build_` | Construct an object | `build_capture_plan()` |
| `parse_` | Turn text or bytes into a structure | `parse_info_file()` |
| `is_` / `has_` / `should_` | Return a boolean | `is_burst_complete()` |
| `read_` / `write_` / `send_` | Perform I/O | `write_info_file()` |
| `handle_` | Handle a request or event | `handle_capture_request()` |
| `start_` / `stop_` | Lifecycle operation | `start_session()` |

Rules:

- The name says what the function does, not how: `decode_i2c_frame()`, not `process_data()` or `handle_data()`.
- A function whose name contains "and" may be doing multiple things. Review whether its responsibilities should be separated.
- A function that performs I/O uses an I/O verb (`read_`, `write_`, `send_`).
- Pure calculations do not perform I/O.

### 1.2 Classes

Use `PascalCase`. A class that has a role ends with the appropriate role suffix. Data classes use plain nouns.

| Suffix | Responsibility | Example |
|---|---|---|
| `Manager` | Coordinates related operations or state | `SessionManager` |
| `Watcher` | Monitors for a condition | `BurstWatcher` |
| `Builder` | Constructs a structured object | `EventBuilder` |
| `Sender` | Sends data | `EventSender` |
| `Store` | Owns persistence | `SessionStore` |
| `Writer` | Writes one data format | `InfoFileWriter` |
| `Client` | Talks to an external system | `CoreClient`, `WorkerClient` |
| `Detector` | Detects a condition | `BurstDetector` |
| `Plugin` | A replaceable capability | `UartPlugin` |

Data classes use descriptive nouns:

- `Event`
- `BurstRecord`
- `InfoFile`
- `CapturePlan`

Exceptions end in `Error`, for example `ClockDriftError`.

Do not add role suffixes when they do not describe a real responsibility.

### 1.3 Variables, constants and booleans

- Use `snake_case` for variables and `UPPER_SNAKE_CASE` for constants.
- Names must be descriptive: `node_id`, not `n`. Single letters are acceptable for short loop indexes.
- Avoid unexplained abbreviations.
- Collection names should be plural: `events`, `buses`, `results`.
- Include the unit in the name when it removes ambiguity: `_ns`, `_hz`, `_bytes`, `_ms`, `_s`.
- Do not include types in names (`str_name`, `list_events`). Use type hints instead.
- Boolean names must start with `is_`, `has_` or `should_`, as required by BRD §4. Avoid `can_` and bare names such as `valid` or `ready`.
- A private helper starts with one underscore: `_build_path()`.
- Do not use unexplained magic numbers or strings. Use named constants or enums for fixed domain values.
- Avoid duplicating domain values throughout the code.

Example:

```python
MAX_NODES = 16
DEFAULT_RETRY_COUNT = 5

if node_count > MAX_NODES:
    raise NodeLimitError("Maximum node count exceeded")
```

Do not create constants for values that have no domain meaning, such as `index + 1`.

### 1.4 Files, modules and packages

- Python files use `snake_case.py`.
- A file is named after its main class (`SessionManager` belongs in `session_manager.py`) or after what it provides (`sample_rate.py`).
- Keep one cohesive responsibility per file.
- Small, closely related data classes may share a file.
- Do not create a file for a one-off helper.
- Packages use lowercase `snake_case`: `node_agent`, `pi5la`.
- Code shared by the Node Agent and pi5la goes in a shared package, never copied between them.
- The shared package must not import from either program.

Every Python module must start with a module header containing:

```python
"""
Module: event_builder
Purpose: Build normalized communication events.
Owner: <team member or team>
"""
```

### 1.5 Terminology

Use the same term for the same concept throughout Wiresploit.

| Term | Meaning |
|---|---|
| `Core` | The central Wiresploit system |
| `Backend` | Only the component named Backend in the approved architecture |
| `Node` | A deployed Wiresploit device with capture and actions functions |
| `DUT` | Device Under Test |
| `Capture` | One of the Node's two functions |
| `Actions` | The Node's other function, responsible for performing configured actions |
| `Online` | A defined Node state |
| `Ready` | A defined Node state |
| `Running` | A defined Node state |
| `Unreachable` | A defined Node state |
| `bus` | The bus identifier, with the type from the approved schema |
| `bus_name` | A user-defined bus label |
| `channel` | A sample or wire channel index |
| `burst` | A captured group of samples |
| `seq_num` | The sequence number of a burst |
| `event` | A normalized decoded communication event |
| `packet` | A raw or network packet, when that is the actual data |
| `burst record` | The record describing a completed burst |
| `info file` | The metadata file beside a burst |
| `capture plan` | The configuration passed to the capture process |
| `session folder` | The folder holding one capture session's files |
| `protocol plug-in` | Node-side decoder from raw samples to events |
| `engine` | Core-side analysis program that consumes events and returns a result |

---

## 2. Functions

### 2.1 Shape

- A function does one thing and is short enough to read without unnecessary scrolling.
- Size and complexity limits are enforced by tools (§9), not by manually counting lines during review.
- Many parameters may indicate that a function does too much. If related values travel together, consider grouping them in a data class.
- Keep nesting shallow. Use early returns and helper functions where they improve readability.
- Never use mutable default arguments (`[]`, `{}`).
- Type hints are required on public functions and encouraged elsewhere.
- Return results instead of unnecessarily modifying caller-owned variables.

### 2.2 How to split a function

Review a function for separation when it combines multiple responsibilities, such as:

- Validating input.
- Calculating values.
- Performing I/O.
- Building an object.
- Deciding what to do next.

Guidelines:

1. **Separate calculation from I/O.** A pure function takes values and returns a value. It is easier to test.
2. **One level of detail per function.** A coordinating function should read like a list of steps. Detailed implementation belongs in helpers.
3. **Flatten with early returns.** Handle error cases first where appropriate.
4. **Name the step.** If a comment is needed to explain a substantial block of logic, consider extracting it into a function.

Before:

```python
def handle_capture_request(request, store, client):
    if request is not None:
        if request.buses:
            rate = 0
            for bus in request.buses:
                rate = max(rate, bus.baud_rate * 8)
            plan = {"rate": rate, "buses": request.buses}
            store.write(plan)
            client.send(plan)
```

After:

```python
def derive_sample_rate_hz(buses: list[BusConfig]) -> int:
    """Return the highest sample rate any bus needs."""
    return max(bus.baud_rate * SAMPLES_PER_BIT for bus in buses)


def handle_capture_request(
    request: ConfigRequest,
    store: SessionStore,
    client: CoreClient,
) -> None:
    """Validate a capture request, store its plan and report it."""
    if not request.buses:
        raise InvalidRequestError("no buses configured")

    plan = build_capture_plan(
        request,
        derive_sample_rate_hz(request.buses),
    )
    store.write_capture_plan(plan)
    client.send_capture_plan(plan)
```


---

## 3. Classes and modules

### 3.1 When to create a class

Create a class when the concept:

- Has identity or a lifecycle.
- Owns state that must stay consistent.
- Manages a resource, such as a connection, file store or device.
- Represents a replaceable role or capability.

Use a function for a stateless operation.

Do not create a class merely to group unrelated functions.

### 3.2 Shape

A class has one responsibility and one main reason to change.

A class that grows large may be doing too much. Split it by responsibility where appropriate.

Do not mix capture, decoding, persistence, transport and API logic in one class.

Size and complexity limits are enforced by tools (§9).

Order of members inside a class:

1. Class constants.
2. `__init__`.
3. Public methods.
4. Private methods (`_name`).

Prefer `@dataclass(frozen=True)` for value objects.

Note: `frozen=True` does not make nested lists or dictionaries immutable.

### 3.3 Interfaces and plug-ins

| Situation | Use |
|---|---|
| One implementation | Concrete class |
| Several implementations of one capability | `typing.Protocol` |
| Shared base behavior plus required methods | `abc.ABC` |
| Stateless operation | Function |
| No need to substitute | No abstraction |

Rules:

- Protocol plug-ins use `typing.Protocol`.
- `Protocol` does not validate implementations at runtime. The loader must check required methods and metadata and fail clearly when validation fails.
- One registry maps protocol names to plug-ins.
- Do not write `if protocol == "uart"` outside a plug-in or the registry.
- Protocol decoding belongs only in protocol plug-ins.
- Engines never decode UART, I2C, SPI or other capture protocols.
- A plug-in must not read files, use the network or read the clock while decoding.

### 3.4 Dependency injection

A class receives the objects it needs instead of constructing important dependencies internally.

```python
class EventSender:
    def __init__(
        self,
        transport: EventTransport,
        clock: Clock,
    ) -> None:
        self.transport = transport
        self.clock = clock
```

Rules:

- Inject external dependencies such as clients, stores, clocks and hardware adapters.
- Constructors must not perform I/O or start threads.
- Use explicit `start()` and `stop()` methods for lifecycle operations.
- Avoid hidden global state.
- Do not inject every small helper. Inject dependencies when doing so improves testing or substitution.

---

## 4. Comments and docstrings

Comments explain **why**, not what.

Example:

```python
# Fluent Bit may refuse requests while it flushes its buffer.
retry_count += 1
```

### Docstrings

- Public functions, classes and modules must have docstrings.
- Follow the docstring requirements in BRD §4.
- Public functions document `Args`, `Returns` and `Raises` where applicable.
- Leave out sections that have nothing meaningful to say. Do not write filler.
- Include units in `Args` and `Returns` for time, frequency, voltage, sample counts and sizes where applicable.
- Include an `Implements:` line only where applicable.
- Use actual BR requirement IDs, such as `BR-MON-02`, `BR-MON-04` and `BR-ENV-01`.
- Never invent a BR ID.
- Private helper functions do not require docstrings under this guideline unless additional documentation is useful for clarity.

### Module headers

Every module starts with a header docstring containing:

- `Module`: The module name.
- `Purpose`: What the module provides.
- `Owner`: The responsible team member or team.

Example:

```python
"""
Module: event_builder
Purpose: Build normalized communication events.
Owner: Software Team

Implements: BR-MON-02
"""
```

Add `Implements:` only when the module implements an identifiable requirement.

### Additional comment rules

- Comment protocol behavior, timing assumptions, bit manipulation, safety constraints and workarounds.
- Update comments in the same change as the associated code.
- Do not leave commented-out code.
- Every TODO must include an owner or a ticket reference.
- Place comments on separate lines above the relevant code. Do not use trailing inline comments.
- When changing an existing comment, reference the previous comment in the change or review explanation and explain why it was changed.

Example of a comment change:

If an existing comment says that an operation retries three times, but the approved behavior changes to five retries, explain the change by referencing the previous three-retry comment and the reason for the new limit.

Example:

```python
# Previous comment: Retry the operation up to 3 times.
# Updated to 5 retries to match the approved retry policy.
for attempt in range(MAX_RETRY_COUNT):
    ...
```

### Docstring example

```python
"""Correlate network and bus events.

Implements: BR-MON-02
"""


def correlate_events(
    network_events: list[Event],
    bus_events: list[Event],
) -> list[CommunicationBlock]:
    """Correlate network and bus events into time-ordered blocks.

    Args:
        network_events: Decoded network events.
        bus_events: Decoded onboard-bus events.

    Returns:
        Communication blocks ordered by timestamp.

    Raises:
        ClockDriftError: If the time sources exceed the allowed drift.

    Implements: BR-MON-02
    """
    ...


def _is_within_window(timestamp_ns: int, window_ns: int) -> bool:
    return ...
```

The private helper does not require a docstring under this guideline.

### Writing doc comments

Doc comments are the source of the generated code reference on this site
(Node › Reference). Nobody edits those pages by hand: fix the comment, and the
page follows at the next build.

**Python** uses Google-style docstrings, as described above. The `mkdocstrings`
plugin renders them, with `docstring_style: google`.

- The first line is one sentence that says what the function does or returns.
- Then `Args:`, `Returns:` and `Raises:`, each only when it has something to say.
- Docstrings are rendered as Markdown. Put placeholders such as `<file>.sr` in
  double backticks (` ``<file>.sr`` `), or the page drops them as HTML tags.
- A module's docstring starts with its header: `Module`, `Purpose`, `Owner`.

**C** puts its doc comments in the header (`.h`), on every public function,
struct, struct member, enum value and macro. Doxygen reads them, and the
`mkdoxy` plugin renders them. A `.c` file's private helpers keep plain `/* */`
comments.

- A doc comment opens with `/**`. A plain `/*` comment does not appear in the
  reference.
- The first sentence is the summary line, so make it a complete sentence.
- Document every parameter with `@param name text` and every non-void return
  with `@return text`. Give units and the error convention (`0`, or `-1` with
  a message on stderr).
- Each header starts with an `@file name.h` block. `@brief` gives the summary,
  and the paragraphs after it describe the module.
- Struct members get their own `/** */` comment on the line above, like any
  other comment.
- Use `@code` and `@endcode` for diagrams and examples, and backticks for code
  in running text.

```c
/**
 * Read the FDEBUG register.
 *
 * @param fd    Handle from fdebug_open().
 * @param value Receives the register value.
 * @return 0 on success, -1 on error (errno set).
 */
int fdebug_read(int fd, uint32_t *value);

/** Result of analyze_runs(): run lengths of one channel. */
typedef struct {
    /** Complete runs examined (first and last run excluded). */
    uint64_t runs;
} run_stats_t;
```

**Check and preview.** `node/scripts/docs.sh build` runs Doxygen with
`node/capture/Doxyfile` first and fails on any Doxygen warning, for example an
undocumented public declaration or a `@param` that names no parameter. Then it
builds the whole site into `site/`. To read the pages while you write:

```bash
node/scripts/docs.sh serve --watch node   # http://127.0.0.1:8000
# from a container started with docker run, publish the port:
docker run --rm -it -u "$(id -u):$(id -g)" -p 8000:8000 -v "$PWD":/work -w /work \
    wiresploit-node node/scripts/docs.sh serve --watch node --dev-addr 0.0.0.0:8000
```

The reference pages are under Node › Reference. `--watch node` makes the
preview rebuild when a header or a Python file changes, not only when a page
in `docs/` does. `serve` does not run the Doxygen warning check: run
`docs.sh build` before you commit.

---

## 5. Errors and logging

### Errors

- Catch an exception only to handle it, add context or translate it.
- Never use a bare `except:`.
- Do not catch everything just to keep the program running.
- Never silently ignore capture, decoding, persistence or transmission failures.
- Translate exceptions using `raise NewError(...) from exc`.
- Do not use `assert` to validate input or enforce production conditions. Raise an appropriate exception.
- Log an exception once, in the layer responsible for handling it.
- Keep captured evidence when a downstream component is unavailable.
- Each retried operation has one owner.
- Define retryable errors, maximum attempts, backoff and behavior after the retry limit.

Example:

```python
try:
    session_store.save_event(event)
except StorageError as exc:
    raise EventPersistenceError(event.event_id) from exc
```

### Logging

- Use Python's `logging` module, not `print()`.
- Use appropriate logging levels:

| Level | Purpose |
|---|---|
| `DEBUG` | Detailed diagnostic information |
| `INFO` | Important state changes and normal operations |
| `WARNING` | Recoverable problems and retries |
| `ERROR` | Failed operations |
| `CRITICAL` | Conditions where the system cannot continue |

- Use structured fields (`session_id`, `bus`, `seq_num`) so logs are searchable.
- Never log secrets or complete sensitive payloads.

### General principle: Clarity over cleverness

- Prefer straightforward and readable implementations over unnecessarily clever solutions.
- Avoid unnecessary abstractions, deeply nested logic and complicated expressions.
- Code should be understandable to another team member without requiring excessive explanation.
- Prefer maintainability and clarity over reducing the number of lines.

---

## 6. Formatting, imports and constants

### Formatting

- Four spaces per indentation level. No tabs.
- One statement per line.
- Use parentheses for multiline expressions, not backslashes.
- The formatter is **Black** (§9).
- Run Black before every commit.
- Do not manually reformat code against Black's output.

### Imports

- Remove unused imports.
- Do not use wildcard imports.
- Prefer absolute imports.
- Group imports into three blocks, separated by a blank line:
  1. Standard library.
  2. Third-party libraries.
  3. Wiresploit modules.

Example:

```python
import json
from pathlib import Path

from pydantic import BaseModel

from node_agent.models.event import Event
```

### Constants and configuration

- Do not use unexplained magic numbers or strings.
- Use named constants or enums for fixed domain values.
- Define each domain value once and reuse it.
- Do not create constants for values without domain meaning, such as `index + 1`.
- Values that differ per deployment come from configuration.
- Secrets must never be stored in source code.
- Include units in names where necessary.

### Whitespace

Black applies PEP 8 whitespace automatically (§9).

The rules to know when reading code:

- Two blank lines between top-level functions and classes.
- One blank line between methods.
- At most one blank line inside a function where appropriate.
- One space around operators and after commas.
- No spaces just inside brackets or before a call's opening bracket.
- Keyword arguments have no spaces: `f(timeout_s=5)`.
- Parameters with type hints and defaults use spaces: `timeout_s: int = 5`.
- Comments use `# ` for block comments.
- Comments appear above the relevant code, not at the end of a code line.
- Multiline lists, calls and parameter lists use trailing commas.
- No trailing whitespace.
- Files end with one newline.

---

## 7. Tests

- Name tests by condition and expected result:

  `test_start_session_rejects_unreachable_node`

- Test one behavior at a time.
- Keep test data small and understandable.
- Tests must be deterministic.
- Use fakes for clocks, transports, capture devices and repositories.
- Unit tests must not require live Elasticsearch or physical hardware.
- Test normal behavior, boundaries and important failure cases.
- A bug fix must include a regression test.
- Test files live in the `tests/` folder of their program and mirror the module names.

---

## 8. Code review and definition of done

A change is ready when:

- [ ] It meets the requirement and approved design.
- [ ] Names follow §1: verb-first functions, prefixed booleans, role suffixes and appropriate units.
- [ ] No prohibited Wiresploit component terminology is used.
- [ ] Ruff passes, so configured size and complexity limits are met.
- [ ] Each function and class has a clear responsibility.
- [ ] Calculation is separated from I/O where appropriate.
- [ ] Layer boundaries are respected.
- [ ] Public functions, classes and modules have the required docstrings.
- [ ] Module headers contain Module, Purpose and Owner.
- [ ] `Implements:` references are included where applicable and use actual BR IDs.
- [ ] TODO comments contain an owner or ticket reference.
- [ ] Comments explain why, are placed above the relevant code and are updated with code changes.
- [ ] No commented-out code remains.
- [ ] No unexplained magic numbers or strings remain.
- [ ] Time handling follows BR-MON-04.
- [ ] Local time and UTC are not mixed within a module.
- [ ] Node terminology, functions and states follow the BRD.
- [ ] Communication Block (CB) and Snapshot Block (SB) terminology is consistent.
- [ ] Errors are handled as in §5. No silent failures.
- [ ] Tests cover the change and its important failure cases.
- [ ] Formatter, linter, type checker and tests pass.
- [ ] A reviewer approved it.

Reviewers check correctness, clarity and maintainability, not personal taste.

Authors explain non-obvious decisions and separate required fixes from suggestions.

---

## 9. Enforcement with tools

Rules that a person has to check cost review time. Rules that a tool checks can be enforced consistently.

Use:

- **Black:** Formatter.
- **Ruff:** Linter.
- **mypy:** Type checker.

Pin tool versions in the repository so the team uses consistent configurations.

### 9.1 Black (formatter)

Black formats whitespace, line breaks, quotes and trailing commas.

Install:

```bash
pip install black
```

Use:

```bash
black .            # Format all files in place
black --check .    # Check formatting without changing files
black --diff .     # Show formatting differences
```

Rules:

- Run `black .` before every commit.
- Use one formatter only.
- Do not run Ruff's formatter on the same files.
- Pin the Black version in the repository.
- Editors should enable format-on-save with Black.

### 9.2 Ruff (linter) and configuration

Example proposed configuration:

```toml
# pyproject.toml [proposed]

[tool.black]
line-length = 88
target-version = ["py311"]

[tool.ruff]
line-length = 88
target-version = "py311"

[tool.ruff.lint]
select = ["E", "F", "I", "N", "D", "B", "C90", "PLR", "UP"]
ignore = [
    "E501",     # Black handles line length; it cannot split long strings
]

[tool.ruff.lint.per-file-ignores]
"tests/**" = ["D", "PLR"]

[tool.ruff.lint.pydocstyle]
convention = "google"
```

**Configuration notes:**

- `E` and `F`: Common errors and code-quality issues.
- `I`: Import sorting.
- `N`: Naming conventions.
- `D`: Docstring checks.
- `B`: Common bug patterns.
- `C90`: McCabe complexity.
- `PLR`: Pylint refactor rules, including selected size and complexity checks.
- `UP`: Python modernization suggestions.
- `E501` is ignored because Black handles line formatting, although it cannot split every long string.
- `PLR2004` is deliberately not ignored, so Ruff can flag magic values in comparisons.

**Important:** `PLR2004` does not detect every unexplained numeric or string literal. The no-magic-values rule still requires code review or additional project-specific checks.

Before adopting this configuration:

1. Run `black .` on the existing code.
2. Run `ruff check .`.
3. Review the warnings.
4. If a rule produces excessive or irrelevant warnings, evaluate whether it should be adjusted.
5. Add rules gradually where necessary.

Do not leave a noisy rule enabled without evaluating its usefulness.

Pin the Python version and ensure language features match it. For example, `X | None` requires Python 3.10 or later.



## 10. References

- [PEP 8 — Style Guide for Python Code](https://peps.python.org/pep-0008/)
- [PEP 257 — Docstring Conventions](https://peps.python.org/pep-0257/)
- [Google Python Style Guide](https://google.github.io/styleguide/pyguide.html)
- [Python `typing.Protocol`](https://docs.python.org/3/library/typing.html#typing.Protocol)
- [Google Engineering Practices — Code Review](https://google.github.io/eng-practices/review/)
- [Ruff documentation](https://docs.astral.sh/ruff/)
- Wiresploit BRD §4 — Coding Standards & Commenting Guidelines
- Approved Core and Node design documents
