# Luckfox Lyra PLC Examples

[简体中文](README.md) | **English**

Example code for Luckfox Lyra PLC, organized into three categories by purpose.
Each example keeps its code, configuration, scripts, and run instructions together.
Product documentation is kept outside this repository.

> The [RS485 test](hardware/rs485/README_EN.md) includes C source, wiring diagrams, and build/run instructions for both endpoints.
> Empty directories containing `.gitkeep` reserve categories only.

## Find an Example

| I want to... | Directory | Example types |
| --- | --- | --- |
| Control hardware | [hardware/](hardware/) | GPIO, relays, RS485, and raw CAN access |
| Use a communication protocol | [protocols/](protocols/) | MQTT, Modbus, EtherCAT, and CANopen |
| Run an application | [applications/](applications/) | Node-RED, OpenPLC, Modbus to MQTT, and other complete applications |

## Main Directories

```text
luckfox-lyra-plc-examples/
├── README.md                  # Chinese homepage with an English link
├── README_EN.md               # English homepage
│
├── hardware/                  # Hardware control
│   ├── gpio/
│   ├── relay/
│   ├── rs485/
│   └── can/
│
├── protocols/                 # Communication protocols
│   ├── mqtt/
│   ├── modbus/
│   ├── ethercat/
│   └── canopen/
│
└── applications/              # Platform examples and integrated applications
    ├── node-red/
    │   └── dashboard/
    ├── openplc/
    │   └── modbus-io/
    └── modbus-to-mqtt/
```

Raw CAN access belongs in `hardware/can/`; CANopen belongs in
`protocols/canopen/`. Complete applications combining multiple technologies
belong in `applications/`.
