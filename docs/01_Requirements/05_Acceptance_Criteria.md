# Acceptance Criteria (AC)

| Field             | Value                                                        |
|-------------------|--------------------------------------------------------------|
| Project Name      | IoT Reconnaissance & Communication-Block Monitoring System   |
| Working Title     | Wiresploit                                                   |
| Document Status   | `Draft` — pending sponsor approval                             |
| Date              | 17-7-2026                                                    |
| Prepared by       | Mohamed Salah-elden                                          |
| Related Documents | 00_BRD.md, 01_Charter.md                                     |

---

## Purpose

This document defines testable Acceptance Criteria (AC) for every Business Requirement (BR) listed in the BRD (Section 5). Each BR is broken down into one or more AC items that can be individually verified during User Acceptance Testing (UAT), per BR-ACC-01. Most criteria are written as measurable statements; requirements involving multi-step behavior or conditional logic use a Given/When/Then format for precision.

**Legend:**
- **Format: Measurable** — a plain, quantifiable/verifiable statement.
- **Format: G/W/T** — Given/When/Then scenario.

---

## 1. Core Monitoring & Correlation

### BR-MON-01 — Unified live multi-protocol view

| AC ID | Format | Criteria |
|---|---|---|
| AC-MON-01.1 | Measurable | The live UI displays network, onboard-bus (I2C/SPI/UART/GPIO), and wireless (BT/LoRa/RFID/etc.) captures simultaneously in a single screen/session, without requiring a separate tool or window per protocol. |
| AC-MON-01.2 | G/W/T | **Given** at least two Nodes with capture enabled, of different protocol types, are connected and active, **when** the analyst opens a live session, **then** events from both protocols appear on the same unified view without manual switching. |

### BR-MON-02 — Correct causal/time ordering

| AC ID | Format | Criteria |
|---|---|---|
| AC-MON-02.1 | G/W/T | **Given** a DUT action triggers both a network event and an internal bus event, **when** both are captured, **then** the system displays them in the unified timeline ordered by their true chronological occurrence, not by arrival/ingestion order. |
| AC-MON-02.2 | Measurable | Events sharing a common trigger (e.g., a login attempt) are visually grouped or linked on the timeline as a correlated set. |

### BR-MON-03 — Live correlation latency

| AC ID | Format | Criteria |
|---|---|---|
| AC-MON-03.1 | Measurable | The system publishes a documented maximum latency (in ms) between an event's occurrence and its appearance on the live correlated timeline, validated against the selected hardware. |
| AC-MON-03.2 | Measurable | In a test run of N events (N to be defined in the test plan), at least 95% of events are displayed within the documented latency threshold. |

### BR-MON-04 — Common time reference / clock sync

| AC ID | Format | Criteria |
|---|---|---|
| AC-MON-04.1 | Measurable | All Nodes with capture enabled, and the Core, synchronize to a local/on-premises NTP or PTP server with no dependency on external internet connectivity. |
| AC-MON-04.2 | Measurable | The system documents a maximum clock drift value (e.g., in µs/ms) between any two Nodes with capture enabled and the Core, verified by test measurement. |
| AC-MON-04.3 | Measurable | Time-sync status (in-sync / drifted / lost) is visible to the analyst for each connected node. |

### BR-MON-05 — Documented capacity limits

| AC ID | Format | Criteria |
|---|---|---|
| AC-MON-05.1 | Measurable | The system documentation states the maximum number of simultaneous Nodes/protocols supported without degradation in correlation accuracy or timeline responsiveness. |
| AC-MON-05.2 | Measurable | A load test at the documented maximum shows no measurable increase in correlation latency beyond the threshold defined in BR-MON-03. |

### BR-MON-06 — Camera trigger conditions

| AC ID | Format | Criteria |
|---|---|---|
| AC-MON-06.1 | G/W/T | **Given** a trigger event/signal is configured, **when** that event occurs, **then** the camera begins recording automatically without operator intervention. |
| AC-MON-06.2 | G/W/T | **Given** a configured delay period following a trigger, **when** the trigger fires, **then** recording starts after the exact configured delay (within a documented tolerance). |
| AC-MON-06.3 | G/W/T | **Given** a configured packet/pattern match condition, **when** a matching packet is captured, **then** recording starts automatically. |

### BR-MON-07 — Video-timeline correlation

| AC ID | Format | Criteria |
|---|---|---|
| AC-MON-07.1 | Measurable | Every recorded video segment is stored with a timestamp aligned to the same time reference used by BR-MON-04. |
| AC-MON-07.2 | G/W/T | **Given** a recorded session containing a video segment, **when** the analyst views the timeline, **then** the video segment appears positioned at its correct correlated point relative to other captured events. |

### BR-MON-08 — Automated video behavior summary

| AC ID | Format | Criteria |
|---|---|---|
| AC-MON-08.1 | G/W/T | **Given** a recorded video segment linked to a triggering event, **when** the analyst requests analysis, **then** the system generates a descriptive text summary of observed physical behavior (e.g., motor movement, LED state change). |
| AC-MON-08.2 | Measurable | The generated summary is displayed attached to (or linked from) the triggering event on the timeline. |

### BR-MON-09 — Node functions

| AC ID | Format | Criteria |
|---|---|---|
| AC-MON-09.1 | G/W/T | **Given** a Node with capture enabled and actions disabled, **when** a session is running, **then** the Node reports capture events and the system rejects any action command sent to it. |
| AC-MON-09.2 | G/W/T | **Given** a Node with capture disabled and actions enabled, **when** the analyst confirms an action, **then** the Node performs that action and contributes no capture events. |
| AC-MON-09.3 | G/W/T | **Given** a Node with both capture and actions enabled, **when** a session is running and the analyst confirms an action, **then** the Node reports capture events and performs the action. |

### BR-MON-10 — Node general settings and color

| AC ID | Format | Criteria |
|---|---|---|
| AC-MON-10.1 | Measurable | Each Node has general settings consisting of a Node ID, a Node name, a Node description, and a color, and no two Nodes in the same session share that color. |
| AC-MON-10.2 | G/W/T | **Given** two Nodes with different unique colors, **when** the analyst views the Node list on the Core, **then** each list entry is shown in that Node's color. |
| AC-MON-10.3 | G/W/T | **Given** a Node whose general settings assign a unique color, **when** that Node is connected, **then** its indication LED lights in the same color shown for it on the Core. |

### BR-MON-11 — Capture protocol settings

| AC ID | Format | Criteria |
|---|---|---|
| AC-MON-11.1 | G/W/T | **Given** a Node with capture enabled, **when** the analyst selects a protocol and saves that protocol's settings, **then** the Node captures that protocol using the saved settings. |
| AC-MON-11.2 | Measurable | Changing the selected protocol changes which protocol's events that Node reports. |

### BR-MON-12 — Node state machine

| AC ID | Format | Criteria |
|---|---|---|
| AC-MON-12.1 | G/W/T | **Given** a Node that has just connected and has not been configured, **when** the Core reads its Node ID, **then** the Core assigns a unique color, adds the Node to the Node list, and shows the state Online. |
| AC-MON-12.2 | G/W/T | **Given** a Node in Online, **when** the analyst saves its configuration, **then** the state becomes Ready and the Node is not yet capturing or performing actions. |
| AC-MON-12.3 | G/W/T | **Given** a Node in Ready, **when** it starts capturing, decoding, matching packets, performing actions, or sending data packets, **then** the state becomes Running. |
| AC-MON-12.4 | G/W/T | **Given** a Node in Online, Ready, or Running, **when** its heartbeat times out, **then** the Core shows Unreachable without a state report from the Node. |
| AC-MON-12.5 | G/W/T | **Given** an Unreachable Node that was not configured, **when** it reconnects, **then** its state is Online. |
| AC-MON-12.6 | G/W/T | **Given** an Unreachable Node that was already configured, **when** it reconnects, **then** its state is Ready. |

---

## 2. Session Recording, Analysis & Reporting

Analysis engines use the categories in BR-ANA-09. A domain is a protocol. Recording, export, and usage metrics are not engines.

| Scope | Group | Covered by |
|---|---|---|
| Single session | Single domain | BR-ANA-02, BR-ANA-05, BR-ANA-07, BR-ANA-08, and IP/domain name detection |
| Single session | Multiple domain | BR-ANA-03 behavioral analysis |
| Single session | Active | Packet replay, packet injection, and fuzzing (BR-ACT) |
| Multiple session | Single domain | Memory comparison |
| Multiple session | Multiple domain | Baseline analysis and pattern detection |

### BR-ANA-01 — Record & playback sessions

| AC ID | Format | Criteria |
|---|---|---|
| AC-ANA-01.1 | Measurable | A full test session (all protocols captured during that session) can be saved to persistent storage and reloaded for playback at a later time. |
| AC-ANA-01.2 | Measurable | Playback reproduces the original event order and timestamps as recorded, without data loss. |

### BR-ANA-02 — Search for sensitive info in a session

| AC ID | Format | Criteria |
|---|---|---|
| AC-ANA-02.1 | G/W/T | **Given** a recorded session containing a known plaintext credential/token, **when** the analyst runs a search/scan, **then** the system identifies it and shows its exact location (packet/timestamp/protocol) within the session. |
| AC-ANA-02.2 | Measurable | Search supports both known-pattern detection (e.g., credential formats) and analyst-defined custom search phrases. |

### BR-ANA-03 — Behavior diagram generation

| AC ID | Format | Criteria |
|---|---|---|
| AC-ANA-03.1 | Measurable | From a recorded session, the analyst can generate a visual diagram representing observed device behavior/flow. |
| AC-ANA-03.2 | Measurable | The generated diagram is exportable in a format usable directly in a client report (e.g., PNG/SVG/PDF). |

### BR-ANA-04 — Export for client reporting

| AC ID | Format | Criteria |
|---|---|---|
| AC-ANA-04.1 | Measurable | Session data and findings can be exported in at least one report-ready format (e.g., PDF, DOCX, or structured JSON/CSV for further processing). |
| AC-ANA-04.2 | Measurable | Exported output includes timestamps, protocol source, and any flagged findings (e.g., detected secrets from BR-ANA-05). |

### BR-ANA-05 — Auto-flag clear-text sensitive data

| AC ID | Format | Criteria |
|---|---|---|
| AC-ANA-05.1 | G/W/T | **Given** a captured packet contains plaintext sensitive data (e.g., credentials/tokens), **when** capture/analysis completes, **then** the packet is automatically flagged and visually distinguished on the timeline without manual search. |
| AC-ANA-05.2 | Measurable | Flagged items are filterable/listable as a dedicated view (e.g., "Findings" panel) separate from the full timeline. |

### BR-ANA-06 — Usage/ROI metrics

| AC ID | Format | Criteria |
|---|---|---|
| AC-ANA-06.1 | Measurable | The system tracks and reports, at minimum: number of sessions conducted, average time-to-finding, and number of secrets/credentials automatically detected. |
| AC-ANA-06.2 | Measurable | Metrics are viewable in an aggregated dashboard/report covering a selectable time period. |

### BR-ANA-07 — Memory reconstruction from bus captures

| AC ID | Format | Criteria |
|---|---|---|
| AC-ANA-07.1 | G/W/T | **Given** a recorded session with SPI/I2C flash or addressable memory transactions, **when** the analyst requests reconstruction, **then** the system aggregates observed address/data pairs into a unified memory map/dump. |
| AC-ANA-07.2 | Measurable | The reconstructed memory map is exportable (e.g., as a binary/hex dump file) for external review. |

### BR-ANA-08 — Reconstruction completeness indication

| AC ID | Format | Criteria |
|---|---|---|
| AC-ANA-08.1 | Measurable | The reconstructed memory map visually distinguishes three states per address range: fully observed, partially observed, never captured. |
| AC-ANA-08.2 | Measurable | A summary statistic (e.g., % of address space fully observed) is displayed alongside the memory map. |

### BR-ANA-09 — Analysis engine categories

| AC ID | Format | Criteria |
|---|---|---|
| AC-ANA-09.1 | Measurable | Each analysis engine is presented in one of these groups: single session / single domain, single session / multiple domain, single session / active, multiple session / single domain, or multiple session / multiple domain. |
| AC-ANA-09.2 | G/W/T | **Given** one recorded session and one selected protocol, **when** the analyst runs IP/domain name detection or memory reconstruction, **then** the result uses only that session and that protocol. |
| AC-ANA-09.3 | G/W/T | **Given** one recorded session containing more than one protocol, **when** the analyst runs behavioral analysis, **then** the result relates events across those protocols. |
| AC-ANA-09.4 | G/W/T | **Given** two sessions of the same protocol, **when** the analyst runs memory comparison, **then** the result compares that protocol's reconstructed memory across the two sessions. |
| AC-ANA-09.5 | G/W/T | **Given** more than one session and more than one protocol, **when** the analyst runs baseline analysis or pattern detection, **then** the result uses those sessions and protocols together. |

---

## 3. Active Triggering & DUT Control

### BR-ACT-01 — Controlled active reconnaissance with confirmation

| AC ID | Format | Criteria |
|---|---|---|
| AC-ACT-01.1 | G/W/T | **Given** an analyst initiates an active reconnaissance action, **when** the action is about to execute, **then** the system requires and logs explicit operator confirmation before proceeding. |
| AC-ACT-01.2 | Measurable | No active reconnaissance action executes without a prior, auditable confirmation step (no silent/automatic execution). |
| AC-ACT-01.3 | Measurable | The DUT's response to the action is captured and made available for analysis through the same platform/session. |

### BR-ACT-02 — Configurable auto-trigger conditions

| AC ID | Format | Criteria |
|---|---|---|
| AC-ACT-02.1 | Measurable | The operator can set a Node's actions trigger to manual, on packet match, scheduled, or on peer notification, without requiring code changes. |
| AC-ACT-02.2 | G/W/T | **Given** an armed on-packet-match, scheduled, or peer-notification trigger, **when** that condition is met during a session, **then** the associated action runs. |

### BR-ACT-03 — Trigger output action types

| AC ID | Format | Criteria |
|---|---|---|
| AC-ACT-03.1 | Measurable | On a Node whose actions function is enabled, the configured action is one of: inject a DUT payload, run a Node script, or send a notification signal. A notification signal can be sent to the Core, to all (the Core and the other Nodes), or to one specific Node. |
| AC-ACT-03.2 | G/W/T | **Given** a trigger and action are selected, **when** the analyst has not confirmed them, **then** the system does not arm or execute that action. |
| AC-ACT-03.3 | G/W/T | **Given** Node A sends a notification signal to Node B, and Node B's trigger is on peer notification, **when** Node B receives that signal, **then** Node B runs its configured action. |

---

## 4. Data Integrity, Security & Access Control

### BR-SEC-01 — Protection of data at rest

| AC ID | Format | Criteria |
|---|---|---|
| AC-SEC-01.1 | Measurable | Captured session data stored at rest is encrypted or otherwise access-controlled such that it cannot be read without proper authentication. |
| AC-SEC-01.2 | Measurable | Unauthorized access attempts (e.g., wrong credentials, missing permissions) are denied and logged. |

### BR-SEC-02 — Tamper-evidence for recorded sessions

| AC ID | Format | Criteria |
|---|---|---|
| AC-SEC-02.1 | G/W/T | **Given** a recorded session has been saved, **when** any part of its data is modified post-capture, **then** the system can detect and flag the modification (e.g., via hash/checksum/signature mismatch). |
| AC-SEC-02.2 | Measurable | Verification of session integrity is available as an explicit action/report the analyst (or client) can run on demand. |

### BR-SEC-03 — Role-based access control

| AC ID | Format | Criteria |
|---|---|---|
| AC-SEC-03.1 | Measurable | The system supports at least two distinct roles (e.g., viewer, analyst/editor) with different permissions for view/export/modify actions. |
| AC-SEC-03.2 | G/W/T | **Given** a user with view-only role, **when** they attempt to modify or export restricted session data, **then** the action is denied. |

### BR-SEC-04 — Encrypted/authenticated node-to-core communication

| AC ID | Format | Criteria |
|---|---|---|
| AC-SEC-04.1 | Measurable | Wireless communication between Nodes and the Core is encrypted and authenticated (e.g., TLS or equivalent), verified via traffic inspection showing no plaintext payload. |
| AC-SEC-04.2 | Measurable | A spoofed/unauthenticated node cannot successfully submit data accepted by the core. |

---

## 5. Non-Interference & Operating Environment

### BR-ENV-01 — No interference during passive monitoring

| AC ID | Format | Criteria |
|---|---|---|
| AC-ENV-01.1 | Measurable | With passive monitoring active and no reconnaissance action triggered, the DUT's measured behavior (timing, responses, state) is unchanged compared to a baseline run without the system attached. |
| AC-ENV-01.2 | Measurable | This criterion is verified separately from — and does not apply to — active reconnaissance actions under BR-ACT-01/03. |

### BR-ENV-02 — Fully air-gapped operation

| AC ID | Format | Criteria |
|---|---|---|
| AC-ENV-02.1 | G/W/T | **Given** the system is fully installed, **when** all external internet connectivity is disconnected, **then** live monitoring, correlation, session recording, and active reconnaissance features all remain fully operable. |
| AC-ENV-02.2 | Measurable | Only initial installation/setup (e.g., pulling Docker images per BR-DEP-01) is documented as requiring internet access; no runtime feature depends on it. |

---

## 6. Extensibility

### BR-EXT-01 — Support for additional protocols

| AC ID | Format | Criteria |
|---|---|---|
| AC-EXT-01.1 | Measurable | The architecture documentation describes a defined process/interface for adding a new protocol capture module without redesigning the core system. |
| AC-EXT-01.2 | Measurable | A proof-of-concept addition of one new protocol (beyond the initial set) can be integrated and demonstrated within an agreed effort/time bound. |

### BR-EXT-02 — Documented external integration interface

| AC ID | Format | Criteria |
|---|---|---|
| AC-EXT-02.1 | Measurable | A documented API or scripting interface exists, specifying how an external hardware capture tool can submit data as an additional source. |
| AC-EXT-02.2 | Measurable | A sample/reference integration (e.g., a script or stub tool) successfully submits data through the documented interface and it appears correctly on the unified timeline. |

---

## 7. Deployment & Delivery

### BR-DEP-01 — Containerized, reproducible deployment

| AC ID | Format | Criteria |
|---|---|---|
| AC-DEP-01.1 | Measurable | The core and its dependencies can be deployed using a documented Docker/Docker Compose configuration on a clean environment, with no manual environment setup steps beyond running the provided commands. |
| AC-DEP-01.2 | Measurable | Once deployed, the system operates fully offline per BR-ENV-02, with internet access needed only during the initial image pull/setup. |

### BR-DEP-02 — Config/data export-import independent of container lifecycle

| AC ID | Format | Criteria |
|---|---|---|
| AC-DEP-02.1 | G/W/T | **Given** a running deployment with configuration and recorded sessions, **when** the analyst exports this data, **then** a portable backup file/package is produced independent of the container instance. |
| AC-DEP-02.2 | G/W/T | **Given** a fresh deployment (new container instance), **when** the exported backup is imported, **then** all configuration and session data is restored correctly and is usable (playback, search, etc.). |

---

## 8. Acceptance, Adoption & Support

### BR-ACC-01 — UAT process against real DUT scenarios

| AC ID | Format | Criteria |
|---|---|---|
| AC-ACC-01.1 | Measurable | A formal UAT test plan exists, covering real DUT scenarios (e.g., an actual IP camera), and is executed and signed off before final delivery. |
| AC-ACC-01.2 | Measurable | Every BR-level AC in this document has a corresponding UAT test case with a pass/fail result recorded. |

### BR-ACC-02 — Training plan/material

| AC ID | Format | Criteria |
|---|---|---|
| AC-ACC-02.1 | Measurable | Training material (e.g., user guide, walkthrough, or session) covering system operation is delivered before full analyst adoption. |
| AC-ACC-02.2 | Measurable | At least one analyst outside the development team completes a supervised test session using only the training material and system documentation, without developer assistance. |

---

## Notes

- Exact numeric thresholds marked as "to be defined"/"to be validated" (e.g., latency values, node count limits, drift tolerance) should be finalized during the technical design phase and captured in the test plan referenced by BR-ACC-01.
- This document should be updated alongside the BRD if requirements change (traceability per BO-10).