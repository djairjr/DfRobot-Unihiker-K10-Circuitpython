
# SPDX-FileCopyrightText: 2026 Unihiker K10 Contributors
#
# SPDX-License-Identifier: MIT

"""
`sc7a20h`
================================================================================

CircuitPython driver for the SC7A20H 3-axis accelerometer (Silan Microelectronics).
Register-compatible with STMicroelectronics LIS2DH12 / LIS3DH.

I2C address: 0x19 (default, ADDR=GND on Unihiker K10)
WHO_AM_I:    0x11

Reference: https://github.com/mydazy/esp_sc7a20h (ESP-IDF driver)
           https://github.com/electronut/Electronutlabs_CircuitPython_LIS2DH12

* Author(s): Unihiker K10 contributors
"""

import time
import struct

from micropython import const

try:
    from typing import Tuple
except ImportError:
    pass

from adafruit_bus_device.i2c_device import I2CDevice

__version__ = "0.1.0"
__repo__ = "https://github.com/mydazy/esp_sc7a20h"

# ── Register addresses (LIS2DH12-compatible) ────────────────────────────
_REG_WHO_AM_I    = const(0x0F)
_REG_CTRL1       = const(0x20)   # ODR[7:4], LP[3], Zen[2], Yen[1], Xen[0]
_REG_CTRL2       = const(0x21)   # High-pass filter
_REG_CTRL3       = const(0x22)   # INT1 routing
_REG_CTRL4       = const(0x23)   # BDU[7], FS[5:4], HR[3], ST[1:0]
_REG_CTRL5       = const(0x24)   # FIFO / latch
_REG_CTRL6       = const(0x25)   # INT2 / polarity
_REG_STATUS      = const(0x27)
_REG_OUT_X_L     = const(0x28)   # Auto-increment burst: 6 bytes (X, Y, Z)

# ── Constants ───────────────────────────────────────────────────────────
_DEVICE_ID       = const(0x11)   # SC7A20H expected WHO_AM_I

# CTRL_REG1: axis enable bits
_AXES_ENABLE     = const(0x07)   # Zen=1, Yen=1, Xen=1

# CTRL_REG4 masks
_BDU             = const(0x80)   # Block Data Update
_FS_MASK         = const(0x30)   # Full-scale selection bits [5:4]
_FS_2G           = const(0x00)
_FS_4G           = const(0x10)
_FS_8G           = const(0x20)
_FS_16G          = const(0x30)
_HR_MASK         = const(0x08)   # High-resolution enable

# Sensitivity in mg/LSb for 12-bit data (normal mode)
# From mydazy/esp_sc7a20h driver
_SENSITIVITY_MG  = {
    2:  1.0,
    4:  2.0,
    8:  4.0,
    16: 12.0,
}

# ODR values for CTRL_REG1 (bits [7:4])
_ODR_VALUES = {  # (register_bits, min_delay_s)
    'power_down': (0x00, 0.0),
    '1_hz':       (0x10, 1.0),
    '10_hz':      (0x20, 0.1),
    '25_hz':      (0x30, 0.04),
    '50_hz':      (0x40, 0.02),
    '100_hz':     (0x50, 0.01),
    '200_hz':     (0x60, 0.005),
    '400_hz':     (0x70, 0.0025),
}


class SC7A20H:
    """Driver for the SC7A20H 3-axis accelerometer.

    :param ~busio.I2C i2c_bus: The I2C bus the sensor is connected to.
    :param int address: The I2C device address. Default is :const:`0x19`.
    :param int range: Full-scale range in g. One of 2, 4, 8, 16.
                      Default :const:`2` (most sensitive, ±2g).
    :param str data_rate: Output data rate. One of ``'power_down'``,
                          ``'1_hz'``, ``'10_hz'``, ``'25_hz'``, ``'50_hz'``,
                          ``'100_hz'``, ``'200_hz'``, ``'400_hz'``.
                          Default ``'100_hz'``.

    Quickstart::

        import board
        import busio
        from sc7a20h import SC7A20H

        i2c = busio.I2C(board.SCL, board.SDA)
        accel = SC7A20H(i2c)

        while True:
            x, y, z = accel.acceleration
            print(f"{x:+.2f} {y:+.2f} {z:+.2f} m/s²")
    """

    def __init__(
        self,
        i2c_bus,
        address: int = 0x19,
        range: int = 2,
        data_rate: str = "100_hz",
    ):
        self._device = I2CDevice(i2c_bus, address)
        self._buf = bytearray(6)

        if range not in _SENSITIVITY_MG:
            raise ValueError(f"range must be one of {list(_SENSITIVITY_MG.keys())}")
        if data_rate not in _ODR_VALUES:
            raise ValueError(f"data_rate must be one of {list(_ODR_VALUES.keys())}")

        # ── Verify WHO_AM_I ────────────────────────────────────────────
        whoami = self._read_reg(_REG_WHO_AM_I)
        if whoami != _DEVICE_ID:
            raise RuntimeError(
                f"SC7A20H not found at 0x{address:02X}: "
                f"WHO_AM_I=0x{whoami:02X} (expected 0x{_DEVICE_ID:02X})"
            )

        # ── Power-down before reconfiguring (safe start) ──────────────
        self._write_reg(_REG_CTRL1, 0x00)
        time.sleep(0.01)

        # ── CTRL_REG4: BDU + full-scale + high-resolution ────────────
        self._range = range
        fs_bits = {2: _FS_2G, 4: _FS_4G, 8: _FS_8G, 16: _FS_16G}
        self._write_reg(_REG_CTRL4, _BDU | fs_bits[range])

        # ── CTRL_REG2: enable high-pass filter for INT1 ──────────────
        self._write_reg(_REG_CTRL2, 0x01)

        # ── CTRL_REG5: latch INT1 ────────────────────────────────────
        self._write_reg(_REG_CTRL5, 0x08)

        # ── CTRL_REG1: set data rate + enable all axes ───────────────
        self._data_rate = data_rate
        odr_bits = _ODR_VALUES[data_rate][0]
        self._write_reg(_REG_CTRL1, odr_bits | _AXES_ENABLE)
        time.sleep(_ODR_VALUES[data_rate][1])

    # ── Low-level helpers ──────────────────────────────────────────────

    def _read_reg(self, reg: int) -> int:
        """Read a single 8-bit register."""
        with self._device as dev:
            dev.write(bytes([reg & 0xFF]))
            dev.readinto(self._buf, end=1)
        return self._buf[0]

    def _write_reg(self, reg: int, value: int) -> None:
        """Write a single 8-bit register."""
        with self._device as dev:
            dev.write(bytes([reg & 0xFF, value & 0xFF]))

    # ── Public properties ──────────────────────────────────────────────

    @property
    def acceleration(self) -> Tuple[float, float, float]:
        """The measured acceleration in m/s² as a tuple ``(x, y, z)``."""
        with self._device as dev:
            dev.write(bytes([_REG_OUT_X_L | 0x80]))  # MSB=1 → auto-increment
            dev.readinto(self._buf, end=6)

        # 16-bit left-aligned → shift right by 4 → signed 12-bit
        raw_x = struct.unpack_from("<h", self._buf, 0)[0] >> 4
        raw_y = struct.unpack_from("<h", self._buf, 2)[0] >> 4
        raw_z = struct.unpack_from("<h", self._buf, 4)[0] >> 4

        mg_per_lsb = _SENSITIVITY_MG[self._range]
        g_to_ms2 = 9.806 / 1000.0  # Convert mg → m/s²
        scale = mg_per_lsb * g_to_ms2

        return (raw_x * scale, raw_y * scale, raw_z * scale)

    @property
    def acceleration_mg(self) -> Tuple[float, float, float]:
        """The measured acceleration in milli-g (mg) as a tuple ``(x, y, z)``.

        Useful for integer-based analysis: at ±2g, 1 LSb = 1 mg.
        """
        with self._device as dev:
            dev.write(bytes([_REG_OUT_X_L | 0x80]))
            dev.readinto(self._buf, end=6)

        raw_x = struct.unpack_from("<h", self._buf, 0)[0] >> 4
        raw_y = struct.unpack_from("<h", self._buf, 2)[0] >> 4
        raw_z = struct.unpack_from("<h", self._buf, 4)[0] >> 4

        mg_per_lsb = _SENSITIVITY_MG[self._range]
        return (raw_x * mg_per_lsb, raw_y * mg_per_lsb, raw_z * mg_per_lsb)

    @property
    def who_am_i(self) -> int:
        """The WHO_AM_I register value. Should be ``0x11``."""
        return self._read_reg(_REG_WHO_AM_I)

    @property
    def temperature(self) -> float:
        """Internal temperature in degrees Celsius (approximate)."""
        # LIS2DH12-compatible: reading from 0x0C, 0x0D
        # Not all SC7A20H devices implement this
        with self._device as dev:
            dev.write(bytes([0x0C | 0x80]))  # auto-increment from TEMP_L
            dev.readinto(self._buf, end=2)
        raw = struct.unpack_from("<h", self._buf, 0)[0] >> 4
        # From LIS2DH12 datasheet: temp = (raw / 8) + 25
        return (raw / 8.0) + 25.0

    @property
    def data_rate(self) -> str:
        """The current output data rate setting."""
        return self._data_rate

    @data_rate.setter
    def data_rate(self, rate: str) -> None:
        if rate not in _ODR_VALUES:
            raise ValueError(f"data_rate must be one of {list(_ODR_VALUES.keys())}")
        odr_bits = _ODR_VALUES[rate][0]
        current = self._read_reg(_REG_CTRL1)
        current &= 0x0F  # keep axis enable bits
        self._write_reg(_REG_CTRL1, odr_bits | current)
        time.sleep(_ODR_VALUES[rate][1])
        self._data_rate = rate

    @property
    def range(self) -> int:
        """The full-scale range in g (2, 4, 8, or 16)."""
        return self._range

    @range.setter
    def range(self, range_g: int) -> None:
        if range_g not in _SENSITIVITY_MG:
            raise ValueError(f"range must be one of {list(_SENSITIVITY_MG.keys())}")
        fs_bits = {2: _FS_2G, 4: _FS_4G, 8: _FS_8G, 16: _FS_16G}
        current = self._read_reg(_REG_CTRL4)
        current &= ~_FS_MASK
        self._write_reg(_REG_CTRL4, current | fs_bits[range_g] | _BDU)
        self._range = range_g

    @property
    def status(self) -> int:
        """The STATUS register byte.

        Bit 3: ZYXOR (data overrun)
        Bit 2: ZOR, YOR, XOR
        Bit 0: ZYXDA (new data available)
        """
        return self._read_reg(_REG_STATUS)
