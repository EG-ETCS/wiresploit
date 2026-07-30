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