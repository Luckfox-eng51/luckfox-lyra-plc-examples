# Luckfox Lyra PLC 例程仓库

**简体中文** | [English](README_EN.md)

Luckfox Lyra PLC 示例代码，按使用目的分为三个入口。
每个例程的代码、配置、脚本和运行说明放在一起，不设产品文档目录。

> [RS485 收发测试](hardware/rs485/README.md)提供 C 源码、硬件接线图和两端编译运行步骤。
> 含 `.gitkeep` 的空目录仅用于预留分类。

## 查找例程

| 你想做什么 | 进入目录 | 例程类型 |
| --- | --- | --- |
| 控制硬件 | [hardware/](hardware/) | GPIO、继电器、RS485、CAN 收发 |
| 使用通信协议 | [protocols/](protocols/) | MQTT、Modbus、EtherCAT、CANopen |
| 运行应用 | [applications/](applications/) | Node-RED、OpenPLC、Modbus 转 MQTT 等完整应用 |

## 主要目录

```text
luckfox-lyra-plc-examples/
├── README.md                  # 中文首页，可切换英文
├── README_EN.md               # 英文首页
│
├── hardware/                  # 硬件控制
│   ├── gpio/
│   ├── relay/
│   ├── rs485/
│   └── can/
│
├── protocols/                 # 通信协议
│   ├── mqtt/
│   ├── modbus/
│   ├── ethercat/
│   └── canopen/
│
└── applications/              # 平台例程与综合应用
    ├── node-red/
    │   └── dashboard/
    ├── openplc/
    │   └── modbus-io/
    └── modbus-to-mqtt/
```

CAN 原始报文收发放在 `hardware/can/`，CANopen 协议放在
`protocols/canopen/`。多项技术组成的完整应用统一放在 `applications/`。
