# GC2145 Camera Driver Analysis — CircuitPython / ESP32-S3

## 1. Driver Chain Overview

```
Python:  cam.take() → displayio.Bitmap
         cam.vflip = True  → set_reg_bits(0x17, bit 1)
         cam.hmirror = True → set_reg_bits(0x17, bit 0)

Binding: ports/espressif/bindings/espcamera/Camera.c
         ↓
Common-HAL: ports/espressif/common-hal/espcamera/Camera.c
         ↓
ESP-IDF: ports/espressif/esp-camera/
         └── driver/esp_camera.c       (main: init, frame capture)
         └── driver/sccb.c             (I2C/SCCB communication)
         └── sensors/gc2145.c          (sensor-specific: init, vflip, hmirror)
         └── sensors/gc2145_regs.h     (register definitions)

Frame Data: camera → DVP parallel bus (8-bit) → ESP32-S3 LCD_CAM peripheral
            → DMA → framebuffer (PSRAM) → RGB565 bitmap
```

## 2. Camera Initialization (Python → C)

### Python:
```python
cam = espcamera.Camera(
    data_pins=[board.CAM_D0..CAM_D7],  # GPIO 8,10,11,9,18,16,15,6
    pixel_clock_pin=board.CAM_PCLK,     # GPIO 17
    vsync_pin=board.CAM_VSYNC,          # GPIO 4
    href_pin=board.CAM_HREF,            # GPIO 5
    i2c=i2c,                             # busio.I2C on GPIO 47/48
    external_clock_pin=board.CAM_XCLK,  # GPIO 7 (20MHz)
    pixel_format=espcamera.PixelFormat.RGB565,
    frame_size=espcamera.FrameSize.QVGA,  # 320x240
    framebuffer_count=2,
)
```

### C (common-hal → IDF):

In `common_hal_espcamera_camera_construct()` (Camera.c):

```c
// 1. Claim all GPIO pins via claim_pin_number()
for (int i = 0; i < 8; i++) claim_pin_number(data_pins[i]);
claim_pin_number(pixel_clock_pin);
claim_pin_number(vsync_pin);
claim_pin_number(href_pin);
if (external_clock_pin) claim_pin_number(external_clock_pin);
if (powerdown_pin) claim_pin_number(powerdown_pin);
if (reset_pin) claim_pin_number(reset_pin);

// 2. Critical: REUSE Python's I2C bus instead of creating a second one
//    (otherwise two masters on same pins = crash)
self->camera_config.pin_sccb_sda = -1;     // DON'T init SCCB pins
self->camera_config.pin_sccb_scl = -1;     // DON'T init SCCB pins
self->camera_config.sccb_i2c_port = i2c->port;  // REUSE Python's I2C port

// 3. Configure DVP parallel bus pins
self->camera_config.pin_d0..d7 = data_pins[0..7];
self->camera_config.pin_pclk = pixel_clock_pin;
self->camera_config.pin_vsync = vsync_pin;
self->camera_config.pin_href = href_pin;
self->camera_config.pin_xclk = external_clock_pin;
self->camera_config.xclk_freq_hz = external_clock_frequency;

// 4. Frame config
self->camera_config.pixel_format = pixel_format;  // RGB565
self->camera_config.frame_size = frame_size;      // QVGA = 320x240
self->camera_config.jpeg_quality = jpeg_quality;
self->camera_config.fb_count = framebuffer_count; // 2 = double buffer
self->camera_config.grab_mode = grab_mode;         // WHEN_EMPTY

// 5. Init camera + sensor
i2c_lock(self);
esp_err_t result = esp_camera_init(&self->camera_config);
i2c_unlock(self);

// 6. After init, get sensor handle
sensor_t *sensor = esp_camera_sensor_get();
// Sensor is now initialized with default GC2145 registers
```

## 3. The VFLIP / HMIRROR Issue

### How they're supposed to work:

```c
// In gc2145.c:
static int set_vflip(sensor_t *sensor, int enable) {
    sensor->status.vflip = enable;
    write_reg(sensor->slv_addr, 0xfe, 0x00);    // Select page 0
    set_reg_bits(sensor->slv_addr, 0x17, 1, 0x01, enable != 0);  // 0x17 bit 1
    // Returns 0 on success
}

static int set_hmirror(sensor_t *sensor, int enable) {
    sensor->status.hmirror = enable;
    write_reg(sensor->slv_addr, 0xfe, 0x00);    // Select page 0
    set_reg_bits(sensor->slv_addr, 0x17, 0, 0x01, enable != 0);  // 0x17 bit 0
}
```

The `set_reg_bits()` function does a read-modify-write on register 0x17:

```c
static int set_reg_bits(uint8_t slv_addr, uint16_t reg,
                        uint8_t offset, uint8_t mask, uint8_t value) {
    int ret = read_reg(slv_addr, reg);    // Read current value of 0x17
    if (ret < 0) return ret;

    uint8_t c_value = ret;
    uint8_t new_value = (c_value & ~(mask << offset)) |
                        ((value & mask) << offset);
    ret = write_reg(slv_addr, reg, new_value);  // Write back modified value
    return ret;
}
```

And `read_reg` / `write_reg` call `SCCB_Read` / `SCCB_Write`:

```c
static int write_reg(uint8_t slv_addr, uint16_t reg, uint8_t data) {
    return SCCB_Write(slv_addr, reg & 0xFF, data);
}
```

### SCCB Communication (via shared I2C port):

```c
// In sccb.c (the OLD implementation, NOT sccb-ng.c):
int SCCB_Write(uint8_t slv_addr, uint8_t reg, uint8_t data) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (slv_addr << 1) | WRITE_BIT, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, reg, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, data, ACK_CHECK_EN);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(sccb_i2c_port, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    return (ret == ESP_OK) ? 0 : -1;
}
```

The I2C port used is the SAME port as Python's busio.I2C (GPIO 47/48).

### The I2C Lock in common-hal:

```c
// In common-hal/espcamera/Camera.c:
static void i2c_lock(espcamera_camera_obj_t *self) {
    // ... checks ...
    common_hal_busio_i2c_try_lock(self->i2c);   // Acquire Python busio lock
}

static void i2c_unlock(espcamera_camera_obj_t *self) {
    common_hal_busio_i2c_unlock(self->i2c);      // Release Python busio lock
}
```

### How vflip/hmirror are exposed to Python:

```c
// Macro in Camera.c:
#define SENSOR_STATUS_GETSET(type, name, status_field_name, setter_function_name) \
    SENSOR_GETSET(type, name, status.status_field_name, setter_function_name)

#define SENSOR_SET(type, name, setter_function_name) \
    void common_hal_espcamera_camera_set_##name(..., type value) {
        i2c_lock(self);
        sensor_t *sensor = esp_camera_sensor_get();
        i2c_unlock(self);                              // ← UNLOCK BEFORE WRITE!
        if (!sensor->setter_function_name) { ... error ... }
        sensor->setter_function_name(sensor, value);   // ← Write occurs OUTSIDE lock!
    }
```

### Why vflip/hmirror MIGHT NOT work (suspected root causes):

1. **LOCK RELEASED BEFORE WRITE**: The `i2c_unlock` happens BEFORE `sensor->set_vflip()`, so the SCCB write uses the I2C port without holding the Python lock. If any Python code tries to use the same I2C bus concurrently, it could interfere.

2. **SCCB uses RAW IDF I2C driver**: `SCCB_Write()` uses `i2c_master_cmd_begin()` directly on the shared I2C port, bypassing the Python busio layer. The Python lock doesn't actually prevent the IDF I2C driver from operating on the port.

3. **GC2145 register 0x17 might need additional configuration**: Some GC2145 variants need register 0xfe (page select) set to 0x00 BEFORE accessing 0x17. The driver DOES set 0xfe=0x00, but there might be timing issues.

4. **I2C write might FAIL silently**: `set_reg_bits` returns -1 on read failure but the caller would set the new value anyway if read failed. The macro checks `if (result < 0)` but the caller in `set_vflip` has `ret |= ...` which could mask the error.

## 4. GC2145 Register 0x17 (P0_ANALOG_MODE1)

```
Address: 0x17 (on Page 0, selected by 0xFE=0x00)

Bit 7: [reserved]
Bit 6: [reserved]
Bit 5: [reserved]
Bit 4: [reserved]
Bit 3: [reserved]
Bit 2: [reserved]
Bit 1: VFLIP   — Vertical flip (1 = enabled)
Bit 0: HMIRROR — Horizontal mirror (1 = enabled)

Default value after sensor init: 0x00 (no flip, no mirror)
```

**IMPORTANT**: There is NO register for 90° rotation in GC2145. Only VFLIP and HMIRROR are available.

## 5. Camera Frame Capture Flow

```python
frame = cam.take(timeout=1)
# Returns:
#   - displayio.Bitmap (if pixel_format == RGB565)  ← OUR CASE
#   - memoryview (if pixel_format == JPEG)
```

### In C:

```c
// take() → common_hal_espcamera_camera_take()
mp_obj_t common_hal_espcamera_camera_take(espcamera_camera_obj_t *self, int timeout_ms) {
    // Return any previously held frame
    esp_camera_fb_return(self->buffer_to_return);
    self->buffer_to_return = NULL;

    // Get new frame from camera
    camera_fb_t *fb = esp_camera_fb_get_timeout(timeout_ms);

    if (pixel_format == PIXFORMAT_RGB565) {
        // Wrap the framebuffer as a displayio Bitmap
        // The framebuffer IS already RGB565, no conversion needed
        return common_hal_displayio_bitmap_construct_from_buffer(
            fb->width, fb->height, 16, fb->buf, fb->len);
    }
}
```

The camera DMA writes raw pixel data directly into the framebuffer in PSRAM. For RGB565 format, the data is already in the correct format — no software conversion needed.

## 6. Pixel Byte Order (Critical for Display)

```
Camera output (RGB565):  little-endian byte pairs per pixel
    pixel[0] = G[2:0] | B[4:0]    (low byte)
    pixel[1] = R[4:0] | G[5:3]    (high byte)

Display expects (ILI9341 with reverse_bytes_in_word=True):
    The FourWire bus reverses bytes within each 16-bit word.
    So the display receives: [R[4:0]|G[5:3], G[2:0]|B[4:0]]
    Which is the correct big-endian RGB565 for ILI9341.
```

This is why `display_bus.send(0x2C, frame)` works directly with correct colors — the byte order matches.

## 7. Camera Resolutions

From GC2145 sensor.c:

```c
static const resolution_info_t resolution[] = {
    { 160,  120,  ASPECT_RATIO_4X3  },  // QQVGA
    { 176,  144,  ASPECT_RATIO_4X3  },  // QCIF
    { 240,  240,  ASPECT_RATIO_1X1  },  // R240X240
    { 320,  240,  ASPECT_RATIO_4X3  },  // QVGA
    { 400,  296,  ASPECT_RATIO_4X3  },  // WQVGA (or QVGA wide)
    { 480,  320,  ASPECT_RATIO_3X2  },  // HVGA
    { 640,  480,  ASPECT_RATIO_4X3  },  // VGA
    { 800,  600,  ASPECT_RATIO_4X3  },  // SVGA
    { 1600, 1200, ASPECT_RATIO_4X3  },  // UXGA
};
```

## 8. Key Files

| File | Purpose |
|------|---------|
| `ports/espressif/esp-camera/driver/esp_camera.c` | Main camera init, frame capture |
| `ports/espressif/esp-camera/driver/sccb.c` | SCCB I2C communication (the version actually used) |
| `ports/espressif/esp-camera/sensors/gc2145.c` | GC2145 sensor driver |
| `ports/espressif/esp-camera/sensors/private_include/gc2145_regs.h` | GC2145 register definitions |
| `ports/espressif/common-hal/espcamera/Camera.c` | CircuitPython common-hal layer |
| `ports/espressif/bindings/espcamera/Camera.c` | CircuitPython Python bindings |
| `esp-idf-config/sdkconfig.defaults` | Default IDF config (has `# CONFIG_GC2145_SUPPORT is not set`) |
| `boards/dfrobot_unihiker_k10/sdkconfig` | Board override (has `CONFIG_GC2145_SUPPORT=y`) |

## 9. Summary of Issues

1. **90° rotation**: GC2145 has NO hardware 90° rotation register. Only VFLIP (bit 1 of 0x17) and HMIRROR (bit 0 of 0x17).
2. **vflip/hmirror not working**: Suspect root cause is I2C lock released before register write, or SCCB communication failure on shared I2C port.
3. **Display bus.send() bypasses rotation**: `display_bus.send()` writes directly to display register 0x2C, so `display.rotation` has no effect.
4. **Frame pixel format**: Camera outputs RGB565 little-endian directly into a `displayio.Bitmap` — no conversion, maximum speed.

## 10. Possible Solutions

### A) Software rotation in Python
```python
# Create rotated bitmap (320x240 → 240x320), very slow per frame
rotated = displayio.Bitmap(240, 320, 65536)
for y in range(240):
    for x in range(320):
        rotated[239-y, x] = frame[x, y]  # 90° anti-clockwise
```
→ ~76k iterations per frame, extremely slow.

### B) Fix vflip/hmirror + accept the rotation
If vflip/hmirror are fixed, the user could flip the image. But this still doesn't give 90° rotation. The camera GC2145 physically outputs in 0° orientation; mounting on the board adds 90°.

### C) Modify GC2145 init registers in firmware
Before starting the sensor, send a custom register sequence that changes the readout window order. For GC2145, there's NO 90° rotation register, but there might be a way to change the window starting position and scan direction to simulate rotation.

### D) Use displayio pipeline (slower but supports rotation)
Use `display.root_group` with TileGrid + set `display.rotation = 90`:
```python
group = displayio.Group()
tg = displayio.TileGrid(frame, pixel_shader=displayio.ColorConverter())
group.append(tg)
display.root_group = group
display.rotation = 90
```
But updating the frame requires new Bitmap + new TileGrid each frame.

### E) Modify the LCD_CAM peripheral configuration in IDF
The ESP32-S3's LCD_CAM peripheral can be configured for different data orderings. Look for register `LCD_MIRROR` or `LCD_DATA_BYTE_SWAP` in the IDF LCD driver. This could potentially swap byte/word order to achieve a "rotation" effect at the hardware level.
