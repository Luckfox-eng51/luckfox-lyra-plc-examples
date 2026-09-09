# Luckfox Lyra PLC 例程仓库

**简体中文** | [English](README_EN.md)

本仓库存放 Luckfox Lyra PLC 的示例代码、应用工程和配套配置、脚本。
README 只保留例程所需的编译、接线和运行说明，不设产品文档目录。

> 当前为目录骨架和例程模板，尚未加入可运行或经过硬件验证的例程。
> 含 `.gitkeep` 的空目录仅用于预留分类。

## 按需求查找

| 我想做什么 | 目录入口 | 内容 |
| --- | --- | --- |
| 使用应用平台 | [applications/](applications/) | Node-RED 流程、OpenPLC 工程 |
| 调试通信协议 | [protocols/](protocols/) | MQTT、Modbus、EtherCAT、CANopen |
| 控制硬件接口 | [hardware/](hardware/) | GPIO、RS485、CAN、继电器 |
| 运行综合案例 | [solutions/](solutions/) | Modbus 转 MQTT、PLC 远程 IO、可视化面板 |
| 查找通用配置 | [configs/](configs/) | systemd、网络、服务和协议配置 |
| 使用配套脚本 | [scripts/](scripts/) | 安装、部署、初始化、测试 |
| 新增独立例程 | [templates/example/](templates/example/) | README、源码、配置、构建入口 |

## 目录结构

```text
luckfox-lyra-plc-examples/
├── README.md                       # 中文首页
├── README_EN.md                    # 英文首页
├── CHANGELOG.md                    # 中文更新记录，可切换英文
│
├── applications/                   # 应用平台例程
│   ├── node-red/
│   └── openplc/
│
├── protocols/                      # 单项通信协议例程
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
├── hardware/                       # 硬件接口与控制例程
│   ├── gpio/
│   ├── rs485/
│   ├── can/
│   └── relay/
│
├── solutions/                      # 多项技术组成的完整案例
│   ├── modbus-to-mqtt/
│   ├── openplc-modbus-io/
│   └── node-red-dashboard/
│
├── configs/                        # 跨例程复用的配置
│   ├── systemd/
│   ├── network/
│   ├── mosquitto/
│   ├── modbus/
│   ├── ethercat/
│   └── canopen/
│
├── scripts/                        # 仓库级辅助脚本
│   ├── install/
│   ├── deploy/
│   ├── init/
│   └── test/
│
└── templates/                      # 新例程起点
    └── example/
        ├── README.md               # 中文说明，可切换英文
        ├── README_EN.md            # 英文说明
        ├── src/
        ├── config/
        ├── Makefile
        └── systemd/
```

## 归类规则

1. **技术在前，语言在后。** 例如 `protocols/mqtt/python/publish/`；
   有协议变体时使用 `protocols/modbus/rtu/c/temperature-sensor/`。
   只在有对应例程时增加语言目录。
2. **每个例程独立。** 源码、配置、依赖版本和构建入口放在同一例程目录。
   Node-RED 使用 `applications/node-red/<例程名>/`，OpenPLC 同理，
   流程和工程文件放在具体例程内部，无需语言层。
3. **按主要用途选择一个位置。** 单项平台、协议、硬件例程分别放入前三类；
   多项技术组合成完整应用时放入 `solutions/`，例如 Modbus 转 MQTT。
   CAN 原始报文收发属于 `hardware/can/`，CANopen 属于 `protocols/canopen/`。
4. **专用资源跟随例程。** 例程自己的配置、服务文件和脚本就近存放，
   根目录 `configs/`、`scripts/` 只放可复用资源。
5. **默认中文，英文可切换。** 所有例程使用 `README.md` 和 `README_EN.md`
   互相链接；两种语言共用同一份代码和配置。

## 新增例程

从仓库根目录复制[例程模板](templates/example/README.md)：

```sh
mkdir -p protocols/modbus/rtu/c
cp -R templates/example protocols/modbus/rtu/c/temperature-sensor
```

填写中英文 README 的七项内容：功能、接线、依赖安装、编译方法、
运行命令、预期结果、常见错误。实现适用的构建目标，移除不需要的模板目录。

模板 Makefile 尚未配置，执行构建或运行目标会明确报错。
