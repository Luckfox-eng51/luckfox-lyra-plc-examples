# IgH EtherCAT Master: CiA402 CSV Velocity Control

[简体中文](README.md) | [English](README_EN.md)

## What this example does

This example uses the IgH EtherCAT Master on a Luckfox Lyra PLC to control one CiA402 slave. It demonstrates how to:

- register and connect slave 0 (Alias 0, Position 0) and validate its identity;
- set object `0x6060` to `9` for Cyclic Synchronous Velocity (CSV) mode;
- register the RxPDO and TxPDO and exchange process data every 2 ms;
- monitor communication without enabling the motor by default;
- follow the CiA402 state machine and send a target velocity only when `--enable` is supplied;
- send zero velocity and a disabled controlword before releasing the master on exit.

> Verified example environment: Rockchip RK3506B, Linux 6.1.99, IgH EtherCAT Master 1.6.10, and iRDT Driver. Start unloaded, at low speed, with an independent emergency stop or power disconnect available.

## Requirements

- Luckfox Lyra PLC, `armv7l` 32-bit ARM EABI5 hard-float, Linux 6.1.99 SMP PREEMPT;
- IgH Master modules, user library, `ethercat` tool, and `ecrt.h`;
- a CiA402 slave supporting CSV mode;
- iRDT Driver named `iRDT_CIA402`, with Vendor ID `0x00000009`, Product Code `0x26483052`, and Revision `0x00010211`;
- dedicated EtherCAT interface, `eth1` by default (`EC_IF` overrides it).

The source targets Alias 0, Position 0 and validates all three identity values before configuration. Confirm them against `ethercat slaves -v` and the slave ESI/XML before enabling motion.

Before deployment, check the platform on the Lyra PLC:

```sh
uname -r
uname -m
ethercat version
ip -br link
```

The kernel version, CPU architecture, IgH version, and dedicated EtherCAT interface must match the example package. Kernel modules and user-space libraries built for another platform must not be reused directly.

## Hardware connection

1. Disable the motor and turn off its power stage.
2. Connect the dedicated EtherCAT port on the Lyra PLC to the slave's EtherCAT IN port.
3. Connect control power, motor power, motor phases, encoder, and the independent emergency-stop circuit according to the drive manual.
4. Power the iRDT Driver and Lyra PLC, then verify that only the expected slave is discovered.

Do not connect the EtherCAT interface to a normal office network. NetworkManager, DHCP clients, and other network services must not configure the dedicated interface.

Check the physical link before changing any PDO settings:

```sh
cat /sys/class/net/eth1/carrier
cat /sys/class/net/eth1/speed
```

The verified setup reports `carrier=1` and `speed=100`. EtherCAT uses 100 Mbps. If `carrier=0`, check slave power, the IN/OUT direction, and the cable first.

The verified data path is Lyra PLC → EtherCAT → LAN9252 → SPI → STM32G474 → three-phase PWM → brushless DC motor. Feedback from the MT6701 encoder returns along the reverse path.

## PDO mapping

| Direction | PDO | Object | Bits | Purpose |
| --- | --- | --- | ---: | --- |
| Master → Slave | `0x1602` | `0x6040:00` | 16 | Controlword |
| Master → Slave | `0x1602` | `0x60FF:00` | 32 | Target velocity |
| Slave → Master | `0x1A02` | `0x6041:00` | 16 | Statusword |
| Slave → Master | `0x1A02` | `0x6064:00` | 32 | Position actual value |

`0x6064` is the actual position value used by this example. The current iRDT firmware has a fixed TxPDO containing `0x6041 + 0x6064`.

## Build on the Lyra PLC

```sh
make check
make build
```

The output is `bin/ethercat_csv_test`. For non-standard IgH paths:

```sh
make build CPPFLAGS='-I/path/to/include' LDFLAGS='-L/path/to/lib'
```

This example uses `/root/ethercat-app` as its deployment directory. The binary built by this repository is the test application; a matching prebuilt application from the product package may also be used. Do not substitute a binary built for a different IgH version or slave identity.

Upload the test application:

```sh
ssh root@<PLC-management-IP> "mkdir -p /root/ethercat-app"
scp bin/ethercat_csv_test root@<PLC-management-IP>:/root/ethercat-app/
ssh root@<PLC-management-IP> "chmod +x /root/ethercat-app/ethercat_csv_test"
```

Then verify its dynamic dependencies on the Lyra PLC:

```sh
ldd /root/ethercat-app/ethercat_csv_test
```

`ec.sh` is the master-management script and is normally supplied by the product package or system image. If it is not installed, upload the copy from this directory separately:

```sh
scp scripts/ec.sh root@<PLC-management-IP>:/root/ethercat-app/
ssh root@<PLC-management-IP> "chmod +x /root/ethercat-app/ec.sh"
```

## Optional deployment helper

Do not commit target-specific `.ko`, `.so`, or executable binaries to this example repository. If IgH is not installed system-wide, prepare an external asset directory containing `ec_master.ko`, `ec_generic.ko`, `libethercat.so.1.2.0`, `ethercat`, and optionally `ecrt.h`, then run:

```sh
sudo EC_ASSETS_DIR=/path/to/igh-assets ./scripts/ec.sh deploy
```

Kernel modules must match the exact kernel running on the Lyra.

## Start the master

```sh
cd /root/ethercat-app
EC_IF=eth1 . ./ec.sh start
. ./ec.sh status
ethercat slaves
```

The script removes IP addresses from the selected interface. Verify `EC_IF` carefully so that a normal management interface is not disrupted.

Check the master and slave after startup:

```sh
ls -l /dev/EtherCAT0
ethercat master
ethercat slaves
ethercat slaves -v
```

A successful startup requires the dedicated link to be UP, `/dev/EtherCAT0` to exist, the discovered slave count to match the wiring, and `iRDT_CIA402` to appear without an `E` error flag. PREOP is normal while only the master is running; the cyclic application requests OP after it starts.

Confirm the fixed PDO mapping from the online slave instead of relying only on the application source:

```sh
ethercat pdos -p 0
ethercat xml -p 0 > /tmp/iRDT_CIA402.xml
grep -n -E '6064|Position' /tmp/iRDT_CIA402.xml
```

The current slave XML explicitly declares `0x6064:00` as `Position Actual Value`.

## Run

Monitor only first:

```sh
/root/ethercat-app/ethercat_csv_test
```

Open another terminal and inspect the cyclic process data:

```sh
ethercat slaves
ethercat domains -d 0 -v
```

Example zero-output process image:

```text
Domain0: Size 12, WorkingCounter 3/3

Output (Master → Slave): 00 00 00 00 00 00
0x6040 Control Word = 0x0000
0x60FF Target Velocity = 0

Input (Slave → Master): 21 12 00 00 00 00
0x6041 Status Word = 0x1221
0x6064 Position Actual Value = 0
```

Applying the CiA402 state mask gives `0x1221 & 0x006F = 0x0021`, which means `Ready to switch on`. The `0x6064` value is a signed 32-bit raw position value; its mechanical unit and scaling must be confirmed from the drive object dictionary and firmware configuration.

After confirming the slave identity, PDO layout, mechanics, and safety circuit, enable at a low target value:

```sh
/root/ethercat-app/ethercat_csv_test --enable --velocity 50 --strategy standard
```

- `--enable` permits drive enable; without it the application only monitors;
- `--velocity N` selects the raw target velocity value, default `50`; do not call it rpm unless the device documentation confirms that unit;
- `--strategy standard` applies controlwords `0x0006`, `0x0007`, then `0x000F`;
- `--cpu N` binds the real-time loop to one CPU, default `2`.

`--strategy ffff` writes controlword `0xFFFF`; it is retained only for device-specific diagnostics and is not recommended for normal operation.

Press `Ctrl+C` to stop. The program sends zero velocity and a disabled controlword for about 500 ms before releasing the master. This software shutdown is not a substitute for a hardware emergency stop.

Wait until the motor has stopped before stopping the master:

```sh
cd /root/ethercat-app
EC_IF=eth1 . ./ec.sh stop
```

Do not unplug the EtherCAT cable as a stopping method. The application cannot replace an independent emergency-stop circuit, safety relay, or drive-integrated safety function.

## Expected output

- with only the master running, `ethercat slaves` lists `iRDT_CIA402` in PREOP;
- after starting the cyclic application, the slave changes to OP;
- `/dev/EtherCAT0` exists;
- `ethercat domains -d 0 -v` reports `Domain0: Size 12, WorkingCounter 3/3`;
- the zero-output test shows Controlword `0x0000`, Target velocity `0`, and a six-byte input image containing Statusword plus Position actual value;
- Statusword `0x1221`, after masking with `0x006F`, gives `0x0021` (`Ready to switch on`);
- `link=1` and normally `AL=0x08` are reported in OP;
- during standard enable, CiA402 progresses through Ready to switch on, Switched on, and Operation enabled before the target changes from `0` to `50`;
- actual position feedback updates while the motor moves and the WKC remains complete.

## Troubleshooting

- Missing `ecrt.h`: install the IgH development header or pass `CPPFLAGS=-I...`.
- `cannot find -lethercat`: install the target-architecture library or pass `LDFLAGS=-L...`.
- Missing `/dev/EtherCAT0`: check loaded modules, kernel compatibility, interface binding, and `dmesg`.
- Slave found but configuration fails: compare Vendor ID, Product Code, Revision, Sync Managers, and PDOs `0x1602`/`0x1A02` with the online XML. The TxPDO is `0x6041 + 0x6064`.
- Real-time scheduling or memory locking fails: run as root or configure `CAP_SYS_NICE`, `CAP_IPC_LOCK`, and real-time limits.
- AL state or working counter is unstable: check cabling, power, PDO sizes, cycle time, duplicate cyclic applications, and DC requirements. The program supplies application time but does not explicitly configure distributed clocks.

### The motor rotates but `0x6064` remains zero

```sh
ethercat pdos -p 0
ethercat domains -d 0 -v
ethercat upload -p 0 -t int32 0x6064 0
```

If the four corresponding bytes in the Domain change while the application still prints zero, inspect the master-side PDO offset, bit width, signed conversion, and byte order. If the raw bytes remain zero while local drive control can rotate the motor, inspect whether the STM32 firmware refreshes the position object and whether the MT6701 encoder path is working.

### Repeated `TIMED OUT` or `UNMATCHED` warnings

A few warnings while an application requests or releases the master may be observed. Continuous high-rate warnings are abnormal. Check whether the final OUT port is connected back to a normal port, whether two master/cyclic applications are running, whether SDO or state-changing commands are being issued during the cyclic loop, whether the real-time thread blocks, and whether cabling and slave power are stable.

## Diagnostic commands

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

`ethercat sii_read` produces binary data. Redirect it to a file instead of printing it to the terminal:

```sh
ethercat sii_read -p 0 > /tmp/slave0_sii.bin
```

## Stop or reload the master

Stop or reload the master with:

```sh
cd /root/ethercat-app
EC_IF=eth1 . ./ec.sh stop
```

`stop` terminates this example process and unloads the EtherCAT modules. Ensure no other EtherCAT application is using the master.
