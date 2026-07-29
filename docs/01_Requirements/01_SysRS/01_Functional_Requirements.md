# Business Requirements → Functional Requirements

## BR-MON — Monitoring

**Source BR:** BR-MON-01 to BR-MON-08,
**Assignee:** BK,
**Status:** In progress.

### Summary
The monitoring module provides a unified live view of DUT communications across network, onboard-bus, and wireless interfaces, processing and correlating captured events on a synchronized timeline.


### Monitoring System Data Flow

```mermaid
flowchart TD

    DUT["Device Under Test (DUT)"]

    NET["Network Traffic<br/>HTTP / TCP / Ethernet / Wi-Fi"]
    BUS["Onboard Communication<br/>I2C / SPI / UART / GPIO"]
    WIRELESS["Wireless Communication<br/>Bluetooth / LoRa / RFID"]

    NET_CAPTURE["Network Capture"]
    CN["Capture Nodes<br/>Onboard Bus Capture"]
    WIRELESS_CAPTURE["Wireless Capture"]

    BRAIN["Brain<br/>Event Processing + Protocol Decoding"]

    TIMESTAMP["Timestamping & Time Synchronization"]

    CORRELATION["Event Correlation<br/>Temporal + Logical Correlation"]

    TIMELINE["Unified Timeline<br/>Communication Blocks"]

    ANALYST["Analyst<br/>Monitor + Analyze DUT Behavior"]


    DUT --> NET
    DUT --> BUS
    DUT --> WIRELESS

    NET --> NET_CAPTURE
    BUS --> CN
    WIRELESS --> WIRELESS_CAPTURE

    NET_CAPTURE --> BRAIN
    CN --> BRAIN
    WIRELESS_CAPTURE --> BRAIN

    BRAIN --> TIMESTAMP
    TIMESTAMP --> CORRELATION
    CORRELATION --> TIMELINE
    TIMELINE --> ANALYST
```
### Functional Requirements

| FR ID | Requirement | Priority | Acceptance Criteria |
|---|---|---|---|
| FR-MON-01-1 | The system shall display network communication events. | Must | Network events are visible in the monitoring interface. |
| FR-MON-01-2 | The system shall display onboard-bus communication events. | Must | Supported onboard-bus events are visible in the monitoring interface. |
| FR-MON-01-3 | The system shall display wireless communication events. | Must | Supported wireless events are visible in the monitoring interface. |
| FR-MON-01-4 | The system shall provide a unified live view of captured communications. | Must | Events from supported sources appear in one live interface. |
| FR-MON-02-1 | The system shall order events. | Must | Events appear in correct time order on the timeline. |
| FR-MON-03-1 | The system shall decode captured communication data from supported protocols. | Must | Captured data is decoded and displayed according to the selected protocol. |
| FR-MON-03-2 | The system shall record system errors in a dedicated error log file. | Must | Each error is recorded with its timestamp, error type, and error message. |
| FR-MON-04-1 | The system shall synchronize the Brain to the common time reference. | Must | The Brain uses the configured common time reference. |
| FR-MON-04-2 | The system shall document the maximum clock drift between monitoring components. | Must | Maximum observed clock drift is measured and documented. |
| FR-MON-05-1 | The system shall support monitoring through multiple Capture Nodes. | Should | Multiple Capture Nodes can provide captured events to the monitoring system. |
| FR-MON-05-2 | The system shall support simultaneous monitoring of the required communication protocols. | Must | The system can simultaneously capture and display events from all required communication protocols. |
| FR-MON-06-1 | The system shall support triggering a Snapshot Node after a defined delay. | Should | The Snapshot Node is triggered after the configured delay. |
| FR-MON-06-2 | The system shall support triggering a Snapshot Node based on a detected packet or pattern. | Should | Detection of a packet or pattern activates the Snapshot Node. |
| FR-MON-06-3 | The system shall capture the DUT state when a Snapshot Node is triggered. | Should | The  physical, electrical, or internal state is captured. |
| FR-MON-07-1 | The system shall timestamp Snapshot Node outputs. | Should | Each snapshot output contains a timestamp. |

### Assumptions & Dependencies
- The Brain can synchronize to a common local time reference.
- Required hardware for supported network, onboard-bus, wireless, and snapshot monitoring is available.
- Snapshot capabilities depend on the type of Snapshot Node being used.
- Maximum supported nodes, protocols, latency, and clock drift require validation on the final hardware configuration.
- The first version supports monitoring one DUT per session.

### Open Questions
- What is the target maximum latency for live event display?
- What is the acceptable maximum clock drift?
- Will NTP or PTP be used for time synchronization?
- What communication protocols must the system support?
- Which Snapshot Node types will be supported in the first release?
- Which snapshot analysis capabilities will be available in the first release?

---

## BR-ANA — Analytics

**Source BR:**
**Assignee:** HK
**Status:** Not started

### Summary
_One or two sentences restating the business need in plain language._

### Functional Requirements

| FR ID | Requirement | Priority | Acceptance Criteria |
|---|---|---|---|
| FR-ANA-01 | | Must / Should / Could | |
| FR-ANA-02 | | | |

### Assumptions & Dependencies
-

### Open Questions
-

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

**Source BR:**
**Assignee:** RA
**Status:** Not started

### Summary
_One or two sentences restating the business need in plain language._

### Functional Requirements

| FR ID | Requirement | Priority | Acceptance Criteria |
|---|---|---|---|
| FR-SEC-01 | | Must / Should / Could | |
| FR-SEC-02 | | | |

### Assumptions & Dependencies
-

### Open Questions
-

---

## BR-DEP — Deployment

**Source BR:**
**Assignee:** M
**Status:** Not started

### Summary
_One or two sentences restating the business need in plain language._

### Functional Requirements

| FR ID | Requirement | Priority | Acceptance Criteria |
|---|---|---|---|
| FR-DEP-01 | | Must / Should / Could | |
| FR-DEP-02 | | | |

### Assumptions & Dependencies
-

### Open Questions
-

---

## BR-EXT — Extensibility

**Source BR:**
**Assignee:** M
**Status:** Not started

### Summary
_One or two sentences restating the business need in plain language._

### Functional Requirements

| FR ID | Requirement | Priority | Acceptance Criteria |
|---|---|---|---|
| FR-EXT-01 | | Must / Should / Could | |
| FR-EXT-02 | | | |

### Assumptions & Dependencies
-

### Open Questions
-

---

## BR-ENV — Environment

**Source BR:**
**Assignee:** YS
**Status:** Not started

### Summary
_One or two sentences restating the business need in plain language._

### Functional Requirements

| FR ID | Requirement | Priority | Acceptance Criteria |
|---|---|---|---|
| FR-ENV-01 | | Must / Should / Could | |
| FR-ENV-02 | | | |

### Assumptions & Dependencies
-

### Open Questions
-

---

## BR-ACC — Accessibility

**Source BR:** 
**Assignee:** YS
**Status:** Not started

### Summary
_One or two sentences restating the business need in plain language._

### Functional Requirements

| FR ID | Requirement | Priority | Acceptance Criteria |
|---|---|---|---|
| FR-ACC-01 | | Must / Should / Could | |
| FR-ACC-02 | | | |

### Assumptions & Dependencies
-

### Open Questions
-