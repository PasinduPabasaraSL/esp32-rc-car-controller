# ESP32 Joystick - Robust Direction Sender (ESP-NOW)

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)  
[![ESP32](https://img.shields.io/badge/platform-ESP32-orange.svg)]()  
[![Status](https://img.shields.io/badge/status-stable-green.svg)]()

**Robust joystick-to-command transmitter for ESP32.**  
Vector normalization, hysteresis, spike rejection, and ESP-NOW send — fixes asymmetric joystick ranges and eliminates direction chatter.

---

## Table of contents
- [Overview](#overview)  
- [Features](#features)  
- [Hardware](#hardware)  
- [Wiring / Pinout](#wiring--pinout)  
- [Quick start](#quick-start)  
- [Calibration & tuning](#calibration--tuning)  
- [Usage examples](#usage-examples)  
- [Troubleshooting](#troubleshooting)  
- [Contributing](#contributing)  
- [License](#license)

---

## Overview
This project reads an analog joystick on an ESP32, filters and normalizes the X/Y inputs asymmetrically (to account for differing travel to 0 and to ADC max), computes a 2D vector, and maps that vector to one of five commands: `F` (forward), `B` (back), `L` (left), `R` (right) or `S` (stop). Commands are sent over ESP-NOW to a receiver MAC you configure.

The approach solves these problems you likely saw earlier:
- unequal axis travel (center→0 vs center→4095)
- noisy ADC samples and single-sample saturations
- rapid toggling when joystick near boundaries
- blocked loops causing sluggish stop behavior

---

## Features
- Asymmetric normalization so left/right and forward/back map evenly
- Median + moving average smoothing
- Spike rejection for 0/4095 saturations
- Vector magnitude + angle decision (angle sector mapping)
- Magnitude hysteresis (enter/exit thresholds) and angular hysteresis
- Emergency stop via button (flushes buffers, forces `S`)
- ESP-NOW support (single-char command send)

---

## Hardware
Minimum:
- 1 × ESP32 development board  
- 1 × Analog joystick (or two if you have separate sticks)  
- Jumper wires  
- Optional: motor controller + motors on receiver side

Pins used (default):
- `PIN_X` (joystick X) → **GPIO 35 (VRX)**  
- `PIN_Y` (joystick Y) → **GPIO 34 (VRY)**  
- `PIN_SW` (button)    → **GPIO 12** (INPUT_PULLUP)

Receiver MAC (set on transmitter):
```c
uint8_t peerMac[] = { 0x20, 0xE7, 0xC8, 0x68, 0xB8, 0x30 };
```

---

## Wiring / Pinout
- Joystick module: VCC → 3.3V, GND → GND  
- Joystick VRx → GPIO 35  
- Joystick VRy → GPIO 34  
- Joystick SW (button) → GPIO 12 (with internal pull-up in code)  

> Tip: on many joysticks the Y axis may be inverted relative to your expectations. The code normalizes and maps angles so inversion just flips direction labels; if needed swap axis wiring or invert sign.

---

## Quick start

1. Clone repository
```bash
git clone https://github.com/PasinduPabasaraSL/esp32-rc-car-controller.git
cd esp32-rc-car-controller
```

2. Open the sketch in Arduino IDE or PlatformIO:
- Arduino: open `vectormap_joystick_with_espnow.ino`  
- PlatformIO: import the project

3. Update `peerMac` in the top of the transmitter sketch to the receiver ESP32 MAC.

4. Compile and flash to the ESP32:
```bash
# PlatformIO example
pio run -t upload
# or using esptool / Arduino IDE upload
```

5. Open serial monitor (115200) and keep joystick idle while the sketch calibrates center. You’ll see:
```
Calibrating center - keep joystick idle...
CenterX=2980 CenterY=2925
Ready (vector normalization + hysteresis mode)
```

6. Move the joystick and watch `R`, `L`, `F`, `B`, `S` printed and sent over ESP-NOW.

---

## Calibration & tuning

### Automatic center calibration
At startup the sketch samples the stick for a short time to determine `centerX` and `centerY`. Keep the stick idle during that period.

### Important tuning parameters (in code)
- `ENTER_MAG` (default `0.35`) — normalized magnitude required to **start** moving. Increase to require larger push.  
- `EXIT_MAG` (default `0.25`) — magnitude below which the code returns to `S`. Lower gives more sensitivity; higher gives more stability.  
- `ANGLE_HYST` (degrees, default `15`) — prevents flipping near sector borders. Increase to bias chosen direction longer.  
- `AVG_SAMPLES` / `MEDIAN_WINDOW` — smoothing window sizes. Larger windows = smoother but more latency.
- `SPIKE_DELTA` — spike rejection sensitivity for sudden 0/4095 jumps.

### How to tune
1. Increase `ENTER_MAG` if you get false moves.  
2. Increase `EXIT_MAG` a little if stop bounces back to a direction.  
3. For responsiveness reduce `AVG_SAMPLES` and `MEDIAN_WINDOW` but watch noise.  
4. Re-run and test full-range pushes (0 and 4095) to verify normalization.

---

## Usage examples (serial traces)
- Normal idle:
```
x=3025 y=2875 dx=0 dy=0 dxn=0.000 dyn=0.000 mag=0.000 ang=0.0 -> S
```
- Push right:
```
x=3892 y=2999 dx=913 dy=0 dxn=0.818 dyn=0.000 mag=0.818 ang=0.0 -> R
```
- Push forward:
```
x=2143 y=0 dx=-834 dy=-2911 dxn=-0.743 dyn=-1.000 mag=1.255 ang=90.0 -> F
```

---

## Troubleshooting

**Problem: stop didn’t work — car moved shortly after pressing button**  
Solution: ensure you have the version that *flushes buffers on button press*. That forces filters to center and keeps the `S` command until release.

**Problem: always sends `L` or biased direction**  
- Check wiring (swap X wires), ensure calibration run with joystick idle.  
- Verify center values printed at startup; if center looks very off, tighten connections or re-run.  
- Adjust `ENTER_MAG`/`EXIT_MAG`.

**Problem: jitter or flicker at neutral**  
- Increase `EXIT_MAG` or median window; increase `DELAY` slightly.  
- If flicker persists at exact diagonal boundaries increase `ANGLE_HYST`.

---

## Contributing
1. Fork the repo and create a feature branch.  
2. Add tests or serial logs for any behavioral changes.  
3. Create a PR with a clear description, profiling data (if performance-sensitive), and why the change is needed.  
4. Use Conventional Commits for commit messages, e.g. `feat(cal): improve normalization logic`.

Suggested PR template:
```md
### Summary
What changes and why.

### How tested
Steps to reproduce + expected logs.

### Notes
Any tuning recommendations or breaking changes.
```

---

## License
This project is released under the **MIT License**. See [LICENSE](LICENSE).

---

## Acknowledgements
- Original algorithm: asymmetric normalization + vector mapping idea.  
- Inspired by iterative debugging of joystick edge/zero saturation problems.

