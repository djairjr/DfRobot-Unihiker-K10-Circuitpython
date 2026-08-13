# Summary of Changes

## Files Edited in Board Directory

1. **board.c** - Display init sequence (MADCTL=0x88), rotation=0, removed pin_GPIO_NONE references
2. **mpconfigboard.h** - I2C/SPI pin configs, removed pin_GPIO_NONE defines  
3. **mpconfigboard.mk** - Flash/PSRAM settings, frozen modules (27 libs)
4. **pins.c** - Removed BUTTON_A/B (are PMIC-controlled), fixed TFT_RESET, removed SD pins
5. **sdkconfig** - Debug settings (USB console, boot log, panic, PSRAM ignore), GC2145_SUPPORT=y

## Files NOT Edited (CP Core)

- **common-hal/espcamera/Camera.c** - Only inspected; confirmed `pin_sccb_sda=-1` and `pin_sccb_scl=-1` are already set
- **shared-bindings/microcontroller/Pin.c** - Only inspected for assert_pin_free behavior
- **esp-idf-config/sdkconfig.defaults** - Has `# CONFIG_GC2145_SUPPORT is not set` (kept as-is; board override works)

## Frozen Modules Added

27 module directories in ~/circuitpython/frozen/ with .py files compiled into firmware.

## Key Discoveries

1. **DIO + OPI combination works** - K10 uses DIO flash + OPI PSRAM (unique but valid, confirmed by es3ink board)
2. **Buttons are PMIC, not GPIO** - BUTTON_A = PMIC P1 bit 4, BUTTON_B = PMIC P0 bit 2
3. **Backlight is PMIC, not GPIO** - eLCD_BLK = PMIC P0 bit 0
4. **Camera I2C shares sensor I2C bus** - Same GPIO47/48 pins, same busio.I2C object
5. **MADCTL=0x88** from SDK's TFT_eSPI ILI9341_Rotation.h case 2
6. **ESP32-S3 filesystem is read-only** during code.py execution - no file logging
7. **I2C requires lock** - try_lock()/unlock() pattern needed for all I2C ops

## Correction: AHT20 "I/O Error" Root Cause

**Previous assumption**: AHT20 hardware was absent or faulty.
**Actual cause**: Test script sent command byte `0x71` which is NOT a valid AHT20 command!

The AHT20 datasheet defines only 4 commands:
- `0xBA` (Soft Reset), `0xBE` (Init AHT20), `0xE1` (Init AHT10), `0xAC` (Trigger Measure)

The status register is read via **direct I2C read** (no command byte):
```python
status = i2c.readfrom(0x38, 1)[0]  # Correct!
# NOT: i2c.writeto(0x38, bytes([0x71]))  # WRONG!
```

This was fixed in the test script and documented in README.md Appendix D.
