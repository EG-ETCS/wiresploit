# Business Requirements Document (BRD)

| Field             | Value                                                        |
|-------------------|--------------------------------------------------------------|
| Project Name      | IoT Reconnaissance & Communication-Block Monitoring System   |
| Working Title     | Wiresploit                                                   |
| Document Status   | `final V0.1`                              |
| Date              | 14-7-2026                                                    |
| Prepared by       | Mohamed Salah-elden                                          |

---

## 1. Executive Summary

This document defines the business case, objectives, and high-level requirements for a system designed to empower black-box security analysis teams to monitor, record, and analyze both internal and external (network-facing) communications of IoT devices (e.g., an IP camera) in real time. The system will capture activity across various protocols (including HTTP/network traffic, I2C, SPI, UART, and logic-level signals), automatically correlate related events into a unified, time-ordered timeline, and provide robust analysis features to help identify potential security weaknesses. In addition, the system will support performing active reconnaissance techniques against the device under test (DUT) and analyzing its resulting responses, with the goal of maximizing the benefits and insights obtained from reconnaissance activities.

## 2. Business Problem / Opportunity

When performing black-box security analysis of an IoT/OT device, a single logical operation (e.g., a login attempt) triggers a chain of communications and actions across multiple physical layers — network traffic and internal hardware buses. Today, analysts use separate, disconnected tools for each layer (e.g., a network sniffer, a logic analyzer) and manually cross-reference timestamps and perform corelation analysis to reconstruct what actually happened. This approach is:

- **Slow** — manual correlation across tools takes significant analyst time per test.
- **Error-prone** — it's easy to miss or mis-order causally related events across tools.
- **Difficult to defend/report on** — findings based on manual correlation are harder to justify with precise, traceable evidence.

**Opportunity:** A unified system that automatically captures, timestamps, and correlates all relevant communications — and helps analysts search for hidden secrets and visualize system behavior and also cabable of performing active reconnaissance techniques — would materially speed up security assessments, improve finding quality, and produce better-documented evidence for client-facing reports.

## 3. Business Objectives

| ID | Objective |
|---|---|
| BO-01 | Reduce analyst time spent manually correlating captures from separate, disconnected tools |
| BO-02 | Reduce cost per engagement by shortening analysis time, enabling more assessments to be completed with the same team size |
| BO-03 | Improve the accuracy and defensibility of security findings through traceable evidence |
| BO-04 | Enable faster identification of sensitive-data exposure (e.g., plaintext credentials) across all monitored protocols |
| BO-05 | Produce report-ready artifacts (timelines, diagrams, exports) to speed up the write-up phase of assessments |
| BO-06 | Build a reusable, extensible internal tool/platform rather than a one-off script, so it can grow with future assessment needs |
| BO-07 | Broadening the behavioral visibility scope of the DUT by performing active reconnaissance techniques and analyzing the resulting responses.|
| BO-08 | Build a growing, reusable knowledge base of recorded sessions across engagements, usable for future comparisons, training, and reference |
| BO-09 | Reduce dependency on individual analyst expertise by encoding correlation and analysis logic into the tool itself |
| BO-10 | Strengthen client trust and deliverable quality through a documented, traceable audit trail of how findings were obtained |
| BO-11 | Reduce tool acquisition and maintenance costs by consolidating separate capture tools (network analyzers, logic analyzers, etc.) into a single platform, at the organizational level — not just individual analyst time |
| BO-12 | Enable measurement of the system's actual ROI over time by having the system itself generate quantitative usage and performance metrics (e.g., number of sessions conducted, average time-to-finding, number of secrets/credentials automatically detected), rather than relying solely on analysts' subjective impressions |


## 4. Definitions

### 4.1 Node

A Node is a hardware device attached directly to a Device Under Test (DUT). There is one Node type. A Node does not come in separate capture, snapshot, or injection variants. Each Node has exactly two functions, and each function can be enabled or disabled independently:

- **Capture.** When capture is enabled, the Node passively observes the DUT. The analyst selects the protocol (for example I2C, SPI, UART, GPIO, Bluetooth, LoRa, RFID, HTTP, TCP, or MQTT) and configures that protocol's settings. The Node timestamps captured events and reports them to the Core. Capture also records a point-in-time DUT state when a trigger condition is met or the analyst triggers it manually. That state can be physical/visual (camera), electrical/signal (GPIO or voltage), or internal/logical (registers, memory, or debug information), and it is placed on the timeline as a Snapshot Block.
- **Actions.** When actions are enabled, the Node runs a configured action when a configured trigger fires. A trigger is one of: manual, on packet match, scheduled, or on peer notification. An action is one of: inject a DUT payload, run a Node script, or send a notification signal. A notification signal is addressed to the Core, to all (the Core and the other Nodes), or to one specific Node. A peer-notification trigger fires when this Node receives a notification signal from another Node.

A Node is used in one of these configurations:

- **Capture and actions.** Capture is enabled and actions are enabled. The Node observes the DUT and can also perform actions.
- **Actions only.** Capture is disabled and actions are enabled. The Node performs actions and does not report capture events.
- **Capture only.** Capture is enabled and actions are disabled. The Node observes the DUT and cannot perform actions.

A Node with both functions disabled is not used in a session. Wired versus wireless connectivity between the Node and the Core is chosen by the hardware lead based on feasibility, cost, and DUT constraints.

Each Node has three groups of settings:

- **General.** Node ID, Node name, Node description, and a unique color. When a Node connects, the Core reads its Node ID, assigns the unique color, and adds the Node to the Node list. The Core shows that color beside the Node. The Node hardware lights an indication LED in that same color, so the physical Node can be matched to its entry on the Core.
- **Capture.** Used when the capture function is enabled. The analyst selects the protocol and configures that protocol's settings.
- **Actions.** Used when the actions function is enabled. The analyst sets a trigger and an action, as defined above.

Each Node follows one state machine with four states:

| State | Meaning |
|---|---|
| **Online** | The Node has connected to the Core. The Core has read its Node ID, assigned a unique color, and added it to the Node list. It has no configuration yet. |
| **Ready** | The Node has been configured. Its role is loaded and it is armed, but it is not yet performing its task. |
| **Running** | The Node is performing its task: capturing, decoding, matching packets, performing actions, and sending data packets. |
| **Unreachable** | The Core has not received a heartbeat in time. The Node may be hung, turned off, or out of battery. |

The Node reports Online, Ready, and Running. The Core infers Unreachable from a heartbeat timeout. From Unreachable, a Node that was not configured returns to Online. A Node that was already configured returns to Ready.

### 4.2 Communication Block (CB)

A Communication Block is a unit on the unified timeline representing a set of communication events — captured from one or more Nodes with capture enabled that the Core should correlate together based on timing and logical relationship, so they represent a single logical operation on the DUT (e.g., a login attempt triggering both an HTTP request and internal I2C/SPI activity).

### 4.3 Snapshot Block (SB)

A Snapshot Block is a unit on the unified timeline representing a captured state or data snapshot at a specific point in time (e.g. internal memory read, voltage level on specific wire, debug info or registers state, camera captues the physical state of the device or any another discrete piece of captured data), Snapshot Block is timestamped and placed on the timeline to provide context around or between Communication Blocks.



## 5. Business Scope

### 5.1 In Scope 

- Passive, real-time capture of DUT communications across: network traffic (HTTP/Ethernet/WiFi, captured directly by the central device), and onboard buses (I2C, SPI, UART, logic-level/GPIO) and wireless communication (Bluetooth, LoRa, RFID, etc.) via Nodes whose capture function is enabled.
- Timestamped, causally-correlated presentation of all captured communications as a unified timeline ("Communication Blocks", "Snapshot Blocks").
- Session recording, replay, filtering, search, annotation, and export for reporting.
- Analysis capabilities: searching captured sessions for hidden secrets/credentials, and generating diagrams/visualizations of system behavior from a session.
- Active reconnaissance on the DUT (e.g., traffic replay/modification, bus-level fault injection, fuzzing, etc.), with the DUT's resulting responses captured and analyzed through the same platform.
- Analysis engines grouped by session scope and by domain. A domain is a protocol. Single-session engines are single-domain, multiple-domain, or active. Multiple-session engines are single-domain or multiple-domain.

### 5.2 Out of Scope

- Automated vulnerability scoring or exploit generation.
- Firmware disassembly / static analysis (a complementary but separate capability).
- Fleet-wide or simultaneous multi-device monitoring (one device under test per session).

---------

## 6. Business Requirements (BR)

The requirements listed below are prioritized using the MoSCoW framework. The MoSCoW method classifies requirements into four categories:

- **Must**: Essential requirements that the solution must deliver for project success.
- **Should**: Important requirements that are not vital for immediate delivery, but add significant value if included.
- **Could**: Desirable but non-essential requirements to be included if time and resources permit.
- **Won't** (or **Would like to**): Requirements that are acknowledged but agreed to be excluded from the current scope or release.


### 6.1 Core Monitoring & Correlation

| ID | Requirement | Priority |
|---|---|---|
| BR-MON-01 | The system shall allow an analyst to observe a live, unified view of a device's communications across network, onboard-bus protocols and wireless communication simultaneously, without needing to operate multiple separate tools | Must |
| BR-MON-02 | The system shall present related communications (e.g., a request and the internal operations it triggers) in correct time order, so an analyst can understand cause and effect | Must |
| BR-MON-03 | The system shall display each event on the live timeline within a defined end-to-end latency value [end-to-end latency is measured from the moment the event happends at its source (e.g. UART Tx Trigger) to the moment it appears on the operator's live timeline (a new communication block appears on the screen)]. This latency value is to be documented and validated against the final chosen hardware/connectivity (wired vs. wireless Nodes) | Should |
| BR-MON-04 | All Nodes with capture enabled, and the Core, shall synchronize to a common time reference (e.g., a local/on-premises NTP or PTP server hosted within the isolated test-bench network) with documented maximum clock drift, to ensure correlation accuracy claims are valid. This time reference shall not depend on external internet connectivity, consistent with BR-ENV-02. | Must |
| BR-MON-05 | The system shall document the maximum number of simultaneous Nodes/protocols it supports without degradation in correlation accuracy or timeline responsiveness | Should |
| BR-MON-06 | The system shall support triggering a state capture on a Node whose capture function is enabled, covering the DUT's physical/visual, electrical/signal, or internal/logical state, based on configurable conditions, including: (a) a defined trigger event/signal, (b) a defined delay period following a trigger, or (c) detection of a specific captured packet/pattern. The specific state captured (e.g., camera recording, GPIO/voltage reading, memory/register dump) shall depend on the capture interfaces configured on that Node. | Should |
| BR-MON-07 | The system shall correlate a Node's captured state output (e.g., video segment, electrical/signal reading, or internal memory/register dump) with its corresponding communication events on the unified timeline, aligned by timestamp. | Should |
| BR-MON-08 | The system shall analyze a Node's captured state output and generate descriptive text summarizing the observed device state or behavior (e.g., "camera motor began moving right," "GPIO pin 3 transitioned HIGH," "instruction pointer register is now pointing to 0x0000ABCD"), and illustrate this on the timeline. | Should |
| BR-MON-09 | Each Node shall expose two functions, capture and actions, and each function shall be independently enabled or disabled. A Node may capture only, perform actions only, or do both. A Node shall not report capture events while capture is disabled, and shall not perform actions while actions are disabled. | Must |
| BR-MON-10 | Each Node shall have general settings: a Node ID, a Node name, a Node description, and a unique color. When the Node connects, the Core shall read the Node ID, assign the unique color, and add the Node to the Node list. The Core shall show that color in the Node list. The Node hardware shall light an indication LED in the same color, so the physical Node matches its entry on the Core. | Must |
| BR-MON-11 | For each Node with capture enabled, the analyst shall select the protocol to capture and configure that protocol's settings. | Must |
| BR-MON-12 | Each Node shall follow a four-state machine: Online, Ready, Running, and Unreachable. Connecting, and the Core reading the Node ID, assigning a unique color, and listing the Node, shall place it in Online. Configuration shall move it from Online to Ready. Starting its task (capture, decode, packet match, actions, and sending data packets) shall move it from Ready to Running. The Core shall mark the Node Unreachable on heartbeat timeout from Online, Ready, or Running, including when the Node hangs, is turned off, or loses battery. A reconnecting Node that was not configured shall return to Online. A reconnecting Node that was already configured shall return to Ready. Online, Ready, Running, and those returns shall be reported by the Node. Unreachable shall be inferred by the Core. | Must |

### 6.2 Session Recording, Analysis & Reporting

| ID | Requirement | Priority |
|---|---|---|
| BR-ANA-01 | The system shall allow analysts to record a full test session and play it back later for analysis or client reporting | Must |
| BR-ANA-02 | The system shall allow analysts to search a recorded session for potentially sensitive information (e.g., credentials, tokens) and see exactly where it appeared | Must |
| BR-ANA-03 | The system shall allow analysts to generate a visual diagram of a device's observed behavior for inclusion in reports | Should |
| BR-ANA-04 | The system shall allow exporting session data and findings in formats suitable for client-facing security reports | Should |
| BR-ANA-05 | The system shall automatically flag any captured, timestamped packet found to contain clear-text sensitive data (e.g., plaintext credentials or tokens), making it visually identifiable within the session timeline for post-capture review | Should |
| BR-ANA-06 | The system shall generate quantitative usage and performance metrics (e.g., number of sessions conducted, average time-to-finding, number of secrets/credentials automatically detected) to support measurement of the tool's return on investment over time | Should |
| BR-ANA-07 | The system shall allow analysts to reconstruct the contents of the DUT's memory (e.g., SPI/I2C flash or addressable memory) from a recorded session, by aggregating the address/data pairs observed across captured bus transactions into a unified memory map/dump | Should |
| BR-ANA-08 | The system shall indicate, within the reconstructed memory map, which address ranges were fully observed, partially observed, or never captured during the session, to give analysts an honest picture of reconstruction completeness | Should |
| BR-ANA-09 | The system shall provide analysis engines in the categories below. A domain is a protocol. Session recording, export, and usage metrics are not analysis engines. | Must |


!!! note
    The capability of raw memory content reconstruction does not constitute firmware disassembly or static binary analysis, which remain out of scope per Section 5.2.

#### Analysis engine categories

| Scope | Group | Uses | Engines |
|---|---|---|---|
| Single session | Single domain | One protocol in one session | Memory reconstruction (BR-ANA-07, BR-ANA-08). IP/domain name detection. Sensitive-data search and flagging (BR-ANA-02, BR-ANA-05). |
| Single session | Multiple domain | More than one protocol in one session | Behavioral analysis, including the behavior diagram (BR-ANA-03). |
| Single session | Active | One session, and the engine acts on the DUT | Packet replay, packet injection, and fuzzing, performed through a Node whose actions function is enabled (BR-ACT). |
| Multiple session | Single domain | One protocol across more than one session | Memory comparison. |
| Multiple session | Multiple domain | More than one protocol across more than one session | Baseline analysis and pattern detection. |

### 6.3 Active Triggering & DUT Control

| ID | Requirement | Priority |
|---|---|---|
| BR-ACT-01 | The system shall allow an analyst to perform a controlled active reconnaissance against a device under test and observe/analyze its response, with explicit confirmation required before any such action | Must |
| BR-ACT-03 | On a Node whose actions function is enabled, actions settings shall pair one trigger with one action. The trigger shall be manual, on packet match, scheduled, or on peer notification. The action shall be inject a DUT payload, run a Node script, or send a notification signal. A notification signal shall be addressable to the Core, to all (the Core and the other Nodes), or to one specific Node. The analyst shall confirm the configured trigger and action before it is armed. A manual trigger runs when the analyst starts it. The other triggers run the action when their condition is met. | Must |

!!! note
    Active reconnaissance actions under this section are explicitly excluded from the non-interference constraint defined in BR-ENV-01, and are only performed with explicit operator confirmation.

### 6.4 Data Integrity, Security & Access Control

| ID | Requirement | Priority |
|---|---|---|
| BR-SEC-01 | The system shall protect captured data (which may include live credentials/secrets) from unauthorized access while in rest | Must |
| BR-SEC-02 | The system shall preserve the integrity of recorded sessions to maintain evidentiary chain of custody | Must |
| BR-SEC-03 | The system shall support role-based access control to restrict who can view, export, or modify captured session data | Could |

### 6.5 Non-Interference & Operating Environment

| ID | Requirement | Priority |
|---|---|---|
| BR-ENV-01 | The system shall not alter or interfere with the device under test's normal operation during passive monitoring. This constraint applies exclusively to passive monitoring activities and does not apply to active reconnaissance actions performed under BR-ACT-01/BR-ACT-03, which are explicitly intended to interact with and affect the DUT's behavior upon operator confirmation. | Must |
| BR-ENV-02 | The system's operational runtime — including live monitoring, correlation, session recording, and active reconnaissance activities — shall be fully operable in an isolated, air-gapped network environment with no dependency on external internet connectivity. Initial installation/setup (e.g., pulling Docker images and dependencies per BR-DEP-01) may require internet access and is not subject to this constraint, provided the system remains fully functional offline once deployed. | Must |

### 6.6 Extensibility

| ID | Requirement | Priority |
|---|---|---|
| BR-EXT-01 | The system shall be extensible to support additional communication protocols as new device types are assessed | Should |
| BR-EXT-02 | The system shall provide a documented integration interface (e.g., API or scripting interface) allowing external hardware capture tools to connect as an additional data source alongside native Nodes with capture enabled | Could |

### 6.7 Deployment & Delivery

| ID | Requirement | Priority |
|---|---|---|
| BR-DEP-01 | The system shall be packaged and delivered as a containerized, reproducible deployment (e.g., Docker/Docker Compose), allowing the core and its dependencies to be installed and run consistently across different environments without manual environment setup. Internet access may be used during initial setup/installation, but is not required afterward for normal operation, in line with BR-ENV-02. | Must |
| BR-DEP-02 | The system shall support exporting/importing its configuration and recorded session data independently of the container lifecycle, to allow backup and migration between deployments | Should |

### 6.8 Acceptance, Adoption & Support

| ID | Requirement | Priority |
|---|---|---|
| BR-ACC-01 | The system shall pass a defined User Acceptance Testing (UAT) process, validated against real DUT scenarios, before being accepted as delivered | Must |
| BR-ACC-02 | The project shall include a training plan/material to onboard analysts on system operation before full adoption | Should |


--------



## 7. Assumptions & Constraints

- Physical access to the device under test is available for attaching capture hardware.
- An isolated test-bench network will be used for the system, separate from general business networks.
- The system is intended for use by one analyst/team on one device at a time in its first version, not for large-scale or concurrent testing.

## 8. Cost / Benefit Considerations

| Cost Factors | Benefit Factors |
|---|---|
| Development cost (in-house or vendor) | Reduced analyst hours per assessment (recurring time savings) |
| Capture hardware (per-protocol devices) | Improved finding quality and defensibility in reports |
| Ongoing maintenance/support | Reusable platform across multiple future engagements/devices |
| Training time for analysts to adopt the tool | Faster report turnaround, potentially enabling more assessments per period |

*(Detailed budget figures to be provided by the Business Owner/Sponsor; a rough estimate should be captured in the Project Charter and refined in the Statement of Work.)*

## 9. Success Criteria

- Analysts report reduced time-to-finding compared to the current manual, multi-tool workflow.
- Client-facing reports produced using the system are accepted without requests for additional manual re-verification of timing/correlation claims.
- The tool is adopted as standard practice for black-box IoT assessments within the team, rather than used ad hoc.

## 10. Risks (Business-Level)

- **Adoption risk:** analysts may default back to familiar individual tools if the new system isn't clearly faster/better in practice — mitigate with early pilot use on a real engagement.
- **Precision/credibility risk:** if timestamp correlation accuracy is oversold or misunderstood, findings based on it could be challenged — the system must always report honest confidence levels (see technical documentation).
- **Vendor/IP risk (if outsourced):** ensure ownership of code and documentation is contractually clear (see BRD-adjacent Statement of Work and IP Assignment Agreement).

## 11. Related Documents

- Project Charter


## 12. glosary

| Abbreviation | Full Form                                 | Description                                                                                               |
|--------------|-------------------------------------------|-----------------------------------------------------------------------------------------------------------|
| API          | Application Programming Interface         | Documented interface allowing external tools to integrate with the system (BR-EXT-02)                      |
| BO           | Business Objective                        | High-level business goal the project aims to achieve (e.g., BO-01, BO-02)                                 |
| BR           | Business Requirement                      | A specific, traceable requirement derived from the business objectives (e.g., BR-MON-01)                  |
| BRD          | Business Requirements Document            | The document defining the business case, objectives, and high-level requirements                           |
| CB           | Communication Block                       | A unit of correlated communication data shown on the unified timeline (could be TCP packet, UART Tx data , SPI data read)                                      |
| CERT         | Computer Emergency Readiness Team         | The organization type acting as Business Owner (EG-CERT)                                                   |
| DUT          | Device Under Test                         | The IoT/OT device being analyzed (e.g., an IP camera)                                                     |
| FwRS         | Firmware Requirements Specification       | Document detailing firmware-specific requirements                                                          |
| GPIO         | General Purpose Input/Output              | Logic-level digital signal pins used for onboard bus/signal capture                                        |
| HwRS         | Hardware Requirements Specification       | Document detailing hardware-specific requirements                                                          |
| HTTP         | HyperText Transfer Protocol               | Network protocol captured as part of the DUT's network traffic                                             |
| I2C          | Inter-Integrated Circuit                  | An onboard communication bus protocol monitored by Nodes with capture enabled                              |
| IoT          | Internet of Things                        | Category of connected embedded devices the system is designed to analyze                                   |
| IP           | Intellectual Property                     | Ownership rights over code/documentation (mentioned under vendor risk)                                     |
| JS           | JavaScript                               | Web-based scripting language used for frontend development                                                 |
| LoRa         | Long Range (wireless protocol)            | A long-range wireless communication protocol supported for capture                                         |
| MoSCoW       | Must, Should, Could, Won't                | Framework used to prioritize business requirements                                                         |
| NTP          | Network Time Protocol                     | Protocol used to synchronize time across the Core and Nodes                                                |
| OT           | Operational Technology                    | Category of industrial/embedded devices referenced alongside IoT                                           |
| PCB          | Printed Circuit Board                     | Physical hardware board referenced in hardware branch naming (e.g., pcb-rev2)                             |
| PR           | Pull Request                              | A GitHub request to merge code changes after review                                                        |
| PTP          | Precision Time Protocol                   | High-precision time synchronization protocol used for clock alignment (BR-MON-04)                         |
| QA           | Quality Assurance                         | Role responsible for testing strategy and validation                                                       |
| RFID         | Radio Frequency Identification            | A wireless protocol supported for capture alongside Bluetooth/LoRa                                         |
| ROI          | Return on Investment                      | Measure of the system's value/benefit over time (BO-12)                                                    |
| SB           | Snapshot Block                            | A captured state/data snapshot shown on the unified timeline                                               |
| SPI          | Serial Peripheral Interface               | An onboard communication bus protocol monitored by Nodes with capture enabled                              |
| SwRS         | Software Requirements Specification       | Document detailing software-specific requirements                                                          |
| SysRS        | System Requirements Specification         | Document detailing top-level system requirements                                                           |
| TCP          | Transmission Control Protocol             | Network-layer protocol captured as part of DUT network traffic                                             |
| TS           | TypeScript                                | Typed superset of JavaScript used in web-based development                                                 |
| UART         | Universal Asynchronous Receiver-Transmitter| An onboard communication bus protocol monitored by Nodes with capture enabled                              |
| UAT          | User Acceptance Testing                   | Formal testing process validating the system before final acceptance                                       |
| Wi-Fi        | Wireless Fidelity                         | Wireless networking technology used for network traffic and/or Node connectivity                           |


