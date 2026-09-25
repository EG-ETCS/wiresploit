# Hardware Requirements Specification (HwRS)

Hardware-specific detailed requirements.

The hardware is a single Node attached to the Device Under Test. A Node is not a separate capture, snapshot, or injection device. Each Node implements two functions, **capture** and **actions**, and each function can be enabled or disabled independently. A Node may capture only, perform actions only, or do both.

Each Node shall include an indication LED. The LED shall light in the unique color assigned in that Node's general settings, and that color shall be the same color the Core uses for the Node in the Node list.