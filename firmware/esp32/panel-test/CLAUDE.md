# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A bare-metal Arduino/PlatformIO reference firmware for an ESP32-WROOM-32 that drives the HU-058D clock's display (two AiP33628 matrix drivers, 33 common-anode RGB LEDs) directly, with no ESPHome. It exists to validate the display map on real hardware and as the minimal porting reference for other MCUs. The whole program is `src/main.cpp`.

It lives inside a larger repo (root: `../../..`):
- `docs/display-map.md` — authoritative COM/SEG → segment map, scan timing, color and current notes. `main.cpp`'s tables must agree with it.
- `docs/aip33628-protocol.md` — 30-bit frame format, latching and timing rules for the driver chip.
- `docs/wiring.md`, `docs/hardware.md` — socket pinout, board variants (HU-058D vs HU-058/SE move the CLK socket pin from 14 to 16).
- `firmware/esphome/` — the production ESPHome firmware; `components/aip33628/` is a custom component implementing the same protocol with 4 binary-weighted sub-frames (16 levels per channel) instead of this firmware's 3 levels.

A change to the map or protocol here usually means the docs and the ESPHome component need the same change.

## Commands

Two independent builds of the same `src/main.cpp`. Keep both working; don't move or rename files to suit one of them.

PlatformIO, single env `esp32dev` (PlatformIO is not currently installed on this machine):

```
pio run                 # build
pio run -t upload       # build and flash
pio device monitor      # serial at 115200
```

ESP-IDF v6.1 (installed via EIM), with arduino-esp32 3.3.12 as a managed component:

```
source ~/.espressif/tools/activate_idf_v6.1.sh
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

ESP-IDF layout: top-level `CMakeLists.txt`, `main/CMakeLists.txt` compiles `../src/main.cpp`, `main/idf_component.yml` pins the Arduino component, and `sdkconfig.defaults` carries what it needs (`FREERTOS_HZ=1000`, `AUTOSTART_ARDUINO`, 240 MHz CPU and no core-1 idle task watchdog, both to match the Arduino core; `loop()` never blocks, so with IDF's default watchdog it logs `task_wdt` errors every 5 s). Things that were needed to build cleanly under IDF:
- `ARDUINO_SELECTIVE_COMPILATION` with every optional Arduino library off. Some of them (ESP_I2S, NetworkClientSecure) fail under IDF 6.1's `-Werror`. If code ever needs one (e.g. WiFi), enable just that one.
- `main.cpp` includes `soc/gpio_struct.h` under `#ifndef PLATFORMIO` (the PlatformIO core provides `GPIO` through `Arduino.h`; the IDF component doesn't). IDF builds with `-Werror`, so avoid `++`/`--`/compound assignment on `volatile` variables (C++20 deprecation).
- `src/main.cpp` has CRLF line endings; preserve them when editing.
- After editing `sdkconfig.defaults`, delete the generated `sdkconfig` so the defaults are applied again.

There are no tests or linters. Verification is on hardware via single-key serial commands (`?` lists them): `l` lamp test, `t` 2:12 AM, `d` 1234 multicolor, `6` week display, `w`/`n` walk one LED position at a time (prints driver/COM/LED), `+`/`-` adjust global current IS.

## Architecture of main.cpp

Data flows in three layers:

1. **Logical level array** `g_level[driver][com pair][led 1..5][channel]`, values 0/1/2 (off/half/full). Patterns write here via `setPos`/`setDigit`/`setColon` etc., which translate block + segment through the `BLOCKS` and `SEGMAP` tables (from `docs/display-map.md`).
2. **`render()`** collapses levels into per-driver, per-COM-pair 16-bit SEG words in `g_ss`, using one sub-frame, or two when any channel in that COM pair is at half duty (`g_nsub`). LED *n* occupies SEG `3n-2..3n` in blue, green, red order; SEG0 is unwired (only driven to mimic the stock lamp test). Writes to `g_ss`/`g_nsub` are guarded with `noInterrupts()`.
3. **`scanTick()`** is a periodic `esp_timer` (301 µs, 8 ticks per ~2.4 ms cycle ≈ 415 Hz, matching the stock 8051). Each COM pair gets two ticks; a pair with one sub-frame is not re-sent on its second tick and stays latched. It's a timer, not a busy task, deliberately, to avoid starving the Arduino loop's core.

`sendFrame()` is the only hardware-dependent code: bit-banged via `GPIO.out_w1ts/w1tc`, 30 bits LSB-first (SS[15:0], CS[7:0], IS[3:0], 2 reserved zeros), data changes only while CLK is low, and the latch is a DATA rising edge while CLK is held high after bit 29. Porting to another MCU means replacing those GPIO writes and the pin constants.

Driver 1 (`PIN_CLK`/`PIN_DATA`, GPIO22/21) carries the hour digits; driver 2 (`PIN_CLK_1`/`PIN_DATA_1`, GPIO19/18) the minute digits.

## Hardware cautions to respect in code

- Current is constant per lit sink (`IS` 0x0 = 2.5 mA … 0xF = 40.4 mA) and does not drop with scan duty. Board rating is ~250 mA; all-red at IS 0xF is ~485 mA and heats the drivers. Keep defaults for heavy patterns low (lamp test uses 0x5).
- The AiP33628 ignores input for the first ~200 µs after power-up; `setup()` waits before the first frame.
