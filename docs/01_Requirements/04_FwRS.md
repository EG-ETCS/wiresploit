# Firmware Requirements Specification (FwRS)

Firmware-specific detailed requirements.

Node firmware implements the same two functions, **capture** and **actions**. Firmware shall honor the enable or disable state of each function: capture disabled means the Node reports no capture events, and actions disabled means the Node does not execute actions.

Firmware shall apply the three setting groups received from the Core:

- **General:** store the Node ID, name, and description, and drive the indication LED in the color the Core assigned.

Firmware shall report the Node state to the Core and shall send a heartbeat. It shall report Online after connecting and before configuration is loaded, Ready after configuration is loaded and before the task starts, and Running while it is capturing, decoding, matching packets, performing actions, or sending data packets. It shall not report Unreachable. On reconnect, firmware shall report Online if no configuration is loaded, and Ready if a configuration is already loaded.
- **Capture:** capture only the selected protocol, using that protocol's configured settings.
- **Actions:** when actions are enabled, run the configured action on the configured trigger. Triggers are manual, on packet match, scheduled, and on peer notification. Actions are inject a DUT payload, run a Node script, and send a notification signal to the Core, to all (the Core and the other Nodes), or to one specific Node.