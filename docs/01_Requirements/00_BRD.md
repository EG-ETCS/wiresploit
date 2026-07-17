# Business Requirements Document (BRD)

| Field             | Value                                                        |
|-------------------|--------------------------------------------------------------|
| Project Name      | IoT Reconnaissance & Communication-Block Monitoring System   |
| Working Title     | Wiresploit                                                   |
| Document Status   | `final V0.0`                              |
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

## 4. Business Scope

### 4.1 In Scope 

- Passive, real-time capture of DUT communications across: network traffic (HTTP/Ethernet/WiFi, captured directly by the central device), and onboard buses (I2C, SPI, UART, logic-level/GPIO) via dedicated Capture Nodes (wired and/or wireless, to be determined by the vendor/hardware lead based on technical feasibility), in addition to wireless communication (Bluetooth, LoRa, RFID, etc.).
- Timestamped, causally-correlated presentation of all captured communications as a unified timeline ("Communication Blocks", "Snapshot Blocks").
- Session recording, replay, filtering, search, annotation, and export for reporting.
- Analysis capabilities: searching captured sessions for hidden secrets/credentials, and generating diagrams/visualizations of system behavior from a session.
- Active reconnaissance on the DUT (e.g., traffic replay/modification, bus-level fault injection, fuzzing, etc.), with the DUT's resulting responses captured and analyzed through the same platform.
- Advanced analysis modules building (e.g., anomaly detection across sessions, automated protocol classification).

### 4.2 Out of Scope

- Automated vulnerability scoring or exploit generation.
- Firmware disassembly / static analysis (a complementary but separate capability).
- Fleet-wide or simultaneous multi-device monitoring (one device under test per session).

---------

## 5. Business Requirements (BR)

The requirements listed below are prioritized using the MoSCoW framework. The MoSCoW method classifies requirements into four categories:

- **Must**: Essential requirements that the solution must deliver for project success.
- **Should**: Important requirements that are not vital for immediate delivery, but add significant value if included.
- **Could**: Desirable but non-essential requirements to be included if time and resources permit.
- **Won't** (or **Would like to**): Requirements that are acknowledged but agreed to be excluded from the current scope or release.


### 5.1 Core Monitoring & Correlation

| ID | Requirement | Priority |
|---|---|---|
| BR-MON-01 | The system shall allow an analyst to observe a live, unified view of a device's communications across network, onboard-bus protocols and wireless communication simultaneously, without needing to operate multiple separate tools | Must |
| BR-MON-02 | The system shall present related communications (e.g., a request and the internal operations it triggers) in correct time order, so an analyst can understand cause and effect | Must |
| BR-MON-03 | The system shall display correlated events on the live timeline within a defined maximum latency threshold (to be validated against chosen hardware) | Should |
| BR-MON-04 | All Capture Nodes and the Brain shall synchronize to a common time reference (e.g., a local/on-premises NTP or PTP server hosted within the isolated test-bench network) with documented maximum clock drift, to ensure correlation accuracy claims are valid. This time reference shall not depend on external internet connectivity, consistent with BR-ENV-02. | Must |
| BR-MON-05 | The system shall document the maximum number of simultaneous Capture Nodes/protocols it supports without degradation in correlation accuracy or timeline responsiveness | Should |
| BR-MON-06 | The system shall support triggering a camera to begin recording video of the DUT based on configurable conditions, including: (a) a defined trigger event/signal, (b) a defined delay period following a trigger, or (c) detection of a specific captured packet/pattern | Should |
| BR-MON-07 | The system shall correlate the recorded video segment with its corresponding communication events on the unified timeline, aligned by timestamp | Should |
| BR-MON-08 | The system shall analyze recorded video segments and generate descriptive text summarizing observed physical device behavior (e.g., "camera motor began moving right"), associated with the triggering event on the timeline | Should |

### 5.2 Session Recording, Analysis & Reporting

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


!!! note
    The capability or raw memory content reconstruction does not constitute firmware disassembly or static binary analysis, which remain out of scope per Section 4.2.

### 5.3 Active Triggering & DUT Control

| ID | Requirement | Priority |
|---|---|---|
| BR-ACT-01 | The system shall allow an analyst to perform a controlled active reconnaissance against a device under test and observe/analyze its response, with explicit confirmation required before any such action | Must |
| BR-ACT-02 | The system shall provide configurable triggering capability, allowing the operator to define conditions (e.g., detection of a specific event, pattern, or signal) that automatically initiate a defined output action | Should |
| BR-ACT-03 | Trigger output actions shall include, at minimum: (a) a hardware-level reset/power-cycle signal to the DUT, (b) generation of a specific wired/logic-level output signal (e.g., GPIO pulse), (c) generation or replay of a specific wireless signal, (D) generation or replay a specific on-board protocol through the capture nodes, with the specific trigger action explicitly configured/confirmed by the operator before use | Must |

!!! note
    Active reconnaissance actions under this section are explicitly excluded from the non-interference constraint defined in BR-ENV-01, and are only performed with explicit operator confirmation.

### 5.4 Data Integrity, Security & Access Control

| ID | Requirement | Priority |
|---|---|---|
| BR-SEC-01 | The system shall protect captured data (which may include live credentials/secrets) from unauthorized access while in rest | Must |
| BR-SEC-02 | The system shall preserve the integrity of recorded sessions such that any post-capture modification is detectable, to maintain evidentiary chain of custody | Must |
| BR-SEC-03 | The system shall support role-based access control to restrict who can view, export, or modify captured session data | Could |
| BR-SEC-04 | Communication between Capture Nodes and the Brain (whether wired or wireless) shall be encrypted and authenticated to prevent interception or spoofing of captured data in transit. (Note: applies primarily where wireless connectivity is used; wired links may rely on physical security instead.) | Could |

### 5.5 Non-Interference & Operating Environment

| ID | Requirement | Priority |
|---|---|---|
| BR-ENV-01 | The system shall not alter or interfere with the device under test's normal operation during passive monitoring. This constraint applies exclusively to passive monitoring activities and does not apply to active reconnaissance actions performed under BR-ACT-01/BR-ACT-03, which are explicitly intended to interact with and affect the DUT's behavior upon operator confirmation. | Must |
| BR-ENV-02 | The system's operational runtime — including live monitoring, correlation, session recording, and active reconnaissance activities — shall be fully operable in an isolated, air-gapped network environment with no dependency on external internet connectivity. Initial installation/setup (e.g., pulling Docker images and dependencies per BR-DEP-01) may require internet access and is not subject to this constraint, provided the system remains fully functional offline once deployed. | Must |

### 5.6 Extensibility

| ID | Requirement | Priority |
|---|---|---|
| BR-EXT-01 | The system shall be extensible to support additional communication protocols as new device types are assessed | Should |
| BR-EXT-02 | The system shall provide a documented integration interface (e.g., API or scripting interface) allowing external hardware capture tools to connect as an additional data source alongside native Capture Nodes | Could |

### 5.7 Deployment & Delivery

| ID | Requirement | Priority |
|---|---|---|
| BR-DEP-01 | The system shall be packaged and delivered as a containerized, reproducible deployment (e.g., Docker/Docker Compose), allowing the Brain and its dependencies to be installed and run consistently across different environments without manual environment setup. Internet access may be used during initial setup/installation, but is not required afterward for normal operation, in line with BR-ENV-02. | Must |
| BR-DEP-02 | The system shall support exporting/importing its configuration and recorded session data independently of the container lifecycle, to allow backup and migration between deployments | Should |

### 5.8 Acceptance, Adoption & Support

| ID | Requirement | Priority |
|---|---|---|
| BR-ACC-01 | The system shall pass a defined User Acceptance Testing (UAT) process, validated against real DUT scenarios, before being accepted as delivered | Must |
| BR-ACC-02 | The project shall include a training plan/material to onboard analysts on system operation before full adoption | Should |


--------



## 6. Assumptions & Constraints

- Physical access to the device under test is available for attaching capture hardware.
- An isolated test-bench network will be used for the system, separate from general business networks.
- The system is intended for use by one analyst/team on one device at a time in its first version, not for large-scale or concurrent testing.

## 7. Cost / Benefit Considerations

| Cost Factors | Benefit Factors |
|---|---|
| Development cost (in-house or vendor) | Reduced analyst hours per assessment (recurring time savings) |
| Capture hardware (per-protocol devices) | Improved finding quality and defensibility in reports |
| Ongoing maintenance/support | Reusable platform across multiple future engagements/devices |
| Training time for analysts to adopt the tool | Faster report turnaround, potentially enabling more assessments per period |

*(Detailed budget figures to be provided by the Business Owner/Sponsor; a rough estimate should be captured in the Project Charter and refined in the Statement of Work.)*

## 8. Future Business Opportunities (Not Yet Scoped)

- AI/ML-assisted analysis modules (e.g., automated report drafting, smarter secret detection, anomaly detection across sessions, automated protocol classification) — see the project's technical documentation for a detailed breakdown.
- Computer-vision-assisted capabilities (e.g., reading device status LEDs as an additional data source, automated board/component identification).

These may become part of a Phase 3 or later roadmap once Phase 1/2 are delivered and real usage data is available to justify further investment.

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

