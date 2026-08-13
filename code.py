import board
import busio
import time
import displayio
import espcamera
from neopixel import NeoPixel
from adafruit_display_shapes.rect import Rect
from adafruit_display_text import label
import terminalio

def pmic_write(i2c, reg, val):
    i2c.writeto(0x20, bytes([reg, val]))

i2c = busio.I2C(board.SCL, board.SDA)
i2c.try_lock()
try:
    pmic_write(i2c, 0x06, 0b11111010)
    pmic_write(i2c, 0x07, 0b01101111)
    pmic_write(i2c, 0x02, 0b00000011)
    pmic_write(i2c, 0x03, 0b10000000)
except:
    pass
finally:
    i2c.unlock()

np = NeoPixel(board.NEOPIXEL, 1, brightness=0.1)
np[0] = (0, 0, 10)

display = board.DISPLAY
print(f"DISPLAY: {display.width}x{display.height}")

cam = espcamera.Camera(
    data_pins=[board.CAM_D0, board.CAM_D1, board.CAM_D2, board.CAM_D3,
               board.CAM_D4, board.CAM_D5, board.CAM_D6, board.CAM_D7],
    pixel_clock_pin=board.CAM_PCLK,
    vsync_pin=board.CAM_VSYNC,
    href_pin=board.CAM_HREF,
    i2c=i2c,
    external_clock_pin=board.CAM_XCLK,
    external_clock_frequency=20000000,
    pixel_format=espcamera.PixelFormat.RGB565,
    frame_size=espcamera.FrameSize.R240X240,
    framebuffer_count=2)
cam.vflip = True
cam.hmirror = False
print(f"CAMERA: {cam.sensor_name}  {cam.width}x{cam.height}")
np[0] = (0, 20, 0)

frame = cam.take(1)
camera_tg = displayio.TileGrid(
    frame,
    pixel_shader=displayio.ColorConverter(input_colorspace=displayio.Colorspace.RGB565_SWAPPED),
    x=0, y=40
)
group = displayio.Group()
rect_top = Rect(0, 0, 240, 40, fill=0x000000)
rect_bot = Rect(0, 280, 240, 40, fill=0x000000)
group.append(camera_tg)
group.append(rect_top)
group.append(rect_bot)

FONT = terminalio.FONT
lbl_title = label.Label(FONT, text="K10 Monitor", color=0xFFFF)
lbl_title.x = 5
lbl_title.y = 12
group.append(lbl_title)

lbl_temp = label.Label(FONT, text="T:--.-C        ", color=0xFFE0)
lbl_temp.x = 5
lbl_temp.y = 288
group.append(lbl_temp)

lbl_humi = label.Label(FONT, text="RH:--.-%       ", color=0x07FF)
lbl_humi.x = 5
lbl_humi.y = 300
group.append(lbl_humi)

lbl_lux = label.Label(FONT, text="Lux:-----      ", color=0x0F0F)
lbl_lux.x = 125
lbl_lux.y = 288
group.append(lbl_lux)

lbl_accel = label.Label(FONT, text="X:--.-- Y:--.--", color=0xF0F0)
lbl_accel.x = 5
lbl_accel.y = 312
group.append(lbl_accel)

display.root_group = group
display.auto_refresh = False
display.refresh()

import adafruit_ahtx0
aht = adafruit_ahtx0.AHTx0(i2c)

from adafruit_ltr329_ltr303 import LTR303
ltr = LTR303(i2c)
ltr.als_gain = 1
ltr.integration_time = 100
ltr.measurement_rate = 500

from sc7a20h import SC7A20H
sc7 = SC7A20H(i2c)

fc = 0
t0 = time.monotonic()

while True:
    frame = cam.take(1)
    if not isinstance(frame, displayio.Bitmap):
        continue
    camera_tg.bitmap = frame
    fc += 1

    if fc % 5 == 0:
        try:
            t = aht.temperature
            h = aht.relative_humidity
            lbl_temp.text = f"T:{t:.1f}C        "
            lbl_humi.text = f"RH:{h:.0f}%       "
        except:
            lbl_temp.text = "T:ERR          "
            lbl_humi.text = "RH:ERR         "

        try:
            ch0 = ltr.visible_plus_ir_light
            ch1 = ltr.ir_light
            if ch0 != 0:
                ratio = ch1 / ch0
                if ratio < 0.45: lux = (1.7743*ch0 + 1.1059*ch1) / ltr.als_data_gain
                elif ratio < 0.64: lux = (4.2785*ch0 - 1.9548*ch1) / ltr.als_data_gain
                elif ratio < 0.85: lux = (0.5926*ch0 + 0.5185*ch1) / ltr.als_data_gain
                else: lux = 0
            else: lux = 0
            lbl_lux.text = f"Lux:{lux:.0f}      "
        except:
            lbl_lux.text = "Lux:ERR       "

        try:
            x, y, z = sc7.acceleration
            lbl_accel.text = f"X:{x:+.1f} Y:{y:+.1f} Z:{z:+.1f}"
        except:
            lbl_accel.text = "ACC:ERR         "

    display.refresh()

    if fc % 25 == 0:
        print(f"{fc} fr, {fc/(time.monotonic()-t0):.1f}fps", end="")
