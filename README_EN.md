# Luckfox Lyra PLC Examples

[简体中文](README.md) | **English**

Example code, application projects, and supporting configuration and scripts for
Luckfox Lyra PLC. README files cover wiring, building, and running examples.
Product documentation is kept outside this repository.

> This repository currently contains a scaffold and an example template.
> No runnable or hardware-verified examples have been added.
> Empty directories containing `.gitkeep` reserve categories only.

## Find an Example

| I want to... | Directory | Contents |
| --- | --- | --- |
| Use an application platform | [applications/](applications/) | Node-RED flows and OpenPLC projects |
| Work with a communication protocol | [protocols/](protocols/) | MQTT, Modbus, EtherCAT, and CANopen |
| Control a hardware interface | [hardware/](hardware/) | GPIO, RS485, CAN, and relays |
| Run an integrated project | [solutions/](solutions/) | Modbus to MQTT, remote PLC IO, and dashboards |
| Find shared configuration | [configs/](configs/) | systemd, network, service, and protocol configuration |
| Use supporting scripts | [scripts/](scripts/) | Installation, deployment, initialization, and tests |
| Add an independent example | [templates/example/](templates/example/) | README, source, configuration, and build entry point |

## Directory Structure

```text
luckfox-lyra-plc-examples/
├── README.md                       # Chinese homepage
├── README_EN.md                    # English homepage
├── CHANGELOG.md                    # Chinese changelog with an English link
│
├── applications/                   # Application platform examples
│   ├── node-red/
│   └── openplc/
│
├── protocols/                      # Individual protocol examples
│   ├── mqtt/
│   │   ├── c/
│   │   ├── python/
│   │   └── nodejs/
│   ├── modbus/
│   │   ├── rtu/
│   │   └── tcp/
│   ├── ethercat/
│   │   └── igh-master/
│   └── canopen/
│
├── hardware/                       # Hardware interface and control examples
│   ├── gpio/
│   ├── rs485/
│   ├── can/
│   └── relay/
│
├── solutions/                      # Projects combining several technologies
│   ├── modbus-to-mqtt/
│   ├── openplc-modbus-io/
│   └── node-red-dashboard/
│
├── configs/                        # Configuration shared across examples
│   ├── systemd/
│   ├── network/
│   ├── mosquitto/
│   ├── modbus/
│   ├── ethercat/
│   └── canopen/
│
├── scripts/                        # Repository-wide supporting scripts
│   ├── install/
│   ├── deploy/
│   ├── init/
│   └── test/
│
└── templates/                      # Starting point for new examples
    └── example/
        ├── README.md               # Chinese instructions with an English link
        ├── README_EN.md            # English instructions
        ├── src/
        ├── config/
        ├── Makefile
        └── systemd/
```

## Organization Rules

1. **Technology first, language second.** For example,
   `protocols/mqtt/python/publish/`. Use
   `protocols/modbus/rtu/c/temperature-sensor/` when a protocol has variants.
   Add language directories as implementations become available.
2. **Keep each example independent.** Put its source, configuration, dependency
   versions, and build entry point together. Node-RED examples use
   `applications/node-red/<example-name>/`; OpenPLC follows the same pattern.
   Keep flows and project files inside each example without a language layer.
3. **Choose one location by primary purpose.** Individual platform, protocol,
   and hardware examples belong in their respective categories.
   Integrated applications such as Modbus to MQTT belong in `solutions/`.
   Raw CAN access belongs in `hardware/can/`; CANopen belongs in
   `protocols/canopen/`.
4. **Keep specific resources with the example.** Place its configuration,
   service files, and scripts locally. Root `configs/` and `scripts/`
   are reserved for reusable resources.
5. **Chinese by default, English available.** Each example uses mutually linked
   `README.md` and `README_EN.md`. Both languages share the same code and configuration.

## Add an Example

Copy [the example template](templates/example/README_EN.md) from the repository root:

```sh
mkdir -p protocols/modbus/rtu/c
cp -R templates/example protocols/modbus/rtu/c/temperature-sensor
```

Complete both READMEs with functionality, wiring, dependencies, compilation,
run commands, expected results, and common errors. Implement the applicable
build targets and remove unused template directories.

The template Makefile is unconfigured and deliberately fails on build or run targets.
