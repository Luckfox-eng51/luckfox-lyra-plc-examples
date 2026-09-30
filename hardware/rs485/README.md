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
3. 检查接线并按下文设置 RS485 终端电阻拨码后，给 PLC 独立供电。电脑 USB 为转换器供电，不为 PLC 供电。

| USB 转 RS485 端子 | PLC 端子 | PLC 引脚编号 |
| --- | --- | ---: |
| `A+` / `A` | `TA / A+`（RS485_A） | 10 |
| `B-` / `B` | `TB / B-`（RS485_B） | 9 |
| `GND`（信号地） | `SGND` | 6 |

**A 接 A，B 接 B。** 示例转换器外壳上的 `USB TO RS485 (B)` 是型号标识，接线以绿色接线端子的 `GND`、`A+`、`B-` 标记为准。

信号地接 PLC 的第 6 脚 `SGND`，为两端提供共同参考；隔离转换器应使用 RS485 总线侧的信号地。`PE` 是保护地，不能作为本图的 `SGND` 接线点。第 7、8 脚是 RS422 的 `RB`、`RA`，本例不连接，也不需要短接到 `TB`、`TA`。

![PLC RS485、RS422 和 CAN 端子定义](images/plc-rs485-pinout.webp)

### RS485 终端电阻拨码开关

拨码开关位于 **PLC 网口旁边的侧面**，位置见下图。按图中观察方向，三个开关从左到右标为 `RS485`、`RS422`、`CAN`；本例使用**最左侧标有 RS485 的开关**，操作时以机壳丝印为准。

![PLC RS485 终端电阻拨码开关位置及向下拨动方向](images/plc-rs485-switch.webp)

[查看拨码开关放大图（SVG）](images/plc-rs485-switch.svg)。照片用于定位开关，不代表测试时应直接照搬照片中的拨动状态。

> **注意：RS485 与 RS422 共用串口资源。** 本板只有一路 RS422/RS485 串口（UART2，`/dev/ttyS2`）。第 9、10 脚既是 RS485 的 `B/A`，也是 RS422 的 `TB/TA`，不能将它们当作两路独立串口，同时连接两套设备进行独立通信。
>
> 本例使用 RS485 二线通信，只接第 6、9、10 脚，第 7、8 脚 `RB/RA` 保持不接，也不要与 `TB/TA` 短接。为明确本例的终端配置，**RS485 拨码向下，接入 120Ω；未使用的 RS422 拨码向上，断开其终端电阻**。CAN 拨码按实际 CAN 总线需要设置。
>
> 拨码只控制终端电阻，不用于选择 RS485/RS422 通信模式；同时打开两个拨码也不会增加一路串口。改用 RS422 时，应先断电，按 RS422 接线和总线终端要求重新配置。

| RS485 拨码位置 | 功能 | 本例设置 |
| --- | --- | --- |
| 向下拨 | 在 RS485 A/B 之间接入板载 **120Ω 终端电阻** | 本例只有 USB 转换器和 PLC 两个端点，PLC 端设为此位置 |
| 向上拨 | 断开板载终端电阻 | 多节点总线中，PLC 位于总线中间时使用 |

在 PLC 断电时设置拨码，再上电测试。转换器端也应按其说明配置总线末端的 120Ω 终端电阻；若已经内置或启用了终端电阻，不要再重复并接。多节点总线只在两个物理末端接入终端电阻，中间节点关闭。

波特率由程序 `-b` 参数设置，TXEN 收发方向由程序控制 GPIO15，均不由终端电阻拨码设置。

向下接入 120Ω 的定义见[厂家产品页“资源简介”](https://www.luckfox.cn/Luckfox-Lyra-PLC)。

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

插入 USB 转换器后，查看设备：

```bash
lsusb
ls -l /dev/serial/by-id/
ls -l /dev/ttyUSB* /dev/ttyACM* 2>/dev/null
```

转换器可能是 `/dev/ttyUSB0`，也可能是 `/dev/ttyACM0`。某一类节点不存在是正常的，以实际检测结果为准。插拔前后对比可确定新增设备。

优先使用 `/dev/serial/by-id/` 下的稳定路径。下面是本次转换器的路径，请替换为自己看到的名称；如果没有 `by-id`，可直接设置实际设备节点，例如 `RS485_PORT=/dev/ttyACM0`：

```bash
RS485_PORT=/dev/serial/by-id/usb-1a86_USB_Single_Serial_5658002104-if00
readlink -f "$RS485_PORT"
```

安装编译工具和 ACL 工具，并只给当前用户增加该串口的读写权限：

```bash
sudo apt update
sudo apt install -y build-essential acl
sudo setfacl -m "u:$(id -un):rw" "$RS485_PORT"
getfacl -p "$RS485_PORT"
```

ACL 输出中应包含当前用户的 `rw-` 权限。USB 重新插拔后需要再次执行 `setfacl`。`RS485_PORT` 只在当前终端有效，新开终端时需要重新设置。

若希望通过用户组长期授权，也可以执行 `sudo usermod -aG dialout "$(id -un)"`，然后完整注销并重新登录；适用于设备节点所属组为 `dialout` 的 Ubuntu 系统。

## 3. 电脑端：编译程序

在终端 A 的 `hardware/rs485` 目录执行：

```bash
mkdir -p build
gcc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror \
  rs485_test.c -o build/rs485_test
./build/rs485_test --help
```

生成电脑端程序 `build/rs485_test`。程序只使用 Linux 系统接口和 libc，不需要安装串口库。

## 4. 板端：复制源码并编译

先在**终端 A（Ubuntu）**设置 PLC 登录地址。`10.10.20.101` 是示例地址，替换为 PLC 当前 IP，用户名按实际系统修改：

```bash
PLC_HOST=lyra@10.10.20.101
ssh "$PLC_HOST" 'mkdir -p ~/rs485-example'
scp rs485_test.c "$PLC_HOST":rs485-example/
```

按提示输入 PLC 登录密码。上述命令将源码复制到 PLC 登录用户的 `~/rs485-example/`，不是复制电脑编译出的程序。

再打开**终端 B**，登录 PLC：

```bash
ssh lyra@10.10.20.101
```

登录后，以下命令都在 **PLC 上**执行：

```bash
cd ~/rs485-example
gcc --version
ls -l /dev/ttyS2 /dev/gpiochip0
```

如果板端没有 GCC，使用板端系统的包管理器安装。对于支持 APT 的镜像，可执行：

```bash
sudo apt update
sudo apt install -y build-essential
```

在板端编译同一份源码：

```bash
mkdir -p build
gcc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror \
  rs485_test.c -o build/rs485_test
./build/rs485_test --help
sudo -v
```

两端程序路径都叫 `build/rs485_test`，但位于不同机器上。常见 Ubuntu 电脑为 x86_64，PLC 为 ARM，**两端分别编译，不能直接混用可执行文件**。PLC 使用 `sudo` 运行，以访问串口和 GPIO。

## 5. 执行测试：电脑发送，PLC 应答

保留两个终端：终端 A 运行 Ubuntu 程序，终端 B 通过 SSH 运行 PLC 程序。**先启动 `reply`，看到 `READY` 后，再启动 `ping`。**

本轮使用 **115200 波特率、100 次往返、256 字节测试数据**。

### 5.1 先在终端 B（PLC）启动应答

```bash
cd ~/rs485-example
sudo ./build/rs485_test \
  -d /dev/ttyS2 -m reply \
  -b 115200 -n 100 -t 60000 -g 5 \
  --rs485 off --gpiochip /dev/gpiochip0 --txen-line 15
```

出现 `TXEN ... active_high=1 idle=0` 和 `READY ... mode=reply` 后切换到终端 A。`-t 60000` 允许等待请求最多 60 秒，方便手动切换终端；它不会让每次应答等待 60 秒。若应答端等待超时退出，重新启动应答端。

这里使用 `--rs485 off`，在测试期间关闭内核 RTS 方向控制，改用本板独立的 TXEN GPIO。正常退出时程序恢复之前的串口设置。

### 5.2 在终端 A（Ubuntu）发起测试

仍在 `hardware/rs485` 目录中，且已设置 `RS485_PORT`：

```bash
./build/rs485_test \
  -d "$RS485_PORT" -m ping \
  -b 115200 -n 100 -l 256 -t 3000 -g 5
```

电脑发送请求后等待 PLC 应答，并检查序号、内容和 CRC。测试结束后，两端都应看到 `SUMMARY ... result=PASS`。

### 5.3 检查结果

两端程序退出后，立即在各自终端查看退出码：

```bash
echo $?
```

退出码应为 `0`。读取退出码前不要先执行其他命令，因为 `$?` 代表上一条命令的状态。

| SUMMARY 字段 | 本轮正常值 | 含义 |
| --- | --- | --- |
| `sent` | `100` | 本端发送帧数 |
| `valid` / `expected` | `100` / `100` | 有效帧数达到预期 |
| `timeouts` / `mismatch` | 均为 `0` | 无超时、无内容不匹配 |
| `crc_errors` / `discarded_bytes` | 均为 `0` | 无 CRC 错误、无异常字节丢弃 |
| `unexpected` | `0` | 无意外帧 |
| `txen_cycles` | PLC 为 `100`，电脑为 `0` | GPIO 收发切换次数 |
| `result` | `PASS` | 本端测试通过 |

按 `Ctrl+C` 可以提前停止，退出码为 `130`。结束后程序释放设备，并在正常清理时将 TXEN 置低。再次测试前，确认两端上一轮程序都已退出。

## 6. 反向测试：PLC 发送，电脑应答

第一轮结束后交换角色，验证 PLC 主动发起通信。

**先在终端 A（Ubuntu）启动应答：**

```bash
./build/rs485_test \
  -d "$RS485_PORT" -m reply \
  -b 115200 -n 100 -t 60000 -g 5
```

**看到 READY 后，在终端 B（PLC）发起：**

```bash
sudo ./build/rs485_test \
  -d /dev/ttyS2 -m ping \
  -b 115200 -n 100 -l 256 -t 3000 -g 5 \
  --rs485 off --gpiochip /dev/gpiochip0 --txen-line 15
```

同样检查两端 `result=PASS` 和退出码 `0`。USB 应答端发送最后一帧后会保留短暂的排空等待，低波特率下可能多等数秒才退出，等待它回到命令行即可。

## 7. 常用参数

完整参数可通过 `./build/rs485_test --help` 查看。

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `-d, --device PATH` | 必填 | 串口路径；电脑使用实际 USB 串口，PLC 使用 `/dev/ttyS2` |
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
sudo ./build/rs485_test \
  -d /dev/ttyS2 -m reply \
  -b 9600 -n 6 -t 60000 \
  --rs485 off --gpiochip /dev/gpiochip0 --txen-line 15
```

**然后在 Ubuntu 执行：**

```bash
./build/rs485_test \
  -d "$RS485_PORT" -m ping \
  -b 9600 -n 6 -l 32 -t 3000 -v
```

源码支持的波特率为 `1200`、`2400`、`4800`、`9600`、`19200`、`38400`、`57600`、`115200`、`230400`、`460800`、`921600`，还需要串口驱动和转换器支持。本程序此前已完成 9600–115200 范围内常用速率的硬件测试，不能将源码接受的所有数值都视为已经验证。

低波特率配合长帧时要增大 `ping` 的 `-t`。例如 9600 波特率、4096 字节数据，一次往返仅线路传输就约需 8.56 秒，可以将 `-t` 设为 `12000`；不能继续沿用 `3000`。这是超时设置示例，该参数组合未在前次测试中验证。

## 9. 常见问题

| 现象 | 检查方法 |
| --- | --- |
| `Permission denied` | 电脑重新执行串口 ACL 授权；PLC 用 `sudo` 运行 |
| 找不到串口 | 确认 USB 转换器已插入，重新查看 `/dev/serial/by-id/` 或实际设备节点 |
| `Device or resource busy` / GPIO 申请失败 | 退出串口助手、上轮测试或占用 UART2 / GPIO15 的程序；确认 PLC Web 页面已关闭 UART2 |
| `Manual GPIO TXEN requires kernel RS485 mode disabled` | PLC 命令保留 `--rs485 off`，同时保留两个 GPIO 参数 |
| 超时、CRC 错误或内容不符 | 检查 A/B/SGND、终端电阻拨码、两端波特率、先应答后发起的启动顺序，以及是否使用了本例的两个程序 |
| `RS485_IOCTL unavailable` 出现在 USB 端 | 部分 USB 串口不支持内核 RS485 ioctl；电脑保留默认 `keep`，由转换器自动切换方向，以最终测试结果为准 |
| `Exec format error` | 可执行文件架构不匹配，在对应机器上重新编译源码 |

普通串口文本助手或 Modbus 从站不能直接替代本例的 `reply` 程序；两端需要使用相同的测试帧协议。
