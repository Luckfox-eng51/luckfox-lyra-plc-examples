# RS485 Temperature and Humidity Reader (C)

[简体中文](README.md) | **English**

Use [temperature_humidity.c](src/temperature_humidity.c) on a Luckfox Lyra PLC to read a Modbus RTU temperature/humidity sensor and display temperature and relative humidity. It supports function codes `03` / `04` and does not write sensor registers. For other data types, use the [generic sensor reader](../sensor-read/README_EN.md).

## 1. Wiring and Power

Prepare a working Luckfox Lyra PLC, an RS485 Modbus RTU sensor, and a DC supply suitable for each device's rated input. Use SSH from the computer to upload the source.

The example temperature/humidity sensor is shown below. Viewed from the front, its terminals are **`+`, `−`, `A+`, `B−` from left to right**. The label specifies **DC 5–30V**. The `+` / `−` terminals are called `DC+` / `DC−` in the table.

![Temperature/humidity sensor and terminal labels](images/temperature-humidity-sensor.webp)

![PLC, DC supply, and RS485 sensor wiring](images/sensor-wiring.webp)

[Open the SVG wiring diagram](images/sensor-wiring.svg). The example sensor accepts **DC 5–30V** and uses a **12V supply**. The diagram shows the PLC and a non-isolated sensor sharing that supply. This is an electrical connection map; branch locations do not represent physical terminal positions. Follow the labels on your devices.

Disconnect power before wiring:

| Supply or PLC terminal | Connect to |
| --- | --- |
| 12V supply `DC+` | PLC power input positive and sensor `+` |
| 12V supply `DC−` | PLC power input negative and sensor `−` |
| PLC pin 10, `TA / A+` | Sensor `A` / `A+` |
| PLC pin 9, `TB / B−` | Sensor `B` / `B−` |
| PLC pin 6, `SGND` | Sensor RS485 signal ground; usually `DC−` on a non-isolated four-wire sensor |

The PLC accepts **7–24V** at its DC input and this sensor accepts **5–30V**, so both can share a **12V supply**. If you change sensors, choose a supply that matches the new sensor's rated voltage.

A shared supply already connects the power negatives. Do not assume the PLC power negative is its RS485 signal ground; use `SGND` for the bus reference as shown above. For an isolated sensor, follow the manufacturer's bus-side signal-ground definition and do not bridge the isolation with power ground. `PE` is protective earth and does not replace `SGND`. Leave RS422 pins 7 and 8 unconnected.

Use this pinout to locate the PLC terminals:

![PLC RS485 terminal numbers](images/plc-rs485-pinout.webp)

> The switches control termination only; they do not select RS485 or RS422 mode. Enabling both switches does not provide another serial port. Before changing to RS422, power off and configure the wiring and termination for the RS422 connection.

The program uses `/dev/ttyS2` and controls TXEN through `/dev/gpiochip0`, line `15` (`GPIO0_B7`), switching back to receive after transmission. Close any UART2 Web feature, serial terminal, or other application using the port before running it.

## 2. Check the Sensor Settings

Check the sensor manual first. These are the program's defaults, **not a universal register map for temperature/humidity sensors**. It does not detect sensor models or register definitions.

| Item | Default |
| --- | --- |
| Serial format | `9600`, 8 data bits, no parity, 1 stop bit (8N1) |
| Slave address / function | `1` / `03` (holding registers) |
| Temperature | Protocol address `0`, signed 16-bit two's complement, multiplied by `0.1` for °C |
| Humidity | Protocol address `1`, unsigned 16-bit, multiplied by `0.1` for %RH |

Use the protocol address carried in Modbus frames, in decimal or `0x` hexadecimal. If the manual maps reference `40001` to protocol address `0`, pass `0`, not `40001`. Follow the manufacturer's address definition.

## 3. Upload to the PLC and Build

**On the Ubuntu computer**, start at the repository root and enter this example. Replace `PLC_IP` with the PLC's actual IP address and adjust the username for your system.

```bash
cd protocols/modbus/rtu/c/temperature-humidity
ssh lyra@PLC_IP 'mkdir -p ~/temperature-humidity-example/src'
scp src/temperature_humidity.c lyra@PLC_IP:temperature-humidity-example/src/
ssh lyra@PLC_IP
```

**After logging in to the PLC**, compile:

```bash
cd ~/temperature-humidity-example
gcc -std=c11 -O2 -Wall -Wextra src/temperature_humidity.c -o temperature_humidity_plc
```

Run the generated `temperature_humidity_plc` on the PLC. If GCC is missing, install it first on an APT-based image:

```bash
sudo apt update
sudo apt install -y build-essential
```

The program needs only Linux system interfaces and the C standard library, with no Modbus library dependency. Source comments and program output are in English.

## 4. Read on the PLC

If the sensor matches the defaults above, read once first:

```bash
sudo ./temperature_humidity_plc --once
```

Read continuously once per second; press `Ctrl+C` to stop:

```bash
sudo ./temperature_humidity_plc -n 0 -i 1000
```

Read three times and include raw values:

```bash
sudo ./temperature_humidity_plc -n 3 --raw
```

Example output (`[time]` represents the actual timestamp; readings vary with the environment):

```text
[time] Temperature: 27.6 °C  Humidity: 59.0 %RH  [Raw: temperature=276, humidity=590]
Summary: successful=3, failed=0.
```

For a finite run, `successful` matching the requested count and `failed=0` indicate successful reads. Confirm the units and interpretation against the sensor register table.

## 5. Options and Baud Rate

Show all options with `./temperature_humidity_plc --help`.

| Option | Default | Purpose |
| --- | --- | --- |
| `-d PATH` | `/dev/ttyS2` | Serial device |
| `-b N` | `9600` | Baud rate; must match the sensor |
| `-a N` | `1` | Modbus slave address, 1–247 |
| `-f 3\|4` | `3` | Read holding / input registers |
| `--parity N\|E\|O` | `N` | None / even / odd parity |
| `--stop-bits 1\|2` | `1` | Stop bits |
| `--temperature-register N` | `0` | Temperature protocol address |
| `--humidity-register N` | `1` | Humidity protocol address |
| `--temperature-scale N` / `--humidity-scale N` | `0.1` | Raw-value scale factors |
| `--temperature-format FORMAT` | `signed` | `signed` two's complement, `unsigned`, or `sign-magnitude` with the high bit as sign |
| `--precision N` | `1` | Decimal places, 0–4 |
| `--once` / `-n N` | Continuous | Read once / a specified count; `-n 0` is continuous |
| `-i MS` / `-t MS` | `1000` / `1000` | Minimum interval between sample starts / timeout per request, in milliseconds |
| `--raw` / `-v` | Off | Raw register values / TX and RX frames |
| `--gpiochip PATH` / `--txen-line N` | `/dev/gpiochip0` / `15` | PLC TXEN control; normally keep the defaults |
| `--auto-direction` | Off | Use with a USB adapter that controls direction automatically |

**Change `-b` to select another baud rate; no source edit or rebuild is needed.** Supported rates are `1200`, `2400`, `4800`, `9600`, `19200`, `38400`, `57600`, and `115200`, subject to sensor and serial-port support. This option does not change the sensor's stored settings.

For example, if the manual specifies 19200, 8E1, slave 2, function 04, temperature at address 10, humidity at address 11, and both scale factors as 0.01:

```bash
sudo ./temperature_humidity_plc -b 19200 --parity E -a 2 -f 4 \
  --temperature-register 10 --humidity-register 11 \
  --temperature-scale 0.01 --humidity-scale 0.01 --once
```

## 6. Optional: Read from a Computer with USB to RS485

Connect sensor A/B/signal ground to a USB to RS485 adapter with automatic direction control, then plug it into the computer. Power the sensor separately at its rated voltage. Disconnect the PLC's A/B during this test so two masters do not send requests on the same bus.

Run this before and after inserting the adapter. The new tty device is its serial port:

```bash
ls /dev/tty*
```

The commands below use `/dev/ttyACM0`; yours may be `/dev/ttyUSB0` or another name. Substitute the device you detected. Run in this example's directory on the computer:

```bash
sudo apt update
sudo apt install -y build-essential acl
sudo setfacl -m "u:$(id -un):rw" /dev/ttyACM0
gcc -std=c11 -O2 -Wall -Wextra src/temperature_humidity.c -o temperature_humidity_pc
./temperature_humidity_pc -d /dev/ttyACM0 --auto-direction --once
```

Check the device name and grant access again after reconnecting USB. Build and run `_pc` on the computer and `_plc` on the PLC; these binaries are not interchangeable.

## 7. Makefile and Troubleshooting

With the complete example directory on the machine, use `make build` on the computer or `make build PLATFORM=plc` on the PLC. `PLATFORM` only selects the output name; it does not cross-compile. `make check` checks compiler warnings, `make run PLATFORM=plc ARGS="--help"` demonstrates the run target, and `make clean` removes generated binaries. Use the earlier `sudo` run commands to access the PLC hardware.

| Symptom | Action |
| --- | --- |
| `Permission denied` | Use `sudo` on the PLC; grant serial access again on the computer |
| Serial port or GPIO busy | Close Web features and other programs using UART2 / GPIO15 |
| Timeout or CRC errors | Check power, A/B/SGND, baud rate, parity, and slave address; add `-v` to inspect frames |
| `Modbus exception` | Check the function code, protocol register address, and register count against the manual |
| Incorrect values or sign | Check temperature/humidity register addresses, temperature encoding, and scale |
| `Exec format error` | Recompile the source on the machine where it will run |

Exit codes: `0` all reads succeeded; `1` read or decode failure; `2` argument or device setup error; `130` interrupted. On exit, the program releases TXEN and restores serial settings.
