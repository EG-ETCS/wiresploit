# Project Charter

| Field             | Value                                                        |
|-------------------|--------------------------------------------------------------|
| Project Name      | IoT Reconnaissance & Communication-Block Monitoring System   |
| Working Title     | Wiresploit                                                   |
| Document Status   | `final V0.0`                             |
| Date              | 14-7-2026                                                    |
| Prepared by       | Mohamed Salah-elden                                          |

---

## 1. Project Purpose / Justification

Black-box security analysis of IoT devices (e.g., an IP camera) requires correlating events across multiple physical communication layers — network traffic (HTTP/TCP), and internal onboard buses (e.g. I2C, SPI, UART, logic-level signals, etc.), debugging ports. Today this is done manually with disconnected tools (e.g., Wireshark for network traffic, separate logic analyzers for bus traffic), requiring analysts to manually cross-reference timestamps to reconstruct what actually happened during a single logical operation (e.g., a login attempt).

This project exists to eliminate that manual correlation burden by building a system that passively captures, timestamps, and causally correlates communications across all relevant protocols in real time, presenting them as a unified timeline of **Communication Blocks (CBs)** and **Snapshot Blocks (SBs)**, then allows the operator to analyze this data — including searching for hidden secrets within captured communications, and using the data to generate diagrams/visualizations of overall system behavior. The system will also support performing active reconnaissance techniques against the device under test and capturing/analyzing its resulting responses..

## 2. Objectives & Success Criteria

- Provide a live, unified, multi-protocol timeline view of DUT communications, replacing multiple disconnected capture tools.
- Enable operators to analyze captured sessions (e.g. identifying hidden secrets within communications).
- Achieve documented, honestly-reported timestamp correlation accuracy across all capture sources (exact tolerance to be validated against chosen hardware).
- Support fully recordable and replayable capture sessions for reporting and evidentiary purposes.
- Reduce analyst time spent manually cross-referencing captures from separate tools during black-box security assessments.
- Zero (or documented, bounded) interference with the device under test — passive capture only.
- performing active reconnaissance attacks against the device under test and observe/record its behavior in response.

## 3. High-Level Scope

**In scope:**
- A central "core" (Docker-based backend, database, and web UI) that ingests and correlates events.
- Wireless/wired Capture Nodes attached to onboard buses (I2C, SPI, UART, logic-level/GPIO) on the device under test, reporting to the core, The choice of wired vs. wireless connectivity for Capture Nodes is not fixed at this stage and will be determined by the hardware lead/vendor based on technical feasibility, cost, and DUT constraints.
- Local capture of the core's own HTTP/network traffic to/from the device under test.
- Real-time multi-lane timeline display, session recording/replay, filtering, annotation, classifying and export.
- Data analysis capabilities: searching captured communications for hidden secrets or custom search phrase, generating report with diagrams/visualizations of overall system behavior from captured sessions, performing active reconnesance attacks and analyse the device bahaviour.
- Active attacks against the device under test (e.g., bruteforcing, Fuzzing or other active interaction), with capture and analysis of the resulting system responses.

**Out of scope:**
- Automated vulnerability scoring or exploit generation.
- Firmware disassembly / static analysis.
- Fleet-wide or multi-device-simultaneous monitoring (single DUT per session).

*(Full detailed scope lives in the project's Business Requirements Document.)*

## 4. Key Stakeholders

| Role                     | Name                                                   | Responsibility                                                                                                                                                                          |
| ------------------------ | ------------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Business Owner | `Egyptian Computer Emergency Readiness Team (EG-CERT)` | Provides project and business direction, approves scope changes, and gives final acceptance of project deliverables.                                                            |
| Project Manager          | `Mohamed Sayed ElAnsary`                               | Plans and manages project execution, coordinates resources, tracks schedule, budget, risks, and communicates project status to stakeholders.                                            |
| Hardware Lead            | `Youssif Sameh`                                                  | Leads hardware architecture and PCB design, selects electronic components, hardware implementation, integration, and hardware verification.                                    |
| Firmware Lead            | `Bishoy Kamel`                                                  | Designs and develops embedded firmware, device drivers, bootloader, and communication protocols, and supports hardware integration and debugging.                                       |
| Software Lead            | `Mariam Essam`                                                  | Leads software architecture and development, manages implementation of backend/frontend components, APIs, databases, and software integration.                                          |
| QA / Test Owner          | `TBD`                                                  | Defines the testing strategy, prepares test plans and test cases, executes verification and validation activities, tracks defects, and confirms that acceptance criteria are satisfied. |

## 5. Project Manager Authority

The Project Manager is authorized to:
- Coordinate day-to-day work across the development team/vendor and hardware engineer.
- Approve minor scope clarifications that do not affect timeline.
- Escalate to the Owner for: major changes, timeline changes, scope changes affecting the documents.

