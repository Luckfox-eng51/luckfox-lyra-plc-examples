# Luckfox Lyra PLC Examples

[简体中文](README.md) | **English**

Example code for Luckfox Lyra PLC, organized into three categories by purpose.
Each example keeps its code, configuration, scripts, and run instructions together.
Product documentation is kept outside this repository.

> This repository currently contains a scaffold and an example template.
> No runnable or hardware-verified examples have been added.
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

<details>
<summary>Add an example (maintainers)</summary>

- Group by technology first, then add a language layer as needed, such as
  `protocols/mqtt/python/publish/` or
  `protocols/modbus/rtu/c/temperature-sensor/`.
- Organize Node-RED and OpenPLC by individual example under the platform.
  No language layer is needed.
- Keep source, dependencies, configuration, service files, and installation
  and test scripts with each example. Use the example's `config/` for configuration,
  `systemd/` for service files, and `scripts/` for scripts when needed.
- Use Chinese `README.md` by default and `README_EN.md` for English.
  Both versions share the same code and configuration.

Copy [the independent example template](.templates/example/README_EN.md)
from the repository root:

```sh
mkdir -p protocols/modbus/rtu/c
cp -R .templates/example protocols/modbus/rtu/c/temperature-sensor
```

The template lives in the hidden `.templates/` directory for maintainers.
Complete functionality, wiring, dependencies, build and run commands,
expected results, and common errors. Implement the applicable build targets.
The template Makefile is unconfigured and deliberately fails on build or run targets.

</details>

<details>
<summary>Changelog</summary>

## Unreleased

- Reduce customer entry points to hardware control, communication protocols, and applications.
- Merge integrated projects into `applications/`, grouping Node-RED and OpenPLC
  examples under their respective platforms.
- Remove empty root configuration and script placeholders; keep supporting
  resources with each example.
- Move the independent example template into the hidden `.templates/example/` directory.
- Default READMEs and the changelog to Chinese with links to English versions.
- Provide three category links and a concise directory tree, with maintainer
  instructions in a collapsible section.

</details>
