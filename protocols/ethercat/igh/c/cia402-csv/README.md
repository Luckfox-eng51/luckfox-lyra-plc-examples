# Luckfox Lyra PLC EtherCAT：iRDT CiA402 CSV 示例

[简体中文](README.md) | [English](README_EN.md)

## 示例功能

本例程在 Luckfox Lyra PLC 上使用 IgH EtherCAT Master 控制一个 CiA402 从站，演示：

- 注册并连接总线上的第 1 个从站（Alias 0、Position 0），同时校验从站身份信息；
- 将运行模式 `0x6060` 设置为 `9`（CSV，周期同步速度模式）；
- 注册 RxPDO/TxPDO，并以 2 ms 周期交换过程数据；
- 默认只监视状态；显式传入 `--enable` 后，按 CiA402 状态机使能并下发目标速度；
- 捕获退出信号，先写入零速度和禁用控制字，再释放主站。

> 示例验证环境：Rockchip RK3506B、Linux 6.1.99、IgH EtherCAT Master 1.6.10、iRDT Driver。第一次运行必须空载、低速，并准备好独立急停或断电手段。

## 适用环境

| 配置项 | 示例配置 |
| --- | --- |
| 主站设备 | Luckfox Lyra PLC，Rockchip RK3506B |
| CPU 架构 | `armv7l`，32 位 ARM |
| Linux 内核 | `6.1.99`，SMP PREEMPT |
| EtherCAT 主站 | IgH EtherCAT Master `1.6.10` |
| 用户空间 ABI | ARM EABI5 hard-float |
| 默认 EtherCAT 接口 | `eth1` |
| 测试从站 | iRDT Driver，在线名称 `iRDT_CIA402` |

部署前在 Lyra PLC 上检查：

```sh
uname -r
uname -m
ethercat version
ip -br link
```

当前程序严格校验从站 0 的 Vendor ID `0x00000009`、Product Code `0x26483052` 和 Revision `0x00010211`。更换从站或升级固件后，必须同步核对源码配置和在线身份。

若从站身份或 PDO 映射不同，必须先修改并验证程序，不能直接使能电机。

## 硬件连接

1. 关闭伺服使能和动力电源。
2. 将 Lyra PLC 的 EtherCAT 专用网口连接到从站的 EtherCAT IN 口。
3. 按驱动器说明接好控制电源、动力电源、电机、编码器和急停回路。
4. 上电后先执行 `ethercat slaves`，确认只发现预期设备。

不要把 EtherCAT 口接入普通办公网络；调试时不要让 NetworkManager、DHCP 客户端或其他网络服务配置该接口。

从站上电后先检查物理链路：

```sh
cat /sys/class/net/eth1/carrier
cat /sys/class/net/eth1/speed
```

当前测试环境的正常结果为 `carrier=1`、`speed=100`。100 Mbps 是 EtherCAT 标准通信速率；如果 `carrier=0`，应先检查从站供电、IN 端口方向和网线，不要直接修改 PDO 配置。

本示例的完整数据流为：Lyra PLC → EtherCAT → LAN9252 → SPI → STM32G474 → 三相 PWM → 无刷直流电机；反馈由 MT6701 编码器沿相反方向返回主站。

## PDO 映射

| 方向 | PDO | 对象 | 位宽 | 程序用途 |
| --- | --- | --- | ---: | --- |
| Master → Slave | `0x1602` | `0x6040:00` | 16 | Controlword |
| Master → Slave | `0x1602` | `0x60FF:00` | 32 | Target velocity |
| Slave → Master | `0x1A02` | `0x6041:00` | 16 | Statusword |
| Slave → Master | `0x1A02` | `0x6064:00` | 32 | Position actual value |

`0x6064` 是本例程的实际位置反馈。iRDT 当前固件的 TxPDO 固定为 `0x6041 + 0x6064`。

## 准备 IgH Master

推荐先在目标板系统中安装 IgH Master。至少应具备：

- 内核模块：`ec_master.ko`、`ec_generic.ko`；
- 运行库：`libethercat.so.1.2.0`（以及对应链接）；
- 工具：`ethercat`；
- 开发头文件：`ecrt.h`。

`scripts/ec.sh deploy` 也可以从外部资源目录部署这些文件，但二进制内核模块和动态库不应提交到本示例仓库。资源目录结构应为：

```text
<assets>/
├── ec_master.ko
├── ec_generic.ko
├── libethercat.so.1.2.0
├── ethercat
└── ecrt.h                 # 可选；编译例程时需要
```

部署命令：

```sh
sudo EC_ASSETS_DIR=/path/to/igh-assets ./scripts/ec.sh deploy
```

内核模块必须和 Lyra 当前运行内核完全匹配。若模块来自其他内核版本或配置，不能直接使用。

## 编译

在 Lyra PLC 上进入本目录：

```sh
make check
make build
```

输出文件为 `bin/ethercat_csv_test`。若 `ecrt.h` 或 `libethercat.so` 安装在非标准路径，可传入：

```sh
make build CPPFLAGS='-I/path/to/include' LDFLAGS='-L/path/to/lib'
```

本示例的部署路径为 `/root/ethercat-app`。本仓库编译生成的 `bin/ethercat_csv_test` 即测试程序；也可使用产品软件包中提供的同版本测试程序。上传前不要随意替换程序依赖的 IgH 主站版本和从站身份参数。

在开发机完成交叉编译或在 Lyra 上编译后，上传测试程序：

```sh
ssh root@<PLC管理口IP> "mkdir -p /root/ethercat-app"
scp bin/ethercat_csv_test root@<PLC管理口IP>:/root/ethercat-app/
ssh root@<PLC管理口IP> "chmod +x /root/ethercat-app/ethercat_csv_test"
```

登录 Lyra 后检查动态依赖：

```sh
ldd /root/ethercat-app/ethercat_csv_test
```

`ec.sh` 属于主站管理脚本，应由产品软件包或系统镜像提供。若目标系统尚未安装，可再上传本目录的脚本：

```sh
scp scripts/ec.sh root@<PLC管理口IP>:/root/ethercat-app/
ssh root@<PLC管理口IP> "chmod +x /root/ethercat-app/ec.sh"
```

## 启动 EtherCAT Master

在 `/root/ethercat-app` 中启动主站。脚本支持 source 调用，也支持直接执行：

```sh
cd /root/ethercat-app
EC_IF=eth1 . ./ec.sh start
. ./ec.sh status
ethercat slaves
```

脚本会清除该接口上的 IP 地址、加载 IgH 模块，并检查 `/dev/EtherCAT0`。请确认 `EC_IF` 指向 EtherCAT 专用接口，填错接口可能中断设备的普通网络连接。

主站启动后执行：

```sh
ls -l /dev/EtherCAT0
ethercat master
ethercat slaves
ethercat slaves -v
```

启动成功至少应满足：EtherCAT 专用网口链路为 UP、`/dev/EtherCAT0` 已创建、在线从站数量与接线一致、设备名为 `iRDT_CIA402` 且状态后没有错误标记 `E`。仅启动主站时从站处于 `PREOP` 属于正常现象；周期控制程序运行后才会申请进入 `OP`。

进入 `PREOP` 后，应以在线结果确认固定 PDO，而不是只依据主站源码：

```sh
ethercat pdos -p 0
ethercat xml -p 0 > /tmp/iRDT_CIA402.xml
grep -n -E '6064|Position' /tmp/iRDT_CIA402.xml
```

当前设备 XML 将 `0x6064:00` 明确声明为 `Position Actual Value`。

## 运行

### 1. 只监视，不使能电机

```sh
/root/ethercat-app/ethercat_csv_test
```

这是第一次上机时推荐的运行方式。程序应周期打印 EtherCAT domain 状态、master/slave 状态字和对象 `0x6064` 的当前值。

新开一个终端检查周期数据：

```sh
ethercat slaves
ethercat domains -d 0 -v
```

零输出示例：

```text
Domain0: Size 12, WorkingCounter 3/3

Output（Master → Slave）：00 00 00 00 00 00
0x6040 Control Word = 0x0000
0x60FF Target Velocity = 0

Input（Slave → Master）：21 12 00 00 00 00
0x6041 Status Word = 0x1221
0x6064 Position Actual Value = 0
```

状态字 `0x1221` 使用掩码 `0x006F` 后为 `0x0021`，表示 `Ready to switch on`。`0x6064` 的数值是有符号 32 位位置原始值；其机械单位和缩放关系必须以驱动器对象字典及固件配置为准。

### 2. 标准 CiA402 状态机使能

先使用很小的目标速度，并确保机构空载：

```sh
/root/ethercat-app/ethercat_csv_test --enable --velocity 50 --strategy standard
```

参数说明：

- `--enable`：允许程序使能驱动器；不传时只监视；
- `--velocity N`：目标速度原始值，默认值为 `50`；该数值不应脱离设备手册直接写成 rpm；
- `--strategy standard`：依次写入 `0x0006`、`0x0007`、`0x000F`；
- `--cpu N`：实时循环绑定的 CPU 核，默认 `2`。

可使用 Makefile 运行：

```sh
sudo make run RUN_ARGS='--enable --velocity 50 --strategy standard --cpu 2'
```

`--strategy ffff` 会写入控制字 `0xFFFF`，仅保留给已确认从站行为的诊断场景，不建议用于正常控制。

按 `Ctrl+C` 或发送 `SIGTERM` 停止。程序会尝试发送约 500 ms 的零速度和禁用命令；该软件动作不能替代硬件急停。

电机完全停止后才能关闭主站：

```sh
cd /root/ethercat-app
EC_IF=eth1 . ./ec.sh stop
```

禁止用拔掉 EtherCAT 网线代替正常停机，也不能用普通 EtherCAT 控制程序替代独立急停回路、安全继电器或驱动器安全功能。

## 预期结果

- 仅启动主站时，`ethercat slaves` 能看到 `iRDT_CIA402`，从站处于 `PREOP`；
- 启动周期控制程序后，从站切换为 `OP`；
- `/dev/EtherCAT0` 存在；
- `ethercat domains -d 0 -v` 显示 `Domain0: Size 12, WorkingCounter 3/3`；
- 零输出测试中 Control Word 为 `0x0000`、Target Velocity 为 `0`；
- 状态字示例 `0x1221` 使用掩码 `0x006F` 后为 `0x0021`，即 Ready to switch on；
- `Master: slaves=1 link=1 AL=0x08` 表示从站通常已进入 OP；
- 标准使能时，状态字依次进入 Ready to switch on、Switched on、Operation enabled，随后目标值由 `0` 变为 `50`；
- TxPDO 中的实际位置反馈随电机运动更新，Domain WKC 保持完整。

## 常见错误

### `ecrt.h: No such file or directory`

IgH 开发头文件未安装。复制/安装 `ecrt.h`，或通过 `CPPFLAGS=-I...` 指定目录。

### `cannot find -lethercat` 或启动时找不到 `libethercat.so.1`

安装与目标系统架构匹配的 IgH 用户态库，执行 `ldconfig`；非标准路径需设置 `LDFLAGS` 或运行时库搜索路径。

### `/dev/EtherCAT0` 不存在

Master 模块未加载、模块与内核不匹配，或没有成功绑定网口。查看 `dmesg`，并运行 `sudo ./scripts/ec.sh status`。

### 找到从站但程序无法配置

核对 ESI/XML 中的 Vendor ID、Product Code、Revision、同步管理器和 PDO 映射。当前程序固定使用 `0x1602` 和 `0x1A02`，TxPDO 为 `0x6041 + 0x6064`。

### 程序无法设置实时调度或锁定内存

以 root 运行，或正确配置 `CAP_SYS_NICE`、`CAP_IPC_LOCK` 与实时资源限制。开发阶段建议先用 root 验证。

### `AL` 状态未进入 `0x08` 或工作计数不稳定

检查网线、从站供电、PDO 长度、周期、应用层状态，以及是否同时运行了两份周期程序。当前示例程序没有显式配置 DC 同步；如更换为要求 DC 的从站，应按其手册扩展程序。

### 电机旋转但 `0x6064` 始终为零

```sh
ethercat pdos -p 0
ethercat domains -d 0 -v
ethercat upload -p 0 -t int32 0x6064 0
```

如果 Domain 中对应的 4 个原始字节发生变化、程序日志仍显示零，检查主站 PDO 偏移、位宽、有符号解析和字节序；如果原始字节始终为零，而驱动器本地控制能够带动电机，则检查 STM32 固件是否刷新位置对象以及 MT6701 编码器链路。

### 持续出现 `TIMED OUT` 或 `UNMATCHED`

应用申请或释放主站瞬间出现少量警告可以继续观察；运行期间持续高频出现则需要排查：末端 OUT 是否误接回普通网口、是否同时运行两份主站/周期程序、是否在周期运行时频繁执行 SDO 或状态修改命令、实时线程是否阻塞，以及网线和从站供电是否稳定。

## 诊断命令

```sh
ethercat master
ethercat slaves
ethercat slaves -v
ethercat pdos -p 0
ethercat domains -d 0 -v
ethercat upload -p 0 -t uint16 0x6041 0
ethercat upload -p 0 -t int8   0x6061 0
ethercat upload -p 0 -t uint16 0x603F 0
ethercat xml -p 0
ethercat cstruct -p 0
```

`ethercat sii_read` 输出二进制内容，应保存到文件，不能直接打印到终端：

```sh
ethercat sii_read -p 0 > /tmp/slave0_sii.bin
```

## 停止或重载 Master

```sh
cd /root/ethercat-app
EC_IF=eth1 . ./ec.sh stop
```

`stop` 会终止本示例进程并卸载 EtherCAT 模块；在共享设备上执行前，请确认没有其他 EtherCAT 应用正在运行。
