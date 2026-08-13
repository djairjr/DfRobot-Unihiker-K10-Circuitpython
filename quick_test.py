# Minimal K10 hardware test - inline I2C, no frozen deps
import board, busio, time, os, math, array

print(f"Board: {os.uname().machine}")
print(f"FW: {os.uname().version}")

i2c = busio.I2C(board.SCL, board.SDA)

# === PMIC init ===
def pw(r, v):
    i2c.writeto(0x20, bytes([r, v]))
def pr(r):
    i2c.writeto(0x20, bytes([r]))
    return i2c.readfrom(0x20, 1)[0]

i2c.try_lock()
pw(0x06, 0b11111010); pw(0x07, 0b01101111)
pw(0x02, 0b00000011); pw(0x03, 0b10000000)
p0 = pr(0x00); p1 = pr(0x01)
i2c.unlock()
print(f"PMIC: P0=0x{p0:02x} P1=0x{p1:02x} BL={'ON' if p0&1 else 'OFF'}")

# === NeoPixel ===
from neopixel import NeoPixel
np = NeoPixel(board.NEOPIXEL, 1, brightness=0.3)
np[0] = (0, 50, 0); print("NP: green")

# === Camera ===
import espcamera
try:
    cam = espcamera.Camera(
        data_pins=[board.CAM_D0,board.CAM_D1,board.CAM_D2,board.CAM_D3,
                   board.CAM_D4,board.CAM_D5,board.CAM_D6,board.CAM_D7],
        pixel_clock_pin=board.CAM_PCLK, vsync_pin=board.CAM_VSYNC,
        href_pin=board.CAM_HREF, i2c=i2c,
        external_clock_pin=board.CAM_XCLK, external_clock_frequency=20000000,
        pixel_format=espcamera.PixelFormat.RGB565,
        frame_size=espcamera.FrameSize.QVGA, framebuffer_count=2)
    print(f"CAM: {cam.sensor_name}")
except Exception as e:
    print(f"CAM: FAIL - {e}")

# === Display ===
import fourwire, busdisplay, displayio
displayio.release_displays()
spi = busio.SPI(board.SCK, MOSI=board.MOSI)
spi.try_lock(); spi.configure(baudrate=40000000, phase=0, polarity=0); spi.unlock()
db = fourwire.FourWire(spi, command=board.DC, chip_select=board.CS, reset=None)
d = busdisplay.BusDisplay(db, init_sequence=bytes([
    0x01,0,0x11,0,0x36,1,0x88,0x3A,1,0x55,0x21,0,0x13,0,0x29,0]),
    brightness=0.5, width=240, height=320, rotation=0, color_depth=16,
    grayscale=False, pixels_in_byte_share_row=False, bytes_per_cell=1,
    reverse_bytes_in_word=True, set_column_command=0x2A, set_row_command=0x2B,
    write_ram_command=0x2C, backlight_pin=None, single_byte_bounds=False)
g = displayio.Group()
bmp = displayio.Bitmap(240,320,16)
pal = displayio.Palette(16)
for i in range(16): pal[i] = (i*16, i*8, i*4)
tg = displayio.TileGrid(bmp, pixel_shader=pal); g.append(tg)
d.root_group = g
for y in range(320):
    for x in range(240):
        bmp[x,y] = ((x*16)//240 + (y*8)//320) % 16
print("DISP: gradient")

# === Audio ===
import audiobusio, audiocore
sr = 16000; tone = array.array("h", [int(16000*math.sin(2*math.pi*440*i/sr)) for i in range(sr*2)])
ts = audiocore.RawSample(tone, sample_rate=sr)
audio = audiobusio.I2SOut(board.I2S_BCLK, board.I2S_LRCLK, board.I2S_DOUT, main_clock=board.I2S_MCLK)
audio.play(ts, loop=False); time.sleep(2.5); audio.deinit()
print("AUDIO: 440Hz")

# === Buttons ===
print("Press A/B (5s)...")
for _ in range(20):
    i2c.try_lock()
    a = not (pr(0x01)&0x10); b = not (pr(0x00)&0x04)
    i2c.unlock()
    if a or b: print(f"  BTN: {'A' if a else ''}{' B' if b else ''}")
    time.sleep(0.25)
print("DONE")
