# Luckfox Lyra PLC 例程仓库

**简体中文** | [English](README_EN.md)

Luckfox Lyra PLC 示例代码，按使用目的分为三个入口。
每个例程的代码、配置、脚本和运行说明放在一起，不设产品文档目录。

> 当前为目录骨架和例程模板，尚未加入可运行或经过硬件验证的例程。
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

<details>
<summary>新增例程（维护者）</summary>

- 先按技术分类，再按需增加语言层，例如 `protocols/mqtt/python/publish/`、
  `protocols/modbus/rtu/c/temperature-sensor/`。
- Node-RED 和 OpenPLC 按平台下的具体例程组织，无需语言层。
- 源码、依赖、配置、服务文件和安装、测试脚本跟随各自例程。
  配置放在例程的 `config/`，服务文件放在 `systemd/`，脚本按需放在 `scripts/`。
- 默认说明使用中文 `README.md`，通过 `README_EN.md` 提供英文版本。
  两种语言共用代码与配置。

从仓库根目录复制[独立例程模板](.templates/example/README.md)：

```sh
mkdir -p protocols/modbus/rtu/c
cp -R .templates/example protocols/modbus/rtu/c/temperature-sensor
```

模板放在隐藏的 `.templates/` 目录，供维护者使用。
填写功能、接线、依赖、编译、运行、预期结果和常见错误，并实现适用的构建目标。
模板 Makefile 尚未配置，执行构建或运行目标会明确报错。

</details>
