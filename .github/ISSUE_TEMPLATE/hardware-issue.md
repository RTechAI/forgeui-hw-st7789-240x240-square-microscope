---
name: MicroScope hardware or instrument issue
about: Report a ForgeUI MicroScope hardware, control, build, or instrument-rendering problem
title: "[MicroScope] "
labels: hardware
assignees: ""
---

## Hardware

- ESP32 model and board:
- ST7789 display/controller:
- PCB or module marking:

## Display wiring

| Display signal | Connected pin / voltage |
| --- | --- |
| GND | |
| VCC | |
| SCL / SCLK | |
| SDA / MOSI | |
| RES / RST | |
| DC | |
| CS | |
| BLK | |

State whether MISO is connected. For the tested 1.54-inch square module, BLK is wired to 3.3V.

## Joystick wiring and calibration

| Joystick signal | Connected pin / voltage |
| --- | --- |
| SW | |
| VRy | |
| VRx | |
| Supply | |
| GND | |

- Observed centre calibration values (`Joystick centre X=... Y=...`):

## Current instrument behaviour

- Current mode (SCOPE / SPECTRUM / XY):
- SCOPE waveform type:
- SCOPE trace behaviour:
- Volts/div behaviour:
- Time/div behaviour:
- Trigger-level and trigger-position display:
- Simulated measurement readouts (frequency / Vpp / RMS):
- SPECTRUM rendering:
- Peak-hold behaviour:
- XY/Lissajous rendering:
- XY phase/ratio behaviour:
- Mode-switching behaviour:

## Software and results

- PlatformIO version:
- `espressif32` platform version:
- Arduino_GFX version:
- Build: PASS / FAIL
- Flash: PASS / FAIL

## Evidence

Attach a physical photo of the board, display, joystick, and wiring. Include short relevant build, upload, or serial logs; remove unrelated output and secrets.
