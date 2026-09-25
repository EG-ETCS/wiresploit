# Software Requirements Specification (SwRS)

Software-specific detailed requirements.

The Core shall group analysis engines as single-session or multiple-session, and then as single-domain or multiple-domain. A domain is a protocol. Single-session engines also include an active group: packet replay, packet injection, and fuzzing. Recording, export, and usage metrics are not analysis engines.

The Core shall configure each Node's **capture** and **actions** functions independently. Software shall treat a Node as capture-only, actions-only, or both, and shall not offer actions for a Node whose actions function is disabled or ingest capture events from a Node whose capture function is disabled.

The Core shall edit three setting groups on each Node:

- **General:** Node ID, Node name, Node description, and a unique color. When a Node connects, the Core reads the Node ID, assigns the unique color, and adds the Node to the Node list in that color.

The Core shall show each Node in one state: Online, Ready, Running, or Unreachable. The Core shall set Unreachable when the Node's heartbeat times out. Online, Ready, and Running, and the return from Unreachable to Online or Ready, come from the Node's report. A reconnecting Node that was not configured is shown as Online. A reconnecting Node that was already configured is shown as Ready.
- **Capture:** the selected protocol and that protocol's settings, applied only while capture is enabled.
- **Actions:** one trigger (manual, on packet match, scheduled, or on peer notification) and one action (inject a DUT payload, run a Node script, or send a notification signal). A notification signal is sent to the Core, to all (the Core and the other Nodes), or to one specific Node.