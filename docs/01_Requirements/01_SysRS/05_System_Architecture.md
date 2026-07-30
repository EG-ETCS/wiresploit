# Wiresploit — System Architecture Document

| Field | Value |
|---|---|
| Project | IoT Reconnaissance & Communication-Block Monitoring System (Wiresploit) |
| Document Type | System Architecture |
| Status | Draft v1.0 |

---
## 1. Overview

Wiresploit is an IoT/OT reconnaissance and communication-block monitoring system designed to provide security analysts with a unified platform for monitoring, recording, correlating, analyzing, and actively interacting with a Device Under Test (DUT).

The system replaces the need for multiple disconnected monitoring and analysis tools by collecting communication events from multiple interfaces, synchronizing them to a common time reference, processing and correlating them in the Brain, and presenting the resulting information through a unified user interface.

---

## 2. Architectural Goals

The architecture is designed to satisfy the following goals:

1. Provide a unified monitoring view across heterogeneous communication interfaces.
2. Preserve accurate timestamps across all captured events.
3. Correlate events from different sources into logical Communication Blocks.
4. Preserve the original chronological order of captured events.
5. Support both passive monitoring and explicitly authorized active reconnaissance.
6. Keep passive monitoring non-interfering with DUT behavior.
7. Enable complete session recording and later review.
8. Support advanced offline analysis of recorded sessions.
9. Protect sensitive captured data through encryption and access control.
10. Support operation in an isolated, air-gapped test-bench environment.
11. Allow new protocols and external capture tools to be added without modifying the core architecture.
12. Provide reproducible deployment using Docker and Docker Compose.

---
## 3. High-Level System Context

The following diagram presents the overall system context.

```mermaid
flowchart LR

    Analyst["Analyst / User"]

    subgraph Wiresploit["Wiresploit System"]
        UI["Web User Interface"]
        Brain["Wiresploit Brain"]
    end

    subgraph CaptureInfrastructure["Capture & Interaction Infrastructure"]
        NetworkCapture["Network Capture<br/> HTTP / TCP / Ethernet / Wi-Fi"]
        CaptureNodes["Onboard Capture Nodes<br/> I2C / SPI / UART / GPIO"]
        WirelessCapture["Wireless Capture<br/> BLE / LoRa / RFID"]
        SnapshotNodes["Snapshot Nodes<br/> Visual / Electrical / Logical"]
        InjectionNodes["Injection / Active Control<br/> Reset / GPIO / RF / Bus Frame"]
        TimeReference["Local PTP Time Reference"]
    end

    DUT["Device Under Test (DUT)"]

    ExternalTools["External Capture Tools<br/>(Semi-Trusted)"]

    Storage["Local Secure Storage"]

    subgraph Installation["Initial Installation"]
        Internet["Internet<br/>(Setup)"]
        Installer["Docker Installation"]
    end


    Analyst --> UI
    UI <--> Brain

    NetworkCapture --> Brain
    CaptureNodes --> Brain
    WirelessCapture --> Brain
    SnapshotNodes --> Brain

    Brain --> SnapshotNodes
    Brain --> InjectionNodes

    SnapshotNodes --> DUT
    InjectionNodes --> DUT

    DUT --> NetworkCapture
    DUT --> CaptureNodes
    DUT --> WirelessCapture

    TimeReference --> Brain
    TimeReference --> CaptureNodes
    TimeReference --> NetworkCapture
    TimeReference --> WirelessCapture
    TimeReference --> SnapshotNodes

    Brain <--> Storage

    ExternalTools --> Brain

    Internet -. "Initial setup" .-> Installer
    Installer -. "Install Wiresploit" .-> Brain
```
---
## 4. Architectural Boundary

The Wiresploit architecture is divided into the following major boundaries:

```mermaid
flowchart TB

    subgraph ExternalEnvironment["External Environment"]
        Analyst["Analyst"]
        ExternalTools["External Capture Tools"]
        DUT["Device Under Test"]
    end

    subgraph WiresploitPlatform["Wiresploit Platform"]
        
        subgraph Presentation["Presentation Layer"]
            UI["Unified User Interface"]
        end

        subgraph Core["Core Brain"]
            API["API / Application Gateway"]
            Monitoring["Monitoring & Correlation"]
            Session["Session Management"]
            Analytics["Analytics & Forensics"]
            Active["Active Reconnaissance"]
            Security["Security & Access Control"]
            Integration["External Integration API"]
        end

        subgraph Data["Data Layer"]
            SessionStore["Encrypted Session Store"]
            ConfigStore["Configuration Store"]
            ExportStore["Export / Artifact Store"]
        end

        subgraph Hardware["Capture & Hardware Layer"]
            Capture["Capture Nodes"]
            Network["Network Capture"]
            Wireless["Wireless Capture"]
            Snapshot["Snapshot Nodes"]
            Injection["Injection / Control Nodes"]
        end

        TimeSync["Local Time Synchronization"]
    end

    Analyst --> UI
    ExternalTools --> Integration
    DUT --> Capture
    DUT --> Network
    DUT --> Wireless

    UI --> API

    API --> Monitoring
    API --> Session
    API --> Analytics
    API --> Active
    API --> Security

    Monitoring --> Session
    Monitoring --> Analytics

    Active --> Injection
    Active --> Snapshot

    Capture --> Monitoring
    Network --> Monitoring
    Wireless --> Monitoring

    Monitoring --> SessionStore
    Session --> SessionStore

    Analytics --> SessionStore
    Analytics --> ExportStore

    Security --> SessionStore
    TimeSync --> Capture
    TimeSync --> Network
    TimeSync --> Wireless
    TimeSync --> Snapshot
    TimeSync --> Monitoring
```
---

## 5. Monitoring and Correlation Pipeline

The monitoring pipeline transforms raw communication data into a unified timeline.

```mermaid
flowchart LR

    subgraph Sources["Data Sources"]
        Network["Network Capture"]
        Bus["Onboard Bus Capture"]
        Wireless["Wireless Capture"]
        External["External Capture Tools"]
        Snapshot["Snapshot Nodes"]
    end

    subgraph Processing["Brain Processing Pipeline"]
        Ingest["Data Ingestion"]
        Validate["Schema / Format Validation"]
        Timestamp["Timestamp Validation"]
        Decode["Protocol Decoding"]
        Normalize["Event Normalization"]
        Correlate["Temporal + Logical Correlation"]
        Order["Chronological Ordering"]
        Block["Communication / Snapshot Block Creation"]
    end

    Timeline["Unified Live Timeline"]

    Network --> Ingest
    Bus --> Ingest
    Wireless --> Ingest
    External --> Ingest
    Snapshot --> Ingest

    Ingest --> Validate
    Validate --> Timestamp
    Timestamp --> Decode
    Decode --> Normalize
    Normalize --> Correlate
    Correlate --> Order
    Order --> Block
    Block --> Timeline
```

The pipeline ensures that events from different communication layers are processed consistently.

For example, a single DUT operation may result in:

1. An HTTP request.
2. A TCP packet.
3. An internal SPI transaction.
4. An I2C transaction.
5. A GPIO state change.

The correlation engine groups logically related events into a Communication Block.

---

## 6. Time Synchronization Architecture

Accurate time synchronization is a core architectural capability.

The Brain and all relevant capture components synchronize against a local time reference.

```mermaid
flowchart TB

    Time["Local PTP Time Reference"]

    Brain["Wiresploit Brain"]
    Network["Network Capture"]
    Node1["Capture Node 1"]
    Node2["Capture Node 2"]
    Wireless["Wireless Capture"]
    Snapshot["Snapshot Node"]

    Time --> Brain
    Time --> Network
    Time --> Node1
    Time --> Node2
    Time --> Wireless
    Time --> Snapshot

    Brain --> Correlation["Event Correlation"]

    Network --> Correlation
    Node1 --> Correlation
    Node2 --> Correlation
    Wireless --> Correlation
    Snapshot --> Correlation

    Correlation --> Timeline["Unified Time-Ordered Timeline"]
```

The architecture must support:

* A common local time reference.
* Timestamped events.
* Timestamped snapshots.
* Clock drift measurement.
* Maximum observed clock drift documentation.
* Monitoring of clock synchronization status.

The time reference must operate locally within the isolated test-bench network and must not depend on Internet connectivity.

---

## 7. Session Recording Architecture

Session recording is responsible for creating a persistent representation of a monitoring or active reconnaissance session.

```mermaid
flowchart TD

    Start["Analyst Starts Session"]

    Capture["Capture Events"]

    Timestamp["Timestamp Events"]

    Process["Decode + Normalize + Correlate"]

    Live["Unified Live Timeline"]

    Record["Session Recorder"]

    Secure["Encrypt + Protect Session"]

    Store["Local Encrypted Session Storage"]

    Stop["Analyst Stops Session"]

    Persist["Persist Session"]

    Start --> Capture
    Capture --> Timestamp
    Timestamp --> Process
    Process --> Live
    Process --> Record

    Record --> Secure
    Secure --> Store

    Stop --> Persist
    Persist --> Store
```

A recorded session contains sufficient information to:

* Reconstruct the original event sequence.
* Preserve original timestamps.
* Review the session.
* Search captured data.
* Run analytics.
* Generate behavior diagrams.
* Reconstruct memory.
* Generate findings.
* Export artifacts.

---
## 8. Analytics and Forensics Architecture

Analytics operates primarily on recorded sessions.

```mermaid
flowchart TD

    Session["Encrypted Recorded Session"]

    Replay["Session Replay"]

    Search["Sensitive Data Search"]

    AutoDetect["Automatic Sensitive Data Detection"]

    Findings["Findings Engine"]

    Memory["Memory Reconstruction"]

    Behavior["Behavior Diagram Generation"]

    Metrics["Usage & ROI Metrics"]

    Export["Export & Reporting"]

    Session --> Replay
    Session --> Search
    Session --> AutoDetect
    Session --> Memory
    Session --> Behavior
    Session --> Metrics

    Search --> Findings
    AutoDetect --> Findings

    Findings --> Export
    Memory --> Export
    Behavior --> Export
    Metrics --> Export
```

---
## 9. Sensitive Data Detection

The system supports both analyst-driven searching and automatic detection.

```mermaid
flowchart LR

    Session["Recorded Session"]

    SearchInput["Analyst Search Query"]

    Patterns["Predefined Sensitive Data Patterns"]

    SearchEngine["Search & Detection Engine"]

    Matches["Matching Events"]

    Findings["Findings"]

    Timeline["Timeline Location"]

    Session --> SearchEngine
    SearchInput --> SearchEngine
    Patterns --> SearchEngine

    SearchEngine --> Matches
    Matches --> Findings
    Matches --> Timeline
```

The detection engine may identify:

* Credentials.
* Tokens.
* API keys.
* Other predefined sensitive-data patterns.

Each finding should retain traceability to:

* Packet/event.
* Timestamp.
* Protocol.
* Timeline location.
* Detection method.

---
## 10. Memory Reconstruction Architecture

Memory reconstruction processes captured bus transactions to reconstruct a logical memory map.

```mermaid
flowchart LR

    Session["Recorded Session"]

    BusEvents["SPI / I2C Bus Transactions"]

    Parser["Address / Data Pair Parser"]

    Aggregator["Memory Map Aggregator"]

    Classification["Coverage Classification"]

    MemoryMap["Reconstructed Memory Map"]

    Summary["Completeness Summary"]

    Export["Memory Export"]

    Session --> BusEvents
    BusEvents --> Parser
    Parser --> Aggregator
    Aggregator --> Classification
    Classification --> MemoryMap

    MemoryMap --> Summary
    MemoryMap --> Export
```

Memory ranges are classified as:

* Fully observed.
* Partially observed.
* Never captured.

The reconstructed memory map is not intended to perform firmware disassembly or static binary analysis.

---
## 11. Active Reconnaissance Architecture

Active reconnaissance is isolated logically from passive monitoring.

```mermaid
flowchart TB

    Analyst["Analyst"]

    UI["Active Reconnaissance UI"]

    Mode["Active Reconnaissance Mode"]

    Config["Trigger Configuration"]

    Validate["DUT Capability Validation"]

    Summary["Action Summary"]

    Confirm{"Explicit Confirmation?"}

    Audit["Immutable Audit Log"]

    Execute["Active Action Executor"]

    Injection["Injection / Control Node"]

    Snapshot["Snapshot Node"]

    DUT["Device Under Test"]

    Response["DUT Response"]

    Capture["Capture Infrastructure"]

    Brain["Brain Correlation Engine"]

    Timeline["Unified Timeline"]

    Analyst --> UI
    UI --> Mode
    Mode --> Config
    Config --> Validate

    Validate --> Summary
    Summary --> Confirm

    Confirm -->|No| Abort["Cancel Action"]
    Confirm -->|Yes| Audit

    Audit --> Execute

    Execute --> Injection
    Execute --> Snapshot

    Injection --> DUT
    Snapshot --> DUT

    DUT --> Response
    Response --> Capture

    Capture --> Brain
    Brain --> Timeline
```

Active reconnaissance actions include:

* Hardware reset.
* Power-cycle.
* GPIO pulse generation.
* Wireless signal replay.
* Onboard protocol frame replay.
* Signal injection.

All active operations require:

1. Explicit Active Reconnaissance mode.
2. Parameter configuration.
3. Parameter validation.
4. Action summary.
5. Explicit analyst confirmation.
6. Audit logging.
7. Execution.
8. DUT response capture.
9. Correlation and analysis.

---

## 12. Passive vs Active Operational Modes

```mermaid
flowchart LR

    System["Wiresploit"]

    Passive["Passive Monitoring Mode"]

    Active["Active Reconnaissance Mode"]

    PassiveCapture["Listen-only Capture"]
    ActiveAction["Authorized DUT Interaction"]

    NoInterference["No DUT Signal Alteration"]
    Confirmation["Explicit Analyst Confirmation"]

    System --> Passive
    System --> Active

    Passive --> PassiveCapture
    PassiveCapture --> NoInterference

    Active --> ActiveAction
    ActiveAction --> Confirmation
```

### Passive Monitoring

In Passive Monitoring mode:

* The system listens to monitored interfaces.
* No packets are injected.
* No signals are transmitted.
* No DUT state is intentionally modified.
* Capture data is processed and correlated.

### Active Reconnaissance

In Active Reconnaissance mode:

* The analyst explicitly enters active mode.
* The analyst configures the action.
* The system validates the configuration.
* The system presents the expected impact.
* The analyst explicitly confirms execution.
* The system performs the authorized action.
* DUT responses are captured and analyzed.

---
## 13. Security Architecture

Security is implemented across authentication, authorization, data protection, and auditability.


```mermaid
flowchart TB

    User["User"] --> Auth["Authentication"] --> RBAC["RBAC Enforcement"]

    subgraph ROLES["Roles"]
        Viewer["Viewer<br/>Passive Monitoring + View"]
        Analyst["Analyst<br/>Passive Monitoring + Analysis + Active Actions"]
        Admin["Admin<br/>Full Access + Administration"]
    end

    RBAC --> Viewer
    RBAC --> Analyst
    RBAC --> Admin

    subgraph CAPABILITIES["Capabilities"]
        Passive["Passive Monitoring<br/>Network / Bus / Wireless"]
        Sessions["Recorded Sessions<br/>Encrypted"]
        Findings["Findings / Exports"]
        Active["Active Reconnaissance<br/>Reset / GPIO / RF / Bus"]
        Config["Configuration Management"]
    end

    Viewer --> Passive
    Viewer --> Sessions

    Analyst --> Passive
    Analyst --> Sessions
    Analyst --> Findings
    Analyst --> Active

    Admin --> Passive
    Admin --> Sessions
    Admin --> Findings
    Admin --> Active
    Admin --> Config

    Active --> Audit["Immutable Audit Trail"]
    Findings --> Audit
    Config --> Audit
```

Role permissions:

| Role    | View | Analyze | Export | Active Actions | Administration |
| ------- | ---: | ------: | -----: | -------------: | -------------: |
| Viewer  |  Yes | Limited |     Yes |             No |             No |
| Analyst |  Yes |     Yes |    Yes |            Yes |             No |
| Admin   |  Yes |     Yes |    Yes |            Yes |            Yes |

---
## 14. Extensibility Architecture

```mermaid
flowchart LR
    subgraph CORE["Core System (unchanged)"]
        PIPELINE["Correlation & Monitoring Pipeline"]
    end

    subgraph PLUGINS["Protocol Plugin Modules"]
        P1["I2C Module"]
        P2["SPI Module"]
        P3["UART Module"]
        P4["Bluetooth Module"]
        P5["...new protocol module"]
    end

    IFACE{{"Standard Protocol Interface\nconnect / parse / send / disconnect"}}

    P1 & P2 & P3 & P4 & P5 -.->|"implements"| IFACE
    IFACE --> PIPELINE

    EXTAPI["Documented External API\n(REST)"]
    EXTTOOL["External Capture Tool"]
    VALID["Schema/Format Validation"]
    LOG["Connection & Submission Log"]

    EXTTOOL --> EXTAPI --> VALID --> LOG --> PIPELINE
```

---
## 15. Deployment Architecture

The system is designed to run in an isolated test-bench environment.

```mermaid
flowchart TB

    subgraph Internet["Internet"]
        Registry["Container Registry / Package Sources"]
    end

    subgraph Installation["Initial Installation"]
        Docker["Docker / Docker Compose"]
    end

    subgraph AirGap["Air-Gapped Test Bench"]

        subgraph Host["Wiresploit Host"]
            Brain["Wiresploit Brain Container"]
            UI["UI Container"]
            Storage["Persistent Storage Volume"]
        end

        Time["Local PTP Server"]

        Capture["Capture Nodes"]

        Network["Network Capture"]

        Wireless["Wireless Capture"]

        Snapshot["Snapshot Nodes"]

        DUT["Device Under Test"]
    end

    Registry -. "Initial Setup" .-> Docker

    Docker --> Brain
    Docker --> UI

    Brain --> Storage

    Time --> Brain
    Time --> Capture
    Time --> Network
    Time --> Wireless
    Time --> Snapshot

    DUT --> Capture
    DUT --> Network
    DUT --> Wireless

    Brain --> Capture
    Brain --> Network
    Brain --> Wireless

    Brain --> Snapshot
```

Runtime operation must not depend on:

* Internet access.
* Cloud services.
* Cloud-based storage.
* Online license checks.
* External telemetry.
* Internet-hosted time synchronization.

Internet connectivity may only be required during initial installation or image acquisition.

---
## 16. Docker Deployment Model

The recommended deployment model uses Docker Compose.

```mermaid
flowchart TB

    Compose["Docker Compose"]

    UI["UI Service"]
    Brain["Brain Service"]
    Database["Local Data Service"]
    Storage["Persistent Volume"]
    Network["Network / Capture Integration"]

    Compose --> UI
    Compose --> Brain
    Compose --> Database

    Brain --> Database
    Database --> Storage

    Brain --> Network
```

The deployment must ensure:

* All Docker images use pinned versions.
* Dependencies are locked.
* Containers can be recreated without data loss.
* Persistent data is stored outside the container writable layer.
* Configuration can be exported.
* Session data can be exported.
* Configuration and sessions can be imported.
* Imported data is validated before application.
* Import/export operations are logged.

---

## 17. Final Architecture Summary

The Wiresploit architecture is centered around the **Brain**, which provides the core processing and orchestration capabilities of the system.

The complete architecture follows two main paths: Passive Monitoring and Active Reconnaissance:

```mermaid
flowchart LR

    DUT["Device Under Test"]

    Sources["Network + Onboard Bus + Wireless"]

    Capture["Capture Infrastructure"]

    Time["Common Local Time"]

    Brain["Wiresploit Brain"]

    Correlation["Decode + Normalize + Correlate"]

    Timeline["Unified Timeline"]

    Session["Session Recording"]

    Analytics["Analytics & Forensics"]

    Active["Active Reconnaissance"]

Passive["Passive Reconnaissance"]

    Security["Security + RBAC + Audit"]

    Storage["Encrypted Local Storage"]

    Reports["Reports + Exports"]

    User["Analyst"]

    DUT --> Sources
    Sources --> Capture
    Capture --> Brain

    Time --> Capture
    Time --> Brain

    Brain --> Correlation
    Correlation --> Timeline

    Timeline --> User

    Brain --> Session
    Session --> Storage

    Storage --> Analytics
    Analytics --> Reports

    User --> Active
    Active --> DUT

    Passive --> DUT
    User --> Passive
    

    Brain --> Security
    Security --> Storage
```

The overall architecture therefore provides a single platform that connects:

**Passive Reconnaissance:**

**Analyst → Passive Reconnaissance → DUT → Capture Infrastructure → Brain → Correlation → Unified Timeline → Session Storage → Analytics → Reports**

**Active Reconnaissance:**

**Analyst → Active Reconnaissance → Injection / Control Nodes → DUT → Capture Infrastructure → Brain → Correlation → Analysis**

The complete data and processing flow can be summarized as:

**DUT → Network / Onboard Bus / Wireless → Capture Infrastructure → Time Synchronization → Brain → Decode + Normalize + Correlate → Unified Timeline → Session Recording → Encrypted Local Storage → Analytics & Forensics → Reports & Exports**

This architecture provides a clear separation between **passive observation** and **active interaction** while preserving the central objective of Wiresploit: **unifying heterogeneous DUT communication capture, passive and active reconnaissance, correlation, analysis, and evidence generation into a single secure and extensible platform.**
