# Business Requirements → Functional Requirements

## BR-MON — Monitoring

**Source BR:** BR-MON-01 to BR-MON-08,
**Assignee:** BK,
**Status:** In progress.

### Summary
The monitoring module provides a unified live view of DUT communications and device states, correlating events and snapshots on a synchronized timeline.

### Monitoring System Data Flow
```mermaid
flowchart TD

    DUT["Device Under Test (DUT)"]

    NET["Network Traffic<br/>HTTP / TCP / Ethernet / Wi-Fi"]
    BUS["Onboard Buses<br/>I2C / SPI / UART / GPIO"]
    WIRELESS["Wireless Communication<br/>Bluetooth / LoRa / RFID"]

    CN["Capture Nodes<br/>Capture + Timestamp Events"]

    BRAIN["Brain<br/>Event Processing + Correlation<br/>Time Synchronization"]

    TIMELINE["Unified Timeline<br/>Communication Blocks + Snapshot Blocks"]

    ANALYST["Analyst<br/>Monitor + Analyze DUT Behavior"]

    DUT --> NET
    DUT --> BUS
    DUT --> WIRELESS

    NET --> CN
    BUS --> CN
    WIRELESS --> CN

    CN --> BRAIN
    BRAIN --> TIMELINE
    TIMELINE --> ANALYST
```
### Functional Requirements

| FR ID | Requirement | Priority | Acceptance Criteria |
|---|---|---|---|
| FR-MON-01-1 | The system shall display network communication events. | Must | Network events are visible in the monitoring interface. |
| FR-MON-01-2 | The system shall display onboard-bus communication events. | Must | Supported onboard-bus events are visible in the monitoring interface. |
| FR-MON-01-3 | The system shall display wireless communication events. | Must | Supported wireless events are visible in the monitoring interface. |
| FR-MON-01-4 | The system shall provide a unified live view of captured communications. | Must | Events from supported sources appear in one live interface. |
| FR-MON-02-1 | The system shall correlate related communication events. | Must | Related events are grouped or linked correctly. |
| FR-MON-02-2 | The system shall order correlated events chronologically. | Must | Events appear in correct time order on the timeline. |
| FR-MON-03-1 | The system shall display live events within a documented latency limit. | Should | Measured event-to-display latency is documented and validated. |
| FR-MON-04-1 | The system shall synchronize Capture Nodes to a common time reference. | Must | All Capture Nodes use the configured common time reference. |
| FR-MON-04-2 | The system shall synchronize the Brain to the common time reference. | Must | The Brain uses the configured common time reference. |
| FR-MON-04-3 | The system shall document the maximum clock drift between monitoring components. | Must | Maximum observed clock drift is measured and documented. |
| FR-MON-05-1 | The system shall document the maximum supported number of simultaneous Capture Nodes. | Should | Maximum supported Capture Nodes are identified through testing. |
| FR-MON-05-2 | The system shall document the maximum supported number of simultaneous protocols. | Should | Maximum supported simultaneous protocols are identified through testing. |
| FR-MON-06-1 | The system shall support triggering a Snapshot Node based on a defined trigger event or signal. | Should | A configured trigger event or signal activates the Snapshot Node. |
| FR-MON-06-2 | The system shall support triggering a Snapshot Node after a defined delay. | Should | The Snapshot Node is triggered after the configured delay. |
| FR-MON-06-3 | The system shall support triggering a Snapshot Node based on a detected packet or pattern. | Should | Detection of a configured packet or pattern activates the Snapshot Node. |
| FR-MON-06-4 | The system shall capture the configured DUT state when a Snapshot Node is triggered. | Should | The configured physical, electrical, or internal state is captured. |
| FR-MON-07-1 | The system shall timestamp Snapshot Node outputs. | Should | Each snapshot output contains a timestamp. |
| FR-MON-07-2 | The system shall correlate Snapshot Node outputs with related communication events. | Should | Snapshots are linked to corresponding communication events. |
| FR-MON-07-3 | The system shall display Snapshot Node outputs on the unified timeline. | Should | Snapshot outputs appear at the correct position on the timeline. |
| FR-MON-08-1 | The system shall analyze Snapshot Node captured outputs. | Should | Supported snapshot data is processed to identify observable device behavior. |
| FR-MON-08-2 | The system shall generate descriptive information from analyzed snapshot outputs. | Should | The system generates a description of the observed device state or behavior. |
| FR-MON-08-3 | The system shall display generated descriptions on the unified timeline. | Should | Generated descriptions appear at the corresponding timeline position. |

### Assumptions & Dependencies
- Capture Nodes and the Brain can synchronize to a common local time reference.
- Required hardware for supported network, onboard-bus, wireless, and snapshot monitoring is available.
- Snapshot capabilities depend on the type of Snapshot Node being used.
- Maximum supported nodes, protocols, latency, and clock drift require validation on the final hardware configuration.
- The first version supports monitoring one DUT per session.

### Open Questions
- What is the target maximum latency for live event display?
- What is the acceptable maximum clock drift?
- Will NTP or PTP be used for time synchronization?
- What is the maximum number of Capture Nodes and protocols targeted for the first release?
- Which Snapshot Node types will be supported in the first release?
- Which snapshot analysis capabilities will be available in the first release?

---

## BR-ANA — Analytics

**Source BR:** BR-ANA-01 to BR-ANA-08
**Assignee:** HK
**Status:** Completed

### Summary
The analytics module lets analysts search recorded sessions for sensitive data, generate report-ready diagrams and exports, automatically flag clear-text secrets, reconstruct device memory from bus captures, and track usage/ROI metrics.

### Analytics Module Data Flow
```mermaid
flowchart TD

    SESSION["Recorded Session"]

    RECORD["Record & Replay<br/>Events + Timestamps"]
    SEARCH["Search & Flagging<br/>Secrets, Custom Search"]
    MEMORY["Memory Reconstruction<br/> Bus Address Map"]
    DIAGRAM["Behavior Diagram<br/>Visual Flow Diagram"]
    METRICS["Usage Metrics<br/>Sessions, Time-to-Finding"]

    EXPORT["Export & Reporting"]

    SESSION --> RECORD
    RECORD --> SEARCH
    RECORD --> MEMORY
    RECORD --> DIAGRAM
    RECORD --> METRICS

    SEARCH --> EXPORT
    MEMORY --> EXPORT
    DIAGRAM --> EXPORT
    METRICS --> EXPORT
```

### Functional Requirements

#### FR-ANA-01 — Session Recording & Replay

| FR ID | Requirement | Priority | Acceptance Criteria |
|---|---|---|---|
| FR-ANA-01-1 | The system shall record all captured communication events from an active session, beginning when the analyst starts the session and ending when they stop it. | Must | Starting a session begins capture; all events from active Capture Nodes are present in the saved session. |
| FR-ANA-01-2 | The system shall record a timestamp for each captured event. | Must | Each recorded event has an associated timestamp. |
| FR-ANA-01-3 | The system shall persist a session to storage when the analyst stops it. | Must | A stopped session remains available after the application restarts. |
| FR-ANA-01-4 | The system shall allow an analyst to play back a previously recorded session. | Must | A stored session can be reloaded and played back. |
| FR-ANA-01-5 | The system shall preserve the original chronological order and timestamps of recorded events during replay. | Must | Replayed events appear in their original order and display their original capture timestamps, not replay time. |



---

#### FR-ANA-02 — Search & Navigation

| FR ID | Requirement | Priority | Acceptance Criteria |
|---|---|---|---|
| FR-ANA-02-1 | The system shall allow an analyst to search a recorded session using a custom search phrase. | Must | An analyst-entered search phrase returns matching results from the recorded session. |
| FR-ANA-02-2 | The system shall display the exact location (packet/timestamp/protocol) of each search match. | Must | Each result links back to its precise position on the timeline. |
| FR-ANA-02-3 | The system shall allow an analyst to navigate directly from a search result to its corresponding timeline event. | Must | Selecting a result jumps the view to that event on the timeline. |

![Session search interface](./session_search_interface_v2-dark.svg)

---

#### FR-ANA-03 — Behavior Diagram Generation

| FR ID | Requirement | Priority | Acceptance Criteria |
|---|---|---|---|
| FR-ANA-03-1 | The system shall generate a Mermaid-based visual behavior diagram (e.g., a sequence diagram) from a recorded session's captured events, so analysts can understand a device's communication flow at a glance instead of manually reading the raw timeline, and so the diagram can be dropped directly into a client report. | Should | A Mermaid diagram is produced that reflects the session's captured events and renders correctly in a standard Mermaid viewer. |
| FR-ANA-03-2 | The system shall represent correlated communication events as linked steps within the diagram (e.g., in a sequence diagram, a login attempt's HTTP request and the I2C read it triggers on the DUT appear as connected messages between the same pair of lifelines), so analysts can easily see how events are connected without checking the raw session. | Should | For a known correlated event pair in the test session, both events appear in the diagram as visually linked/connected elements rather than disconnected entries. |


---

#### FR-ANA-04 — Export & Reporting

| FR ID | Requirement | Priority | Acceptance Criteria |
|---|---|---|---|
| FR-ANA-04-1 | The system shall allow an analyst to export recorded session data (e.g., as JSON or CSV). | Should | An export file containing session data is produced. |
| FR-ANA-04-2 | The system shall allow an analyst to export detected sensitive-data findings (e.g., as JSON or CSV). | Should | An export file containing findings (e.g., flagged secrets) is produced. |
| FR-ANA-04-3 | The system shall allow an analyst to export custom search results (e.g., as JSON or CSV). | Should | An export file containing the custom search results (each with its matched location, timestamp, and protocol) is produced. |
| FR-ANA-04-4 | The system shall export the generated diagram (e.g., a Mermaid sequence diagram of the session) in a report-ready format (e.g., PNG/SVG). | Should | The exported diagram opens correctly in a standard image viewer and matches what was rendered in-app. |
| FR-ANA-04-5 | The system shall allow an analyst to export the reconstructed memory map (e.g., binary/hex dump). | Should | The memory map can be exported and opened externally. |
| FR-ANA-04-6 | The system shall allow an analyst to choose which of the following to include in a single consolidated report: recorded session data, detected sensitive-data findings , custom search results , and the generated diagram  — exported in one or more report-ready formats (e.g., PDF, DOCX). | Should | The analyst can select any combination of session data, findings, custom search results, and generated diagrams, and the exported file, in a supported report-ready format, contains only the selected items. |

![Export Session Options](./export_report_options-dark.svg)


---

#### FR-ANA-05 — Sensitive-Data Flagging

| FR ID | Requirement | Priority | Acceptance Criteria |
|---|---|---|---|
| FR-ANA-05-1 | The system shall inspect captured packets for predefined clear-text sensitive data patterns and automatically flag matching packets, without analyst action. | Should | Plaintext credentials/tokens are automatically detected and flagged without analyst action. |
| FR-ANA-05-2 | The system shall provide a "Findings" control that toggles a contextual highlight mode on the timeline, emphasizing communication blocks containing flagged findings and fading unrelated blocks when active. | Should | Clicking the "Findings" control switches the timeline into highlight mode (flagged blocks emphasized, others faded, no separate view); clicking again returns to the normal view. |

![Findings toggle button](./findings_toggle_button_v2-dark.svg)

---

#### FR-ANA-06 — Usage Metrics

| FR ID | Requirement | Priority | Acceptance Criteria |
|---|---|---|---|
| FR-ANA-06-1 | The system shall display a running timer showing elapsed time for the current active session. | Should | While a session is active, an elapsed-time timer is visible and updates continuously. |
| FR-ANA-06-2 | The system shall track the number of completed analysis sessions. | Should | A running count of completed sessions is tracked. |
| FR-ANA-06-3 | The system shall calculate the average time required to identify findings. | Should | An average time-to-finding value is computed and available. |
| FR-ANA-06-4 | The system shall track the number of automatically detected sensitive-data findings. | Should | A running count of auto-detected findings is tracked. |
| FR-ANA-06-5 | The system shall present the tracked usage metrics (e.g., session count, average time, average session time, findings detected) in a dashboard, filterable by a selectable time period. | Should | A dashboard shows session count, average time-to-finding, average session time, and findings detected, and supports date-range filtering. |

![Session timer and usage metrics dashboard](./br_ana_06_combined_mockup-dark.svg)

---

#### FR-ANA-07 — Memory Reconstruction

| FR ID | Requirement | Priority | Acceptance Criteria |
|---|---|---|---|
| FR-ANA-07-1 | The system shall extract address/data pairs from captured (e.g., SPI/I2C) bus transactions. | Should | Address/data pairs are correctly parsed from bus capture data. |
| FR-ANA-07-2 | The system shall aggregate extracted address/data pairs into a unified reconstructed memory map. | Should | A reconstructed memory map is generated from a session with bus captures. |
| FR-ANA-07-3 | The system shall allow an analyst to view the reconstructed memory map. | Should | The memory map is viewable within the application. |



---

#### FR-ANA-08 — Reconstruction Completeness

| FR ID | Requirement | Priority | Acceptance Criteria |
|---|---|---|---|
| FR-ANA-08-1 | The system shall classify each observed memory address range as fully observed, partially observed, or never captured. | Should | Every address range in the reconstructed memory map is correctly classified into one of the three states. |
| FR-ANA-08-2 | The system shall visually distinguish fully observed, partially observed, and never-captured ranges in the memory map. | Should | The three states are visually distinct in the reconstructed map. |
| FR-ANA-08-3 | The system shall display a completeness summary statistic (e.g., % of address space fully observed) alongside the memory map. | Should | A summary percentage is shown with the map. |

![Reconstructed memory map view with export control](./memory_map_export_view-dark.svg)

---

#### FR-ANA-09 — Snapshot Behavior Descriptions

| FR ID | Requirement | Priority | Acceptance Criteria |
|---|---|---|---|
| FR-ANA-09-1 | The system shall analyze Snapshot Node captured outputs to identify observable device behavior. | Should | Supported snapshot data is processed to identify observable device behavior. |
| FR-ANA-09-2 | The system shall generate descriptive information from analyzed snapshot outputs. | Should | The system generates a description of the observed device state or behavior. |
| FR-ANA-09-3 | The system shall display generated descriptions on the unified timeline. | Should | Generated descriptions appear at the corresponding timeline position. |

---

### Assumptions & Dependencies
- Accurate search/flagging depends on reliable timestamps from time sync (FR-MON-04).
- Session storage/format depends on BR-DEP export-import design (FR-DEP-02-x).
- At-rest protection of exported findings depends on FR-SEC-01 (encryption at rest).
- The finding count metric (FR-ANA-06-3) depends on the flagging logic defined in FR-ANA-05-2; changes to detection patterns will affect the reported count.

### Open Questions
- What default credential/token patterns should auto-flagging (FR-ANA-05-1) detect out of the box?
- What diagram type(s)/tooling will be used to generate behavior diagrams (FR-ANA-03-1)?
- Which export formats will be supported for v1 (FR-ANA-04-3)?
- BR-ANA-01 does not define behavior if an analyst leaves a session running indefinitely (e.g., forgets to stop it) — should there be a maximum session duration, an idle timeout, or is indefinite recording acceptable?

---

## BR-ACT — Actions

**Source BR:**
**Assignee:** ME
**Status:** Not started

### Summary
_One or two sentences restating the business need in plain language._

### Functional Requirements

| FR ID | Requirement | Priority | Acceptance Criteria |
|---|---|---|---|
| FR-ACT-01 | | Must / Should / Could | |
| FR-ACT-02 | | | |

### Assumptions & Dependencies
-

### Open Questions
-

---

## BR-SEC — Security

**Source BR:** BR-SEC-01 to BR-SEC-04
**Assignee:** RA
**Status:** In progress

### Summary
The system shall protect captured data — including any live credentials or secrets it may contain — while at rest, guarantee that recorded sessions cannot be silently altered after capture, and support role-based access control. Where Capture Node–to-Brain communication is wireless, that link shall be encrypted and authenticated to prevent interception or spoofing.

### Functional Requirements

| FR ID | Requirement | Priority | Acceptance Criteria |
|---|---|---|---|
| FR-SEC-01-1 | The system shall encrypt captured session data at rest, including any embedded credentials or secrets. | Must | Data stored on disk is not readable in plaintext without the appropriate decryption key/credentials. |
| FR-SEC-01-2 | The system shall restrict filesystem/storage-level access to captured data to authorized system processes and users only. | Must | Attempting to access stored session data outside authorized processes/accounts is denied. |
| FR-SEC-01-3 | The system shall avoid persisting decrypted credentials/secrets in logs, temporary files, or caches. | Must | A review of logs, temp files, and caches after a capture session shows no plaintext credentials/secrets. |
| FR-SEC-02-1 | The system shall generate a cryptographic hash (or equivalent integrity marker) for each recorded session at the time of capture. | Must | Each recorded session has an associated hash/integrity value generated and stored at capture time. |
| FR-SEC-02-2 | The system shall detect and flag any post-capture modification to a recorded session. | Must | Deliberately modifying a captured session file causes a subsequent integrity check to fail and be flagged. |
| FR-SEC-02-3 | The system shall maintain an auditable chain-of-custody record for each recorded session (e.g., capture time, hash, subsequent access/export events). | Must | A chain-of-custody log exists per session and reflects all recorded access/export events in order. |
| FR-SEC-03-1 | The system shall support defining roles with distinct permissions (e.g., view, export, modify) for captured session data. | Could | At least two distinct roles can be configured with different permission sets. |
| FR-SEC-03-2 | The system shall enforce role-based restrictions such that a user can only view, export, or modify session data permitted by their assigned role. | Could | A user assigned a restricted role is blocked from performing an action outside their permissions. |
| FR-SEC-03-3 | The system shall log role-based access attempts, including denied actions. | Could | Both successful and denied access attempts are recorded with user, role, action, and timestamp. |
| FR-SEC-04-1 | The system shall encrypt communication between Capture Nodes and the Brain when the connection is wireless. | Could | Wireless Capture Node–Brain traffic is unreadable when intercepted without the decryption key. |
| FR-SEC-04-2 | The system shall authenticate Capture Nodes to the Brain (and vice versa) over wireless connections to prevent spoofing. | Could | An unauthenticated/spoofed device attempting to connect wirelessly as a Capture Node is rejected. |
| FR-SEC-04-3 | The system may rely on physical security in lieu of encryption/authentication for wired Capture Node–Brain connections. | Could | Wired-link deployments are documented as relying on physical security controls instead of link encryption. |

### Assumptions & Dependencies
- A key management approach (storage, rotation, recovery) for at-rest encryption is defined before implementation.
- Chain-of-custody logging depends on reliable time synchronization (per FR-MON-04 / FR-ENV-02-3).
- Role definitions and permission sets are agreed upon with stakeholders prior to implementation.
- Wireless Capture Node deployments are the primary driver for FR-SEC-04; wired-only deployments may deprioritize this requirement.

### Open Questions
- What encryption standard/algorithm is required or preferred for data at rest (e.g., AES-256)?
- Who manages encryption keys, and what is the key recovery process if lost?
- What specific roles are needed (e.g., Analyst, Admin, Auditor), and what permissions does each have?
- Is tamper detection sufficient (detect-only), or is tamper-proofing (prevent modification entirely) required?
- What authentication mechanism is expected for wireless Capture Node–Brain links (e.g., mutual TLS, pre-shared keys, certificates)?
- Does chain-of-custody need to meet a specific legal/evidentiary standard (e.g., for use in formal investigations)?

---

## BR-DEP — Deployment

**Source BR:** BR-DEP_01 to BR-DEP_02
**Assignee:** M
**Status:** Completed

### Summary
_The system shall run on Docker with fixed, predictable versions, and let users move their configuration and session data without losing it when containers are stopped, updated, or replaced._

### Functional Requirements

| FR ID | Requirement | Priority | Acceptance Criteria |
|---|---|---|---|
| FR-DEP-01-1 | System components (Brain + all dependencies) shall be packaged as Docker images defined via Dockerfile(s) and orchestrated via Docker Compose. | Must | Each system component has a corresponding Dockerfile and is defined as a service in `docker compose build` and that it completes successfully with exit code 0. |
| FR-DEP-01-2 | All image versions shall be pinned (no :`latest tags`; locked dependency versions via committed lockfiles) | Must | Every `FROM` in all Dockerfiles and every image: in `docker-compose.yml` — has an explicit version tag (e.g. postgres:16.3), not latest . |
| FR-DEP-01-3 | All Dockerfile install steps shall install strictly from the lockfile. | Must | No use of npm install, or unpinned pip install in place of their lockfile-strict equivalents.
| FR-DEP-02-1 | The system shall provide a function to export the current configuration and recorded session data to a file stored outside the container's writable layer  | Should | Confirm persistence after the container is stopped/removed. |
| FR-DEP-02-2 | The system shall provide a function to import previously exported configuration and recorded session data into a running or newly deployed instance. | Should | Import a previously exported data into both  a running instance and  a freshly deployed instance; confirm the operation completes successfully in both cases. |
| FR-DEP-02-3 | The system shall validate imported data for integrity and compatibility (e.g., correct format/schema). | Should |  Confirm validation runs before any data is applied. |
| FR-DEP-02-4| The system shall log export and import operations, including timestamp and outcome (success/failure). | Should | Perform successful and failed export/import operations; confirm each is recorded. |

### Assumptions & Dependencies
- The application has a defined, versioned schema for configuration and session data to support import validation.
- Exported files are stored on a host-accessible path that persists independently of the container.

### Open Questions
- Does import into a running instance hot-apply the config/session data, or does it require a restart?
- On import, is the behavior full overwrite or merge with existing session data?
- What happens if validation (FR-DEP-02-3) fails partway through an import?
- What export/import formats are required (JSON, SQL dump, encrypted archive)?


---

## BR-EXT — Extensibility

**Source BR:** BR-EXT-01 to BR-EXT-02
**Assignee:** M
**Status:** Completed

### Summary
_The system shall grow to support new communication protocols over time, and let external capture nodes plug in as an additional data source._

### Functional Requirements

| FR ID | Requirement | Priority | Acceptance Criteria |
|---|---|---|---|
| FR-EXT-01-1 | Implement communication protocol via a modular architecture, allowing new protocol modules without modifying the core system codebase. | Should | Isolate protocol handling in separate modules with a defined interface. |
| FR-EXT-01-2 | Define a standard protocol module interface (e.g., required methods/functions for connect, parse, send, disconnect) that any new protocol implementation must conform to. | Should | Review documentation/code for a defined protocol interface (abstract class, contract, or schema); confirm an existing protocol module implements it fully. |
| FR-EXT-02-1 | Provide a documented API (e.g., REST, or similar) enabling external hardware capture tools to submit captured data to the system. | Could | Call the documented API from an external tool/script and confirm data is accepted. |
| FR-EXT-02-2 | Treat data received from external capture tools as an additional data source, processed through the same correlation/monitoring pipeline as data from native Capture Nodes. | Could | Equivalent handling for data from both the external integration interface and a native Capture Node.|
| FR-EXT-02-3 | Validate data submitted by external capture tools for correct format/schema before ingesting it into the pipeline. | Could | Reject invalid payloads without disrupting the pipeline. |  
| FR-EXT-02-4 | Log all connections and data submissions from external capture tools, including source identity, timestamp, and outcome (success/failure). | Could | Submited data via the integration interface is logged with source, timestamp, and outcome. |


### Assumptions & Dependencies
- External capture nodes are assumed to be semi-trusted; hence explicit validation and logging requirements (FR-EXT-02-3, 02-4).

### Open Questions
- Should rejected/invalid payloads (FR-EXT-02-3) be queued for review, or simply dropped with an error response to the sender?

---

## BR-ENV — Non-Interference & Operating Environment

**Source BR:** BR-ENV-01 to BR-ENV-02 ,
**Assignee:** YS ,
**Status:** Completed.

### Summary
The system must behave as a passive observer during normal monitoring — never altering the DUT's behavior — and must be fully deployable and operable inside an isolated, air-gapped test-bench network, with internet access confined to initial setup only.

### Environment Operation Flow
```mermaid
flowchart LR

    Analyst --> Brain

    Brain --> CaptureNodes["Capture Nodes"]

    CaptureNodes --> DUT["Device Under Test"]

    DUT -.Passive Monitoring 
    (listen-only).-> CaptureNodes

    Brain --> Storage

    Brain --> Timeline

    subgraph Deployment Environment
        Brain
        CaptureNodes
        Storage
        Timeline
    end

    Internet[(Internet)]

    Internet -. Required only during 
    installation .-> Installer["Initial Installation"]

    Internet -. No dependency 
    during runtime .-x Brain
```
### Functional Requirements

| FR ID | Requirement | Priority | Acceptance Criteria |
|---|---|---|---|
| FR-ENV-01-1 | The system shall operate in a listen-only (passive) mode on all monitored interfaces — network, onboard-bus, and wireless — during standard monitoring sessions. | Must | No outbound transmission, injection, or signal alteration occurs on any monitored interface while in passive monitoring mode. |
| FR-ENV-01-2 | The system shall not transmit, inject, or otherwise alter any signal on a monitored interface while operating in passive monitoring mode. | Must | Electrical/timing measurements on tapped interfaces show no measurable deviation from baseline DUT behavior during passive monitoring. |
| FR-ENV-01-3 | The system shall visually indicate to the analyst which mode is currently active — passive monitoring or active reconnaissance (per FR-ACT). | Should | The active mode is clearly and unambiguously displayed in the interface at all times. |
| FR-ENV-01-4 | The system shall exclude active reconnaissance actions performed under FR-ACT-01/03 from the non-interference constraint, since those are explicitly intended to affect the DUT. | Must | Active reconnaissance actions execute normally and are not blocked or flagged by non-interference checks. |
| FR-ENV-02-1 | The system's runtime — including live monitoring, correlation, session recording, and active reconnaissance — shall function fully with no outbound or inbound internet connection. | Must | All core runtime functions operate correctly with the test-bench network disconnected from the internet. |
| FR-ENV-02-2 | The system shall not depend on any internet-hosted service (e.g., license checks, telemetry, update checks) during normal operation. | Must | No outbound requests to external/internet-hosted endpoints are observed during normal operation. |
| FR-ENV-02-3 | The system's time synchronization mechanism (per FR-MON-04) shall use only a local network time reference, not an internet-hosted NTP/PTP source. | Must | The configured time reference server resides within the isolated test-bench network. |
| FR-ENV-02-4 | The system may require internet access solely during initial installation/setup (e.g., pulling container images per BR-DEP-01), and this exception shall be explicitly documented. | Must | Documentation clearly states which setup steps require internet access and confirms no such dependency exists post-setup. |

### Assumptions & Dependencies
- The monitoring hardware is correctly connected to the DUT.
- The deployment environment provides local networking between the Brain and Capture Nodes.
- Docker images and required dependencies are downloaded before deployment into an air-gapped environment.

### Open Questions
- Will software updates also support fully offline installation?
- What operating systems are officially supported for deployment?
- What quantitative threshold (e.g., timing/electrical tolerance) defines "no interference" for passive taps on each protocol?
- Is there a need to detect and alert if an outbound internet call is attempted during normal operation, or is documentation-only compliance sufficient for the first release?

---

## BR-ACC — Acceptance, Adoption & Support

**Source BR:**  BR-ACC-01 to BR-ACC-02 ,
**Assignee:** YS ,
**Status:** Completed.

### Summary
Before the system is considered delivered, it must pass a formal User Acceptance Testing process against real DUT scenarios, and analysts must receive training material to support onboarding and full adoption.

### Acceptance & Adoption Pipeline
```mermaid
flowchart LR
    UAT["User Acceptance Testing"]  --> Decision{Pass ?}
    DEV["Development<br/>Complete"] --> UAT["User Acceptance Testing"]
    Decision -->|No| Fixes["Defect Resolution"]
    Fixes -->  UAT["User Acceptance Testing"]
    Decision -->|Yes| Acceptance["Customer Acceptance"]
    Acceptance --> Training["Analyst Training"]
    Training --> Deployment["Operational Use"]
```
### Functional Requirements

| FR ID | Requirement | Priority | Acceptance Criteria |
|---|---|---|---|
| FR-ACC-01-1 | The system shall be validated through a documented UAT test plan covering all Must-priority business requirements, executed against real DUT scenarios. | Must | A UAT test plan exists mapping test cases to Must-priority BRs, and all cases are executed against a real DUT. |
| FR-ACC-01-2 | The system shall record UAT results (pass/fail per scenario, with evidence) for review. | Must | UAT results are documented per scenario with pass/fail status and supporting evidence (logs, screenshots, or captures). |
| FR-ACC-01-3 | The system shall require formal stakeholder sign-off confirming UAT completion before being considered delivered. | Must | A signed/recorded acceptance confirmation exists from the designated stakeholder(s) referencing the completed UAT results. |
| FR-ACC-02-1 | The project shall produce training material (e.g., user guide, quick-start guide, walkthrough) covering core system operation. | Should | Training material exists and covers, at minimum, session setup, live monitoring, snapshot triggering, and session export/reporting. |
| FR-ACC-02-2 | The project shall deliver an onboarding session or equivalent training activity to analysts prior to full system adoption. | Should | At least one training session is conducted and attendance/completion is recorded prior to declaring full adoption. |
| FR-ACC-02-3 | The training material shall be reviewed for completeness and accuracy against the delivered system's actual functionality. | Could | A review/feedback checklist confirms training material matches current system behavior, with discrepancies logged and resolved. |

### Assumptions & Dependencies
- Real DUT hardware is available for UAT execution, consistent with the assumption in Section 7 of the BRD.
- UAT scenarios are derived from the Must-priority requirements across all BR categories (MON, ANA, ACT, SEC, ENV, DEP, EXT).
- Designated stakeholder(s) authorized to grant sign-off are identified before UAT begins.
- Training is delivered before production deployment.

### Open Questions
- Who is responsible for approving UAT?
- Will training be instructor-led, self-paced, or both?
- What is the minimum number/coverage of real DUT scenarios required for UAT to be considered representative?