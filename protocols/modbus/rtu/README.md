# Modbus RTU 例程

**简体中文** | [English](README_EN.md)

通过 Luckfox Lyra PLC 的 RS485 接口读取 Modbus RTU 传感器。C 例程分别在 PLC 或 Linux 电脑上编译，目录内包含源码、接线图和使用步骤。

| 例程 | 适用场景 |
| --- | --- |
| [温湿度传感器读取](c/temperature-humidity/README.md) | 配置温度、湿度寄存器并同时显示测量值 |
| [通用传感器读取](c/sensor-read/README.md) | 按自己的传感器手册设置地址、16/32 位类型、倍率和单位 |

两个程序均使用功能码 `03` / `04` 读取数据，不修改传感器配置。测试 RS485 基础收发可使用 [RS485 收发测试](../../../hardware/rs485/README.md)。
