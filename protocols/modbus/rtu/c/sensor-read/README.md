# 通用 Modbus RTU 传感器读取（C）

**简体中文** | [English](README_EN.md)

使用 [sensor_read.c](src/sensor_read.c)，根据客户自己的传感器说明书设置地址、类型、倍率和单位，读取 RS485 Modbus RTU 数据。支持功能码 `03` / `04`，支持 16/32 位整数和 32 位浮点数；不写寄存器，也不自动识别传感器。只需同时显示温湿度时，可使用 [温湿度专用例程](../temperature-humidity/README.md)。

## 1. 接线与供电

准备已正常运行的 Luckfox Lyra PLC、RS485 Modbus RTU 传感器和符合设备额定电压的直流电源。电脑通过 SSH 上传源码。

以下以温湿度传感器演示接线，其他传感器按各自铭牌和手册连接。正面朝向自己时，端子从左到右为 **`+`、`−`、`A+`、`B−`**；铭牌供电范围为 **DC 5–30V**。`+` / `−` 即下表中的 `DC+` / `DC−`。

![温湿度传感器实物与端子标识](images/temperature-humidity-sensor.webp)

![PLC、直流电源和 RS485 传感器接线](images/sensor-wiring.webp)

[查看 SVG 接线图](images/sensor-wiring.svg)。本例传感器的供电范围为 **DC 5–30V**，使用 **12V 电源**。图示为 PLC 与非隔离传感器共用该电源；图中表示电气连接关系，分线位置不代表实际端子位置，接线以设备标识为准。

断电后接线：

| 电源或 PLC 端子 | 连接到 |
| --- | --- |
| 12V 电源 `DC+` | PLC 电源输入正极、传感器 `+` |
| 12V 电源 `DC−` | PLC 电源输入负极、传感器 `−` |
| PLC 第 10 脚 `TA / A+` | 传感器 `A` / `A+` |
| PLC 第 9 脚 `TB / B−` | 传感器 `B` / `B−` |
| PLC 第 6 脚 `SGND` | 传感器 RS485 信号地；普通非隔离四线传感器通常为 `DC−` |

PLC 的直流端子输入范围为 **7–24V**，本例传感器为 **5–30V**，因此两者可以共用 **12V 电源**。更换传感器时，按新传感器的额定电压选择电源。

同一路电源的负极已共地，但 PLC 电源负极不应直接当作 RS485 信号地；总线参考地按上表接 `SGND`。隔离传感器按厂家定义连接总线侧信号地，不要用电源负极跨接隔离两侧。`PE` 是保护地，不代替 `SGND`。RS422 的第 7、8 脚本例不接。

PLC 的实际端子编号见下图：

![PLC RS485 端子编号](images/plc-rs485-pinout.webp)

> 拨码只控制终端电阻，不用于选择 RS485/RS422 通信模式；同时打开两个拨码也不会增加一路串口。改用 RS422 时，应先断电，按 RS422 接线和总线终端要求重新配置。

程序使用 `/dev/ttyS2`，默认通过 `/dev/gpiochip0` 的第 `15` 号线（`GPIO0_B7`）控制 TXEN，自动完成发送后切回接收。运行前关闭占用 UART2 的 Web 功能、串口助手和其他程序。

## 2. 从手册确定读取参数

运行前确认：串口参数、Modbus 从站地址、功能码、协议寄存器地址、数据类型及换算公式。默认值为 `9600 8N1`、从站 `1`、功能码 `03`、地址 `0`、读取 `1` 个寄存器并按 `u16` 显示，读取一次后退出。

- `-a` 是从站地址；`-s` 是报文中的起始寄存器地址，支持十进制和 `0x` 十六进制。若手册的 `40001` 对应协议地址 `0`，就用 `-s 0`，以厂家定义为准。
- `-q` 是 **16 位寄存器数量**，不是字节数。一个 32 位数需要两个寄存器，例如 `-q 2 --type f32`。
- 换算公式为：**显示值 = 解码值 × `--scale` + `--offset`**。`--unit` 只设置显示单位，从站地址使用 `-a`。
- 同一次请求的所有数值使用相同的类型、倍率、偏移和单位。不同定义的寄存器分开读取，命令依次执行，不要同时占用串口。

## 3. 上传到 PLC 并编译

**在 Ubuntu 电脑上**，从仓库根目录进入本例程目录。将 `PLC_IP` 替换为 PLC 的实际 IP，用户名按实际系统修改。

```bash
cd protocols/modbus/rtu/c/sensor-read
ssh lyra@PLC_IP 'mkdir -p ~/sensor-read-example/src'
scp src/sensor_read.c lyra@PLC_IP:sensor-read-example/src/
ssh lyra@PLC_IP
```

**登录 PLC 后**编译：

```bash
cd ~/sensor-read-example
gcc -std=c11 -O2 -Wall -Wextra src/sensor_read.c -o sensor_read_plc
```

生成的 `sensor_read_plc` 在 PLC 上运行。如果没有 GCC，在支持 APT 的镜像上先安装：

```bash
sudo apt update
sudo apt install -y build-essential
```

程序仅依赖 Linux 系统接口和 C 标准库，无需安装 Modbus 库。源码中的注释和运行输出为英文。

## 4. 在 PLC 读取

下面均为参数用法示例，寄存器定义需要与自己的传感器手册一致。

读取从站 1 的保持寄存器 0、1，同时显示原始值：

```bash
sudo ./sensor_read_plc -a 1 -f 3 -s 0 -q 2 --raw
```

若温度位于地址 0，为有符号 16 位数，倍率为 0.1：

```bash
sudo ./sensor_read_plc -s 0 --type i16 --scale 0.1 --name Temperature --unit C
```

输出示例（`[time]` 为实际时间）：

```text
[time] Temperature (R0): 27.6 C
Summary: successful=1, failed=0.
```

若湿度位于地址 1，为无符号 16 位数，倍率为 0.1：

```bash
sudo ./sensor_read_plc -s 1 --type u16 --scale 0.1 --name Humidity --unit '%RH'
```

32 位浮点示例：19200、8E1、从站 2、功能码 04、地址 100 起的两个寄存器、字序 CDAB：

```bash
sudo ./sensor_read_plc -b 19200 -p E -a 2 -f 4 -s 100 -q 2 \
  --type f32 --order CDAB --name Pressure --unit kPa --precision 2
```

任一命令添加 `-n 0 -i 1000` 可连续读取，每秒一次；按 `Ctrl+C` 停止。添加 `-n 10` 可读取 10 次。有限次数读取结束后，`successful` 等于请求次数且 `failed=0` 表示本轮读取成功。

## 5. 参数与波特率

完整帮助：`./sensor_read_plc --help`。

| 参数 | 默认值 | 用途 |
| --- | --- | --- |
| `-d PATH` | `/dev/ttyS2` | 串口设备 |
| `-b N` | `9600` | 波特率，与传感器一致 |
| `-p N\|E\|O` / `--stopbits 1\|2` | `N` / `1` | 校验位 / 停止位，数据位固定为 8 |
| `-a N` | `1` | 从站地址，1–247 |
| `-f 3\|4` | `3` | 保持寄存器 / 输入寄存器 |
| `-s N` / `-q N` | `0` / `1` | 起始协议地址 / 寄存器数量（1–16） |
| `--type TYPE` | `u16` | 数据类型，见下表 |
| `--scale N` / `--offset N` | `1` / `0` | 先乘倍率，再加偏移 |
| `--order ORDER` | `ABCD` | 32 位字节顺序：`ABCD`、`BADC`、`CDAB`、`DCBA` |
| `--name TEXT` / `--unit TEXT` | 无 | 显示名称 / 单位 |
| `--precision N` | 自动 | 固定小数位数，0–6 |
| `-n N` | `1` | 请求次数，`0` 连续 |
| `-i MS` / `-t MS` | `1000` / `1000` | 最小请求起始间隔 / 超时（毫秒） |
| `--raw` / `-v` | 关闭 | 原始寄存器值 / 收发报文 |
| `--gpiochip PATH` / `--txen-line N` | `/dev/gpiochip0` / `15` | 手动 TXEN；设备名为 `/dev/ttyS2` 时默认启用 |
| `--auto-direction` | 其他串口默认自动 | USB 转换器使用自动收发，禁用 GPIO 控制 |

| 类型 | 含义 | 每个数值占用寄存器 |
| --- | --- | ---: |
| `u16` | 无符号 16 位整数 | 1 |
| `i16` | 有符号 16 位整数（补码） | 1 |
| `signmag16` | 最高位为符号，其余位为绝对值 | 1 |
| `u32` / `i32` | 无符号 / 有符号 32 位整数 | 2 |
| `f32` | IEEE 754 单精度浮点数 | 2 |

32 位类型的 `-q` 必须为偶数；`--order` 根据传感器手册填写。例如 `ABCD` 表示高字在前且每字节按高位在前，`CDAB` 表示交换两个 16 位字。

**更换波特率只修改 `-b`，无需重新编译。** 可选 `1200`、`2400`、`4800`、`9600`、`19200`、`38400`、`57600`、`115200`，须与传感器及串口能力匹配；此参数不会修改传感器自身的波特率。

## 6. 可选：电脑通过 USB 转 RS485 读取

将传感器的 A/B/信号地接到支持自动收发切换的 USB 转 RS485 转换器，转换器接电脑 USB。传感器仍按额定电压独立供电。测试时断开 PLC 的 A/B，避免两个主站同时发请求。

在 Ubuntu 电脑插入转换器前后分别执行，新增的 tty 设备就是转换器串口：

```bash
ls /dev/tty*
```

下面以 `/dev/ttyACM0` 为例；实际也可能是 `/dev/ttyUSB0` 等，替换为自己电脑检测到的名称。在电脑的本例程目录执行：

```bash
sudo apt update
sudo apt install -y build-essential acl
sudo setfacl -m "u:$(id -un):rw" /dev/ttyACM0
gcc -std=c11 -O2 -Wall -Wextra src/sensor_read.c -o sensor_read_pc
./sensor_read_pc -d /dev/ttyACM0 --auto-direction -s 0 -q 1 --raw
```

USB 重新插拔后重新确认设备名并授权。`_pc` 在电脑编译和运行，`_plc` 在 PLC 编译和运行，不能混用。

## 7. Makefile 与常见问题

也可在具有完整例程目录的机器上使用 `make build`（电脑）或 `make build PLATFORM=plc`（PLC）。`PLATFORM` 只决定文件名，不进行交叉编译。`make check` 检查编译警告，`make run PLATFORM=plc ARGS="--help"` 演示运行入口，`make clean` 删除生成的可执行文件。实际访问 PLC 设备时使用前面的 `sudo` 运行命令。

`make test` 使用 Python 3 和模拟串口验证数据解析、CRC、超时及退出清理；无需传感器。Python 仅用于测试，不是运行 C 程序的依赖。

| 现象 | 处理方法 |
| --- | --- |
| `Permission denied` | PLC 用 `sudo` 运行；电脑重新授予串口权限 |
| 串口或 GPIO 忙 | 关闭占用 UART2 / GPIO15 的 Web 功能和其他程序 |
| 超时、CRC 错误 | 检查供电、A/B/SGND、波特率、校验位和从站地址；加 `-v` 查看报文 |
| `Modbus exception` | 对照手册检查功能码、协议寄存器地址和读取数量 |
| 数值或正负号不正确 | 检查寄存器含义、编码方式和倍率；32 位数据还需检查字节顺序 |
| `Exec format error` | 在当前机器上重新编译源码 |

退出码：`0` 全部读取成功；`1` 读取或解析失败；`2` 参数或设备配置错误；`130` 用户中断。退出时程序释放 TXEN，并恢复串口配置。
