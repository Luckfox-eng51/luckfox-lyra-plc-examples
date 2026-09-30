# Luckfox Lyra PLC RS485 收发测试

**简体中文** | [English](README_EN.md)

使用同一份 [rs485_test.c](rs485_test.c)，分别在 Ubuntu 电脑和 Luckfox Lyra PLC 上编译，测试 USB 转 RS485 与 PLC 之间的双向通信。程序自动生成测试数据、应答并校验，不需要手工输入数据，也不依赖 Python。

本例采用私有测试帧，包含序号、测试数据和 CRC16，**不是 Modbus RTU 程序**。串口格式固定为 **8 数据位、无校验、1 停止位（8N1），无流控**。

## 1. 硬件准备与接线

需要一台 Ubuntu 电脑、一台已正常供电的 Luckfox Lyra PLC、一个支持自动收发切换的 USB 转 RS485 转换器，以及 A/B 和信号地连接线。电脑和 PLC 还需有可用于 SSH 的网络连接。

![电脑、USB 转 RS485 与 PLC 接线示意图](images/rs485-wiring.webp)

[查看可缩放的 SVG 接线图](images/rs485-wiring.svg)。图中的端子对应表按信号排列；PLC 上的实际端子位置见下方厂家图。

1. 断电接线，将转换器 USB 插头接到电脑 USB 接口，可使用 USB 延长线。
2. 按下表连接转换器和 PLC，A/B 建议使用双绞线。
3. 检查接线后，给 PLC 独立供电。电脑 USB 为转换器供电，不为 PLC 供电。

| USB 转 RS485 端子 | PLC 端子 | PLC 引脚编号 |
| --- | --- | ---: |
| `A+` / `A` | `TA / A+`（RS485_A） | 10 |
| `B-` / `B` | `TB / B-`（RS485_B） | 9 |
| `GND`（信号地） | `SGND` | 6 |

**A 接 A，B 接 B。** 示例转换器外壳上的 `USB TO RS485 (B)` 是型号标识，接线以绿色接线端子的 `GND`、`A+`、`B-` 标记为准。

信号地接 PLC 的第 6 脚 `SGND`，为两端提供共同参考；隔离转换器应使用 RS485 总线侧的信号地。`PE` 是保护地，不能作为本图的 `SGND` 接线点。第 7、8 脚是 RS422 的 `RB`、`RA`，本例不连接，也不需要短接到 `TB`、`TA`。

![PLC RS485、RS422 和 CAN 端子定义](images/plc-rs485-pinout.webp)

> 拨码只控制终端电阻，不用于选择 RS485/RS422 通信模式；同时打开两个拨码也不会增加一路串口。改用 RS422 时，应先断电，按 RS422 接线和总线终端要求重新配置。

### PLC 串口与收发控制

| 项目 | 本例配置 |
| --- | --- |
| RS485 串口 | `/dev/ttyS2`（UART2） |
| TXEN 引脚 | `GPIO0_B7` |
| GPIO 控制器 | `/dev/gpiochip0` |
| 控制器内偏移 | `15` |
| TXEN 电平 | 高电平发送，低电平接收 |

PLC 需要软件控制 TXEN。C 程序在板端完成“TXEN 拉高 → 发送 → 等待 UART 发空 → 立即拉低 TXEN → 接收”，无需手动操作 GPIO。**PLC 无论运行 `ping` 还是 `reply`，都必须保留 GPIO 参数。** 电脑端的 USB 转换器自行切换方向，不加 GPIO 参数。

测试前关闭占用该串口的串口助手或其他应用；如果 PLC Web 页面已打开 UART2，先在页面中关闭 UART2，释放串口和 TXEN GPIO。

## 2. 电脑端：查看串口并授予权限

以下电脑端命令在 Ubuntu 的**终端 A**执行。先进入本仓库根目录，再进入例程目录：

```bash
cd hardware/rs485
```

先拔下 USB 转 RS485 转换器，在 Ubuntu 电脑上执行以下命令，查看当前的 tty 设备：

```bash
ls /dev/tty*
```

再插入转换器，重新执行同一条命令。对比连接前后的列表，新增的 tty 设备就是转换器对应的串口。

**后续电脑端命令均以 `/dev/ttyACM0` 为例。** 如果新增的设备名称不同，将命令中的 `/dev/ttyACM0` 替换为实际名称即可。

安装编译和权限工具，并授予当前用户该串口的读写权限：

```bash
sudo apt update
sudo apt install -y build-essential acl
sudo setfacl -m "u:$(id -un):rw" /dev/ttyACM0
```

USB 重新插拔后，重新确认串口名称并执行授权命令。

## 3. 电脑端：编译程序

在**终端 A** 的 `hardware/rs485` 目录执行：

```bash
gcc -std=c11 -O2 -Wall -Wextra rs485_test.c -o rs485_test_pc
```

这条命令在当前目录生成电脑端程序 `rs485_test_pc`，在 Ubuntu 电脑上运行。

## 4. 板端：上传源码并编译

**第一步：在终端 A（电脑）上传源码。** 文中的 `PLC_IP` 请替换为 PLC 实际 IP，用户名按实际系统修改。

```bash
ssh lyra@PLC_IP 'mkdir -p ~/rs485-example'
scp rs485_test.c lyra@PLC_IP:rs485-example/
```

第一条在 PLC 上创建存放目录，第二条将源码复制进去，按提示输入 PLC 密码即可。

**第二步：打开终端 B，登录 PLC。**

```bash
ssh lyra@PLC_IP
```

**第三步：登录后，在 PLC 上编译。**

```bash
cd ~/rs485-example
gcc -std=c11 -O2 -Wall -Wextra rs485_test.c -o rs485_test_plc
```

这会在 PLC 的 `~/rs485-example` 目录生成板端程序 `rs485_test_plc`，在 PLC 上运行。两端使用同一份源码，分别编译，生成的程序不能混用。

如果提示 `gcc: command not found`，在支持 APT 的 PLC 镜像中执行以下命令，再重新编译：

```bash
sudo apt update
sudo apt install -y build-essential
```

## 5. 执行测试：电脑发送，PLC 应答

本轮使用 **115200 波特率、100 次往返、每帧 256 字节**。

**第一步：在终端 B（PLC）启动应答程序。**

```bash
sudo ./rs485_test_plc -d /dev/ttyS2 -m reply -b 115200 -n 100 -t 60000 \
  --rs485 off --gpiochip /dev/gpiochip0 --txen-line 15
```

看到 `READY` 后，切回终端 A。应答端最多等待请求 60 秒，超时退出后重新启动即可。PLC 命令中的 GPIO 参数用于自动切换发送和接收，运行时保留。

**第二步：在终端 A（电脑）发起测试。**

```bash
./rs485_test_pc -d /dev/ttyACM0 -m ping -b 115200 -n 100
```

程序自动发送数据并检查 PLC 的回复。

**第三步：查看结果。** 两端最后都显示 `SUMMARY ... result=PASS`，表示本轮测试通过。需要提前停止时按 `Ctrl+C`；再次测试前，等待两端程序都退出。

## 6. 反向测试（可选）：PLC 发送，电脑应答

上一轮结束后，可以交换角色测试。

**先在终端 A（电脑）启动应答：**

```bash
./rs485_test_pc -d /dev/ttyACM0 -m reply -b 115200 -n 100 -t 60000
```

**看到 READY 后，在终端 B（PLC）发送：**

```bash
sudo ./rs485_test_plc -d /dev/ttyS2 -m ping -b 115200 -n 100 \
  --rs485 off --gpiochip /dev/gpiochip0 --txen-line 15
```

两端最后都应显示 `result=PASS`。低波特率下，电脑应答端发送完毕后可能多等数秒才退出。

## 7. 常用参数

下表列出常用参数，未指定时使用默认值。查看完整帮助：电脑端执行 `./rs485_test_pc --help`，PLC 端执行 `./rs485_test_plc --help`。

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `-d, --device PATH` | 必填 | 串口路径；本例电脑端为 `/dev/ttyACM0`，PLC 端为 `/dev/ttyS2` |
| `-m, --mode ping\|reply` | 必填 | `ping` 主动发起，`reply` 接收请求并应答 |
| `-b, --baud N` | `9600` | 波特率，**两端必须一致** |
| `-n, --count N` | `100` | 测试帧数，范围 1–1000000；两端设为相同值，`0` 不表示无限运行 |
| `-l, --length N` | `256` | `ping` 的测试数据长度，1–4096 字节；`reply` 根据收到的长度应答 |
| `-t, --timeout MS` | `3000` | 单次发送/接收等待的超时限制，单位 ms，范围 100–600000；手动启动 `reply` 建议 60000 |
| `-g, --gap MS` | `5` | `ping` 的请求间隔，或 `reply` 收到请求后发送应答前的等待，范围 0–60000 ms |
| `-v, --verbose` | 关闭 | 打印每一帧，短测试排查时可添加 |
| `--gpiochip PATH` | `/dev/gpiochip0` | PLC TXEN 所在的 GPIO 控制器 |
| `--txen-line N` | 不启用 GPIO 控制 | 本 PLC 必须设置为 `15`，这是控制器内偏移，不是接线端子编号 |
| `--txen-setup-us N` | `20` | TXEN 拉高后、发出首字节前的等待，单位微秒；通常保持默认 |
| `-r, --rs485 MODE` | `keep` | 内核 RS485 配置：`keep` 保留，`off` 关闭，`high`/`low` 使用内核 RTS 方向控制。本例电脑保持默认，PLC 用 `off` 并由 GPIO 控制方向 |

`--rs485 high/low` 不能代替本板的 `--txen-line 15`，也不能与手动 GPIO TXEN 同时使用。

退出码：`0` 测试通过；`1` 校验失败或超时；`2` 参数、设备配置或 I/O 错误；`130` 用户中断。

## 8. 修改波特率

**修改双方命令中的 `-b`，不需要改源码，也不需要重新编译。** 例如将 `-b 115200` 改为 `-b 9600`。先结束旧测试，然后用相同的新波特率重新启动两端，仍然先 `reply`、后 `ping`。

以 **9600 波特率、6 次往返、32 字节测试数据**为例：

**先在 PLC 执行：**

```bash
sudo ./rs485_test_plc \
  -d /dev/ttyS2 -m reply \
  -b 9600 -n 6 -t 60000 \
  --rs485 off --gpiochip /dev/gpiochip0 --txen-line 15
```

**然后在 Ubuntu 执行：**

```bash
./rs485_test_pc \
  -d /dev/ttyACM0 -m ping \
  -b 9600 -n 6 -l 32 -t 3000 -v
```

源码支持的波特率为 `1200`、`2400`、`4800`、`9600`、`19200`、`38400`、`57600`、`115200`、`230400`、`460800`、`921600`，还需要串口驱动和转换器支持。本程序此前已完成 9600–115200 范围内常用速率的硬件测试，不能将源码接受的所有数值都视为已经验证。

低波特率配合长帧时要增大 `ping` 的 `-t`。例如 9600 波特率、4096 字节数据，一次往返仅线路传输就约需 8.56 秒，可以将 `-t` 设为 `12000`；不能继续沿用 `3000`。这是超时设置示例，该参数组合未在前次测试中验证。

## 9. 常见问题

| 现象 | 检查方法 |
| --- | --- |
| `Permission denied` | 电脑重新执行串口 ACL 授权；PLC 用 `sudo` 运行 |
| 找不到串口 | 对比 USB 转换器连接前后的 tty 设备列表，将命令中的 `/dev/ttyACM0` 替换为新增的设备名称 |
| `Device or resource busy` / GPIO 申请失败 | 退出串口助手、上轮测试或占用 UART2 / GPIO15 的程序；确认 PLC Web 页面已关闭 UART2 |
| `Manual GPIO TXEN requires kernel RS485 mode disabled` | PLC 命令保留 `--rs485 off`，同时保留两个 GPIO 参数 |
| 超时、CRC 错误或内容不符 | 检查 A/B/SGND、两端波特率、先应答后发起的启动顺序，以及是否使用了本例的两个程序 |
| `RS485_IOCTL unavailable` 出现在 USB 端 | 部分 USB 串口不支持内核 RS485 ioctl；电脑保留默认 `keep`，由转换器自动切换方向，以最终测试结果为准 |
| `Exec format error` | 可执行文件架构不匹配，在对应机器上重新编译源码 |

普通串口文本助手或 Modbus 从站不能直接替代本例的 `reply` 程序；两端需要使用相同的测试帧协议。

读取实际 Modbus RTU 传感器请使用 [温湿度读取例程](../../protocols/modbus/rtu/c/temperature-humidity/README.md)或[通用传感器读取例程](../../protocols/modbus/rtu/c/sensor-read/README.md)。
