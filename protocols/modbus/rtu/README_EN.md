# Modbus RTU Examples

[简体中文](README.md) | **English**

Read Modbus RTU sensors through the Luckfox Lyra PLC RS485 port. Compile the C examples on the PLC or a Linux computer. Each directory includes source code, wiring diagrams, and usage instructions.

| Example | Use case |
| --- | --- |
| [Temperature and humidity reader](c/temperature-humidity/README_EN.md) | Configure temperature/humidity registers and display both measurements |
| [Generic sensor reader](c/sensor-read/README_EN.md) | Set addresses, 16/32-bit types, scaling, and units from your sensor manual |

Both programs read with function codes `03` / `04` and do not change sensor settings. For basic serial communication checks, use the [RS485 test](../../../hardware/rs485/README_EN.md).
