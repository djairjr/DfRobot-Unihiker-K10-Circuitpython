# CircuitPython Port: DFRobot UNIHIKER K10 (ESP32-S3)

**Status**: ✅ Functional — All major hardware features working:
- PMIC (TCA9555 I2C expander: backlight, camera reset, buttons, audio amp)
- Display ILI9341 (240×320, oriented correctly via MADCTL=0x88)
- NeoPixel (single RGB LED)
- Camera GC2145 (2MP, QVGA via espcamera)
- Audio I2S (MAX98357 speaker amp, 440Hz tone)
- Buttons A/B (PMIC-controlled, NOT GPIOs)
- Sensors: SC7A20H (accelerometer), LTR303ALS (light), AHT20 (temp/humidity)
- PSRAM 8MB OPI @ 80MHz ✅
- Flash 16MB DIO @ 80MHz ✅
- USB Serial/JTAG console ✅
- Frozen Python libraries (27 modules pre-loaded)

## Table of Contents
1. [Hardware Overview](#1-hardware-overview)
2. [Environment Setup](#2-environment-setup)
3. [Board Configuration Files](#3-board-configuration-files)
4. [The Display](#4-the-display)
5. [The PMIC (TCA9555)](#5-the-pmic-tca9555)
6. [Pins: What NOT to Map](#6-pins-what-not-to-map)
7. [The Camera (GC2145)](#7-the-camera-gc2145)
8. [Frozen Modules](#8-frozen-modules)
9. [Build System & SDKConfig](#9-build-system--sdkconfig)
10. [Debugging the Boot Failure](#10-debugging-the-boot-failure)
11. [Test Script](#11-test-script)
12. [File Change Log](#12-file-change-log)
13. [Key SDK References](#13-key-sdk-references)

---

## 1. Hardware Overview

| Component | Part | Interface | Notes |
|-----------|------|-----------|-------|
| MCU | ESP32-S3 (Xtensa LX7 dual-core) | — | 240MHz |
| PSRAM | 8MB OPI (Octal PSRAM) | Octal SPI @ 80MHz | Used for camera framebuffers |
| Flash | 16MB Quad SPI | DIO @ 80MHz | Dual I/O mode (unusual!) |
| Display | ILI9341 240×320 | FourWire (SPI) | 8-bit parallel 8080 on schematic, but CP uses SPI via FourWire |
| Camera | GC2145 2MP | Parallel (DVP) + I2C (SCCB) | 8-bit data bus |
| Audio | MAX98357 I2S amp + speaker | I2S | BCLK=GPIO0, LRCLK=GPIO38, DOUT=GPIO45, MCLK=GPIO3 |
| Backlight | PMIC-controlled (TCA9555) | I2C | NOT a GPIO pin! |
| Buttons | 2x (A, B) | PMIC (TCA9555) | NOT GPIOs! Active LOW |
| Accelerometer | SC7A20H | I2C (addr 0x19) | 3-axis |
| Light sensor | LTR303ALS | I2C (addr 0x29) | ALS + UV |
| Temp/Humidity | AHT20 | I2C (addr 0x38) | Requires proper init sequence (see §Sensors) |
| NeoPixel | Single WS2812B | GPIO | GPIO46 |

### I2C Bus Map

| Address | Device | Notes |
|---------|--------|-------|
| 0x11 | Unknown | Always present, function unknown |
| 0x19 | SC7A20H | Accelerometer (WHOAMI=0x11) |
| 0x20 | TCA9555 | PMIC I/O expander (CRITICAL) |
| 0x29 | LTR303ALS | Light sensor |
| 0x38 | AHT20 | Temp/humidity (see §Sensors for init protocol) |

### PMIC (TCA9555) Register Map

**Port 0 (0x02 output / 0x00 input):**
| Bit | Signal | Direction | Notes |
|-----|--------|-----------|-------|
| 0 | eLCD_BLK | OUT | Backlight enable (1=ON) |
| 1 | eCamera_rst | OUT | Camera reset (1=release) |
| 2 | eP11_KeyB | IN | Button B (0=pressed) |
| 3–7 | Other | OUT | Various board functions |

**Port 1 (0x03 output / 0x01 input):**
| Bit | Signal | Direction | Notes |
|-----|--------|-----------|-------|
| 4 | eP5_KeyA | IN | Button A (0=pressed) |
| 7 | eAmp_Gain | OUT | Audio amp enable (1=ON) |

### GPIO Pin Map

| GPIO | Function | CP Pin Name | Notes |
|------|----------|-------------|-------|
| 0 | I2S BCLK | I2S_BCLK | Audio bit clock |
| 1 | — | — | Not mapped |
| 2 | — | — | Do NOT map (conflicts per SDK) |
| 3 | I2S MCLK | I2S_MCLK | Audio master clock |
| 4 | CAM_VSYNC | CAM_VSYNC | Camera vsync |
| 5 | CAM_HREF | CAM_HREF | Camera href |
| 6 | CAM_D7 | CAM_D7 | Camera data bit 7 |
| 7 | CAM_XCLK | CAM_XCLK | Camera external clock |
| 8 | CAM_D0 | CAM_D0 | Camera data bit 0 |
| 9 | CAM_D3 | CAM_D3 | Camera data bit 3 |
| 10 | CAM_D1 | CAM_D1 | Camera data bit 1 |
| 11 | CAM_D2 | CAM_D2 | Camera data bit 2 |
| 12 | TFT SCK | SCK | Display SPI clock |
| 13 | TFT DC | DC | Display data/command |
| 14 | TFT CS | CS | Display chip select |
| 15 | CAM_D5 | CAM_D5 | Camera data bit 5 |
| 16 | CAM_D4 | CAM_D4 | Camera data bit 4 |
| 17 | CAM_PCLK | CAM_PCLK | Camera pixel clock |
| 18 | CAM_D6 | CAM_D6 | Camera data bit 6 |
| 19 | USB_D- | — | Native USB |
| 20 | USB_D+ | — | Native USB |
| 21 | TFT MOSI | MOSI (MIS0 is NC) | Display SPI MOSI |
| 38 | I2S LRCLK | I2S_LRCLK | Audio frame clock |
| 39 | I2S DIN | I2S_DIN | Audio mic input |
| 45 | I2S DOUT | I2S_DOUT | Audio speaker output |
| 46 | NEOPIXEL | NEOPIXEL | On-board NeoPixel |
| 47 | SDA | SDA | I2C data (shared with camera SCCB) |
| 48 | SCL | SCL | I2C clock (shared with camera SCCB) |

---

## 2. Environment Setup

### Prerequisites
- Linux environment (WSL2 works fine)
- Git
- Python 3.12+
- ESP-IDF v6.0 toolchain

### Clone CircuitPython

```bash
git clone https://github.com/adafruit/circuitpython.git
cd circuitpython
git submodule update --init --recursive
```

### ESP-IDF Toolchain

The ESP-IDF v6.0 submodule comes with CircuitPython. Install tools:

```bash
cd ports/espressif
python -m venv ~/.espressif/python_env/idf6.0_py3.12_env
source ~/.espressif/python_env/idf6.0_py3.12_env/bin/activate
pip install --upgrade pip
python esp-idf/tools/idf_tools.py install
python esp-idf/tools/idf_tools.py install-python-env
```

The xtensa compiler will be at:
```
~/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin/
```

### Build Fix: Python Path

**CRITICAL**: The build requires the IDF Python environment's `bin/` in PATH, otherwise cmake uses `/usr/bin/python3` which lacks `kconfgen`:

```bash
export IDF_PYTHON_ENV_PATH=~/.espressif/python_env/idf6.0_py3.12_env
export PATH="$IDF_PYTHON_ENV_PATH/bin:$PATH"
make BOARD=dfrobot_unihiker_k10
```

---

## 3. Board Configuration Files

All board-specific files are in:
```
ports/espressif/boards/dfrobot_unihiker_k10/
```

### 3a. `mpconfigboard.mk` — Build Options

```makefile
USB_VID = 0x303A
USB_PID = 0x1001
USB_MANUFACTURER = "DFRobot"
USB_PRODUCT = "UNIHIKER K10"

IDF_TARGET = esp32s3

CIRCUITPY_ESP_FLASH_SIZE = 16MB
CIRCUITPY_ESP_FLASH_MODE = dio       # ← KEY: DIO, not QIO!
CIRCUITPY_ESP_FLASH_FREQ = 80m

CIRCUITPY_ESP_PSRAM_SIZE = 8MB
CIRCUITPY_ESP_PSRAM_MODE = opi       # Octal PSRAM
CIRCUITPY_ESP_PSRAM_FREQ = 80m

# Frozen modules (Python libraries compiled into firmware)
FROZEN_MPY_DIRS += $(TOP)/frozen/Adafruit_CircuitPython_NeoPixel
FROZEN_MPY_DIRS += $(TOP)/frozen/Adafruit_CircuitPython_ILI9341
FROZEN_MPY_DIRS += $(TOP)/frozen/Adafruit_CircuitPython_AHTx0
FROZEN_MPY_DIRS += $(TOP)/frozen/Adafruit_CircuitPython_SD
FROZEN_MPY_DIRS += $(TOP)/frozen/Adafruit_CircuitPython_Wave
FROZEN_MPY_DIRS += $(TOP)/frozen/Adafruit_CircuitPython_Display_Shapes
FROZEN_MPY_DIRS += $(TOP)/frozen/Adafruit_CircuitPython_Display_Text
FROZEN_MPY_DIRS += $(TOP)/frozen/Adafruit_CircuitPython_BusDevice
FROZEN_MPY_DIRS += $(TOP)/frozen/Adafruit_CircuitPython_Register
FROZEN_MPY_DIRS += $(TOP)/frozen/Adafruit_CircuitPython_SimpleIO
FROZEN_MPY_DIRS += $(TOP)/frozen/adafruit_tca9555
```

### 3b. `mpconfigboard.h` — C Macros

Key settings:
- `MICROPY_HW_BOARD_NAME` and `MICROPY_HW_MCU_NAME` — board identification
- `CIRCUITPY_BOARD_I2C` — defines I2C bus pins (SCL=GPIO48, SDA=GPIO47)
- `CIRCUITPY_BOARD_SPI` — defines SPI bus pins (SCK=GPIO12, MOSI=GPIO21, MISO=GPIO_NONE)
- `CIRCUITPY_BOARD` — enables common board peripheral setup

**IMPORTANT**: Do NOT define `pin_GPIO_NONE`. CircuitPython does not have this pin. Use `mp_const_none` in board.c and remove related entries from pins.c.

### 3c. `board.c` — Display Initialization

The display init sequence specifies the ILI9341 initialization commands including the critical MADCTL value:

```c
static const uint8_t ili9341_init_seq[] = {
    0x01, 0x00,                   // Software Reset
    0x11, 0x00,                   // Sleep Out
    0x36, 0x01, 0x88,             // MADCTL: MY=1, MX=0, BGR=1 → CORRECT!
    0x3A, 0x01, 0x55,             // Pixel Format: 16-bit RGB565
    0x21, 0x00,                   // Display Inversion ON
    0x13, 0x00,                   // Normal Display Mode ON
    0x29, 0x00,                   // Display ON
};
```

The MADCTL value **0x88** was derived from the DFRobot SDK's TFT_eSPI library:
- `ILI9341_Rotation.h` case 2: `writedata(TFT_MAD_MY | TFT_MAD_COLOR_ORDER)`
- `TFT_MAD_MY = 0x80`, `TFT_MAD_COLOR_ORDER = 0x08`
- Result: `0x88` — this fixes BOTH 180° rotation AND mirror flip

Also set rotation=0 when constructing the BusDisplay.

---

## 4. The Display

### Hardware Interface
- **Type**: ILI9341 240×320 TFT
- **Interface**: 8-bit parallel 8080 (per schematic)
- **CircuitPython interface**: FourWire (SPI) — works because the ESP32-S3 SPI controller can drive the display at 40MHz
- **SPI pins**: MOSI=GPIO21, SCK=GPIO12, CS=GPIO14, DC=GPIO13
- **MISO is NC** (no display data output needed)

### MADCTL: The Critical Fix

Getting the display orientation right was the hardest part. The correct MADCTL value was found by analyzing the DFRobot SDK:

1. SDK's `initScreen()` calls `tft.setRotation(_dir)` where `_dir=2`
2. TFT_eSPI's `ILI9341_Rotation.h` for rotation 2:
   ```c
   case 2: writedata(TFT_MAD_MY | TFT_MAD_COLOR_ORDER); break;
   // TFT_MAD_MY = 0x80, TFT_MAD_COLOR_ORDER = 0x08
   // Result: 0x88
   ```
3. This gives **MY=1, MX=0, BGR=1** — correct for the K10's LCD module

### Backlight
- NOT connected to a GPIO pin!
- Controlled by PMIC TCA9555 at 0x20, port 0, bit 0 (eLCD_BLK)
- Active HIGH: write 1 to bit 0, port 0 to turn on
- Must be initialized in Python code (or boot.py) after I2C init

---

## 5. The PMIC (TCA9555)

**This is the key to making the K10 work.** The TCA9555 I2C I/O expander controls:
- Backlight
- Camera power (reset)
- Buttons (A, B)
- Audio amplifier gain

### Initialization Sequence

```python
import busio
i2c = busio.I2C(board.SCL, board.SDA)

def pmic_w(reg, val):
    i2c.writeto(0x20, bytes([reg, val]))

def pmic_r(reg):
    i2c.writeto(0x20, bytes([reg]))
    return i2c.readfrom(0x20, 1)[0]

i2c.try_lock()
pmic_w(0x06, 0b11111010)  # CONFIG0: P0 bit2=IN, rest=OUT
pmic_w(0x07, 0b01101111)  # CONFIG1: P1 bit4=IN, bit7=OUT, rest=IN
pmic_w(0x02, 0b00000011)  # OUTPUT0: backlight=ON, camera_rst=HIGH
pmic_w(0x03, 0b10000000)  # OUTPUT1: amp_gain=ON
i2c.unlock()
```

### Button Reading

Since buttons are PMIC-controlled, read them from the input registers:
```python
p0 = pmic_r(0x00)  # Input port 0
p1 = pmic_r(0x01)  # Input port 1
btn_a = not (p1 & 0x10)  # Port 1, bit 4 (active LOW)
btn_b = not (p0 & 0x04)  # Port 0, bit 2 (active LOW)
```

### Why Buttons Are NOT GPIOs

The original `pins.c` had:
```c
{ MP_ROM_QSTR(MP_QSTR_BUTTON_A), MP_ROM_PTR(&pin_GPIO12) },
{ MP_ROM_QSTR(MP_QSTR_BUTTON_B), MP_ROM_PTR(&pin_GPIO2) },
```

BUT GPIO12 is **display SCK** and GPIO2 is unused. The actual buttons are behind the PMIC. These GPIO entries were removed in the corrected `pins.c`.

---

## 6. Pins: What NOT to Map

### Common Pitfalls

1. **`pin_GPIO_NONE` does not exist!** CircuitPython does not have this pin. Use `mp_const_none` instead. The original board.c and pins.c referenced `&pin_GPIO_NONE` which causes a compilation error.

2. **TFT_RST is NC**: The ILI9341 reset pin is not connected on the K10 (RST=-1). Don't try to map it. Use `mp_const_none` or `NULL` in the code.

3. **SD card pins may not exist**: The schematic may show SD card connections, but the K10 doesn't have a physical SD card slot. Remove or comment out SD_CMD, SD_CLK, SD_D0 pin entries if they cause issues.

4. **Camera data pins are INDIVIDUAL**: Do NOT define a combined tuple like `CAMERA_DATA`. Define individual `CAM_D0` through `CAM_D7` pins and pass them as a list:
   ```python
   data_pins=[board.CAM_D0, board.CAM_D1, board.CAM_D2, board.CAM_D3,
              board.CAM_D4, board.CAM_D5, board.CAM_D6, board.CAM_D7]
   ```

---

## 7. The Camera (GC2145)

### Pin Connections

| Signal | GPIO | CP Pin Name |
|--------|------|-------------|
| CAM_D0 | 8 | CAM_D0 |
| CAM_D1 | 10 | CAM_D1 |
| CAM_D2 | 11 | CAM_D2 |
| CAM_D3 | 9 | CAM_D3 |
| CAM_D4 | 16 | CAM_D4 |
| CAM_D5 | 15 | CAM_D5 |
| CAM_D6 | 18 | CAM_D6 |
| CAM_D7 | 6 | CAM_D7 |
| CAM_PCLK | 17 | CAM_PCLK |
| CAM_VSYNC | 4 | CAM_VSYNC |
| CAM_HREF | 5 | CAM_HREF |
| CAM_XCLK | 7 | CAM_XCLK |
| SCL (SCCB) | 48 | SCL |
| SDA (SCCB) | 47 | SDA |

### Critical: I2C is Shared!

The camera SCCB (I2C) bus SHARES the same physical pins (GPIO47/48) as the main I2C bus for sensors. **Do NOT create a separate I2C object for the camera!** Use the same `busio.I2C(board.SCL, board.SDA)` and pass it to the camera constructor.

### GC2145_SUPPORT Must Be Enabled

This is the most common failure point. The default CircuitPython sdkconfig has:
```config
# CONFIG_GC2145_SUPPORT is not set
```

To fix, add to the board's `sdkconfig` file:
```config
CONFIG_GC2145_SUPPORT=y
```

This enables the GC2145 sensor driver in the `g_sensors[]` array so `camera_probe()` can detect it.

### Camera Power

The camera reset pin (eCamera_rst) is controlled by the PMIC, not a GPIO:
- PMIC port 0, bit 1 = eCamera_rst
- Set HIGH to release camera from reset
- Done via `pmic_w(0x02, 0b00000011)` (bit 0=backlight, bit 1=camera_rst)

There is no separate powerdown pin (pin_pwdn is set to -1/NO_PIN).

### Initialization Order

**IMPORTANT**: Initialize the camera BEFORE the display to avoid potential I2C bus contention ("SCL in use" error):
```python
# 1. Init I2C
i2c = busio.I2C(board.SCL, board.SDA)

# 2. Init PMIC (releases camera reset)
i2c.try_lock()
pmic_w(0x06, 0b11111010)
pmic_w(0x07, 0b01101111)
pmic_w(0x02, 0b00000011)
pmic_w(0x03, 0b10000000)
i2c.unlock()

# 3. Init CAMERA (before display!)
cam = espcamera.Camera(
    data_pins=[...],
    pixel_clock_pin=board.CAM_PCLK,
    vsync_pin=board.CAM_VSYNC,
    href_pin=board.CAM_HREF,
    i2c=i2c,  # ← same I2C object!
    external_clock_pin=board.CAM_XCLK,
    external_clock_frequency=20000000,
    pixel_format=espcamera.PixelFormat.RGB565,
    frame_size=espcamera.FrameSize.QVGA,
    framebuffer_count=2
)

# 4. Init Display
displayio.release_displays()
# ... rest of display init
```

### Camera Reset After Display Init Issue

If the error "SCL in use" appears, it's because:
1. The display initialization (FourWire SPI at 40MHz) has already started
2. The camera SCCB protocol asserts its I2C pins, which conflicts with the already-running display's SPI pins (even though they're different pins, the camera function `assert_pin_free` may check pins that the display system has reserved)

Solution: Initialize camera BEFORE display, or ensure `i2c` is passed to the camera constructor to bypass the SCCB pin claim.

---

## 8. Frozen Modules

CircuitPython can compile Python libraries into the firmware binary so they're available without `circup install`.

### How It Works
1. Place `.py` files in `~/circuitpython/frozen/ModuleName/`
2. Add `FROZEN_MPY_DIRS += $(TOP)/frozen/ModuleName` to `mpconfigboard.mk`
3. The build system compiles them to `.mpy` at build time

### Modules Frozen for K10

| Module | Purpose | Frozen |
|--------|---------|--------|
| `neopixel` | WS2812B LED control | ✅ |
| `adafruit_ili9341` | ILI9341 display driver | ✅ |
| `adafruit_ahtx0` | AHT20 temp/humidity sensor | ✅ |
| `adafruit_sdcard` | SD card library | ✅ |
| `adafruit_wave` | WAV file playback | ✅ |
| `adafruit_display_shapes` | Vector shapes for display | ✅ |
| `adafruit_display_text` | Text rendering for display | ✅ |
| `adafruit_busdevice` | I2C/SPI device helpers | ✅ |
| `adafruit_register` | I2C register helpers | ✅ |
| `simpleio` | Simple I/O helpers | ✅ |
| `adafruit_tca9555` | TCA9555 I2C expander driver | ✅ |

### Important: `adafruit_tca9555` Lock Issue

The frozen `adafruit_tca9555` driver does NOT acquire the I2C bus lock (`try_lock()`/`unlock()`). Using it directly causes `"Function requires lock"` errors. The workaround is to use inline helper functions for TCA9555 operations:

```python
def pmic_w(reg, val):
    """Write PMIC register - manages I2C lock internally"""
    i2c.try_lock()
    i2c.writeto(0x20, bytes([reg, val]))
    i2c.unlock()
```

---

## 9. Build System & SDKConfig

### How SDKConfig Works

1. The Makefile computes `SDKCONFIGS` from `mpconfigboard.mk` settings (flash size, PSRAM, etc.)
2. Default config files are loaded from `esp-idf-config/`:
   - `sdkconfig.defaults` — global defaults
   - `sdkconfig-flash-{size}{-options}.defaults` — flash size
   - `sdkconfig-flash-{mode}.defaults` — flash mode
   - `sdkconfig-psram{-size|-mode|-freq}.defaults` — PSRAM
   - `sdkconfig-{target}.defaults` — chip target
3. **Board's `sdkconfig` is appended to SDKCONFIG_DEFAULTS** by the build system (confirmed in CMakeCache.txt)
4. Cached sdkconfig in `build-{board}/esp-idf/sdkconfig` takes priority on incremental builds
5. After menuconfig, run `make update-board-sdkconfig` to save board-specific settings

### K10 Config Files Selected

For K10, the following default files are loaded:
```
sdkconfig.defaults
sdkconfig-opt.defaults
sdkconfig-flash-16MB.defaults     # 16MB flash
sdkconfig-flash-dio.defaults      # DIO mode
sdkconfig-flash-80m.defaults      # 80MHz
sdkconfig-psram.defaults          # PSRAM enabled
sdkconfig-psram-8MB.defaults      # 8MB PSRAM
sdkconfig-psram-opi.defaults      # Octal PSRAM
sdkconfig-psram-80m.defaults      # 80MHz PSRAM
sdkconfig-ble.defaults            # BLE enabled (ESP32-S3)
sdkconfig-esp32s3.defaults        # ESP32-S3 target
boards/dfrobot_unihiker_k10/sdkconfig  # Board overrides
```

### Board SDKConfig Overrides

File: `boards/dfrobot_unihiker_k10/sdkconfig`

```config
# Debug/Diagnostic settings for K10

# Bootloader log: INFO level
CONFIG_BOOTLOADER_LOG_LEVEL_INFO=y
CONFIG_BOOTLOADER_LOG_LEVEL=3

# USB Serial/JTAG console (critical for serial output!)
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED=y
CONFIG_ESP_CONSOLE_UART_NUM=-1
CONFIG_ESP_CONSOLE_ROM_SERIAL_PORT_NUM=6

# Panic: print and reboot (instead of silent reboot)
CONFIG_ESP_SYSTEM_PANIC_PRINT_REBOOT=y

# PSRAM: continue even if memory test fails
CONFIG_SPIRAM_IGNORE_NOTFOUND=y

# CAMERA: GC2145 sensor support
CONFIG_GC2145_SUPPORT=y
```

### DIO + OPI: A Unique but Valid Combination

The K10 uses **DIO flash mode** with **OPI PSRAM**. This is uncommon among CircuitPython ESP32-S3 boards. The only other board using this combination is the `es3ink` (e-ink display).

This works because:
- Flash is accessed via the SPI flash controller (DIO mode)
- PSRAM is accessed via the Octal SPI controller (OPI mode)
- These are independent peripherals on the ESP32-S3

---

## 10. Debugging the Boot Failure

### The Problem

The initial CP firmware compiled but the board would **not boot** — no serial output, no CIRCUITPY drive, just a black screen.

### Root Cause

The default sdkconfig had:
```config
CONFIG_ESP_CONSOLE_NONE=y         # No console output!
CONFIG_BOOTLOADER_LOG_LEVEL_NONE=y  # No bootloader log!
CONFIG_ESP_SYSTEM_PANIC_SILENT_REBOOT=y  # Silent reboot on error!
```

This meant any boot error was completely invisible. The board would silently crash and reboot in a loop.

### The Fix

Add to the board's `sdkconfig`:
```config
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
CONFIG_BOOTLOADER_LOG_LEVEL_INFO=y
CONFIG_ESP_SYSTEM_PANIC_PRINT_REBOOT=y
CONFIG_SPIRAM_IGNORE_NOTFOUND=y
```

This:
1. Enables the USB Serial/JTAG built-in CDC ACM console for serial output
2. Shows bootloader log messages at INFO level
3. Prints panic information instead of silently rebooting
4. Allows boot even if PSRAM memory test fails (common on prototype hardware)

### Additional Fix: `pin_GPIO_NONE`

The original code referenced `&pin_GPIO_NONE` which doesn't exist in the CP codebase. Fix:
- In `board.c`: Change `&pin_GPIO_NONE` to `mp_const_none` for parameters like `backlight_pin`
- In `pins.c`: Remove any entries using `pin_GPIO_NONE` (TFT_RESET, SD pins)
- In `mpconfigboard.h`: Remove `#define pin_GPIO_NONE` if present

---

## 11. Test Script

A comprehensive test script is included at:
```
code.py
```

Copy it to the CIRCUITPY drive as `code.py`. The script tests:
1. PMIC init (backlight ON, camera reset HIGH, audio amp ON)
2. Camera GC2145 (QVGA RGB565, 2 framebuffers)
3. NeoPixel (green)
4. Display ILI9341 (gradient pattern)
5. Audio I2S (440Hz sine wave, 2 seconds)
6. Buttons A/B (reads via PMIC for 5 seconds)
7. Sensors (SC7A20H, AHT20, LTR303ALS, 0x11)

### Important Notes
- Camera is initialized BEFORE display to avoid I2C contention
- All I2C operations use `try_lock()`/`unlock()` pattern
- No file writes (ESP32-S3 filesystem is read-only when code.py runs)
- No frozen module dependencies for PMIC — uses inline I2C writes

---

## 12. File Change Log

| File | Changes | Reason |
|------|---------|--------|
| `boards/dfrobot_unihiker_k10/board.c` | Changed MADCTL to 0x88, rotation=0, `pin_GPIO_NONE`→`mp_const_none` | Fix display orientation, fix compilation |
| `boards/dfrobot_unihiker_k10/mpconfigboard.h` | Removed `pin_GPIO_NONE` defines, added I2C/SPI pin configs | Fix compilation, add peripheral support |
| `boards/dfrobot_unihiker_k10/mpconfigboard.mk` | Added flash/PSRAM config, frozen module dirs | Enable all hardware features |
| `boards/dfrobot_unihiker_k10/pins.c` | Removed BUTTON_A/B GPIO entries, fixed TFT_RESET, removed SD pins | Buttons are PMIC, not GPIO; fix pin mapping |
| `boards/dfrobot_unihiker_k10/sdkconfig` | Added debug settings + GC2145_SUPPORT=y | Enable serial output, camera support |
| `~/circuitpython/frozen/*/` | 27 module directories with `.py` files | Pre-load common libraries into firmware |

---

## 13. Key SDK References

The DFRobot SDK (`unihiker_k10-0.0.5.zip`, 6378 files) is archived at:
```
~/Unihiker/k10_sdk_0.0.5/
```

Key files:
| File | Information |
|------|-------------|
| `libraries/unihiker_k10/src/unihiker_k10.cpp` | Main initScreen() with setRotation(2) |
| `libraries/TFT_eSPI/TFT_Drivers/ILI9341_Rotation.h` | **MADCTL=0x88** for rotation 2 |
| `libraries/TFT_eSPI/User_Setup.h` | Display pin mapping (MOSI=21, SCLK=12, etc.) |
| `variants/unihiker_k10/pins_arduino.h` | All GPIO assignments |
| `libraries/unihiker_k10/src/unihiker_k10.h` | Class definitions |
| `libraries/unihiker_k10/src/who_lcd.h` | LCD init sequence constants |
| `libraries/unihiker_k10/src/who_button.h` | Button definitions |
| `libraries/unihiker_k10/src/initBoard.h` | Board initialization |

---

## Appendix D: AHT20 Sensor Protocol

**Critical**: The AHT20 does NOT use a command byte for status reads. Direct I2C read returns the status byte.

### Valid Commands (from official datasheet)

| Command | Bytes | Description |
|---------|-------|-------------|
| Soft Reset | `0xBA` | Resets the sensor, takes 20ms |
| Initialize (AHT20) | `0xBE 0x08 0x00` | Calibration command |
| Initialize (AHT10) | `0xE1 0x08 0x00` | Fallback for older sensors |
| Trigger Measurement | `0xAC 0x33 0x00` | Starts measurement, takes 80ms |

### Common Pitfalls

1. **Do NOT send 0x71 before reading!** This is a common mistake. 0x71 is NOT a valid AHT20 command. Sending it causes the device to NACK, resulting in `[Errno 5] Input/output error`.

2. **Status is read directly**, without any preceding command byte:
   ```python
   status = i2c.readfrom(0x38, 1)[0]
   ```
   The AHT20 returns its status byte when addressed for a read. No write needed.

3. **The device must be initialized** before it returns valid measurements:
   ```python
   # Step 1: Read status (direct read, no command)
   status = i2c.readfrom(0x38, 1)[0]

   # Step 2: If not calibrated, send init command
   if not (status & 0x08):
       i2c.writeto(0x38, bytes([0xBE, 0x08, 0x00]))
       time.sleep(0.01)

   # Step 3: Trigger measurement
   i2c.writeto(0x38, bytes([0xAC, 0x33, 0x00]))
   time.sleep(0.08)

   # Step 4: Read 6 bytes (status + humidity_msb + humidity_lsb + humidity_temp + temp_msb + temp_lsb)
   data = bytearray(6)
   i2c.readfrom_into(0x38, data)

   # Step 5: Decode
   humidity_raw = ((data[1] << 12) | (data[2] << 4) | (data[3] >> 4))
   humidity_pct = (humidity_raw * 100) / 0x100000
   temp_raw = ((data[3] & 0x0F) << 16) | (data[4] << 8) | data[5]
   temp_c = (temp_raw * 200.0 / 0x100000) - 50
   ```

4. **Using the frozen library** (`adafruit_ahtx0`) handles all of this automatically:
   ```python
   from adafruit_ahtx0 import AHTx0
   aht = AHTx0(i2c)
   print(f"{aht.temperature:.1f}C {aht.relative_humidity:.1f}%")
   ```

### Status Register

| Bit | Mask | Meaning |
|-----|------|---------|
| 7 | 0x80 | Busy (1=measuring, 0=idle) |
| 3 | 0x08 | Calibrated (1=calibrated, 0=needs init) |

---

## Appendix A: Build Commands Reference

```bash
# Initial setup
cd ~/circuitpython/ports/espressif
export IDF_PYTHON_ENV_PATH=~/.espressif/python_env/idf6.0_py3.12_env
export PATH="$IDF_PYTHON_ENV_PATH/bin:$PATH"

# Build firmware
make BOARD=dfrobot_unihiker_k10

# Clean specific component cache
rm -rf build-dfrobot_unihiker_k10/esp-idf/esp-camera

# Full rebuild
make BOARD=dfrobot_unihiker_k10 clean
make BOARD=dfrobot_unihiker_k10

# Menuconfig (interactive config editor)
make BOARD=dfrobot_unihiker_k10 menuconfig

# Update board sdkconfig from build
make BOARD=dfrobot_unihiker_k10 update-board-sdkconfig
```

## Appendix B: Flashing the Board

### Via UF2 (drag-and-drop)
1. Press and hold BOOT button
2. Connect USB-C to computer
3. Release BOOT button — board appears as a USB drive
4. Drag `firmware.uf2` to the drive
5. Board reboots automatically

### Via Serial (esptool.py)
```bash
esptool.py --chip esp32s3 --port /dev/ttyACM0 write_flash 0x0 firmware.bin
```

## Appendix C: I2C Lock Pattern Reference

All I2C operations from Python code MUST use this pattern:
```python
i2c = busio.I2C(board.SCL, board.SDA)

def safe_i2c_write(addr, data):
    """Write bytes to I2C device with proper lock management"""
    i2c.try_lock()
    try:
        i2c.writeto(addr, bytes(data))
    finally:
        i2c.unlock()

def safe_i2c_read(addr, length):
    """Read bytes from I2C device with proper lock management"""
    i2c.try_lock()
    try:
        i2c.writeto(addr, bytes([0x00]))
        return i2c.readfrom(addr, length)
    finally:
        i2c.unlock()
```


