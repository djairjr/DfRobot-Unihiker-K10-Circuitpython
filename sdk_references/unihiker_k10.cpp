/*!
 * @file unihiker_k10.cpp
 * @brief This is the implementation of the driver library for UniHiker K10
 * @copyright   Copyright (c) 2010 DFRobot Co.Ltd (http://www.dfrobot.com)
 * @license     The MIT License (MIT)
 * @author [TangJie](jie.tang@dfrobot.com)
 * @version  V1.0
 * @date  2024-6-21 
 */

#include "unihiker_k10.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(BOARD_UNIHIKER_K10)
#include "esp32-hal-rmt.h"
#endif
#include "initBoard.h"
#include "app_wifi.h"
#include "app_httpd.hpp"
#include "app_mdns.h"
#include "SD.h"
#include <TFT_eSPI.h>
#include "GT30L24A3W.h"
#include "esp_code_scanner.h"
#include "lv_qrcode.h"


SemaphoreHandle_t xLvglMutex = NULL;///<LVGL mutex
extern SemaphoreHandle_t xSPIlMutex;
SemaphoreHandle_t xI2SMutex;

static TaskHandle_t cameraDisplayTaskHandle = NULL;///<Camera display handle


QueueHandle_t xQueueCamer= NULL;
QueueHandle_t xQueueAI= NULL;

extern QueueHandle_t xQueueID;

extern uint8_t _moveMode;


uint8_t configFPS = 0;

lv_obj_t *_cam = NULL;
lv_obj_t *_canvas = NULL;

int _dir = 2;
uint8_t camPlayState = 1;
static lv_obj_t *qrCode = NULL;

/*Change to your screen resolution*/
static const uint16_t screenWidth  = 240;
static const uint16_t screenHeight = 320;

static Gesture _gesture = GestureNone;


static uint8_t _stopMusic = 0;//stop music
static uint8_t _stopState = 1;//stop music

//ESP_Panel *panel = NULL;
static TFT_eSPI tft = TFT_eSPI(screenWidth, screenHeight); /* TFT instance */
static unsigned char _pBits[136];

static uint32_t bgColor = 0xffffff;

uint8_t playMusicState = 0;


Button::Button(uint8_t _io)
: pressedCb(NULL), unpressedCb(NULL), io(_io), io1(-1), io2(-1), pinNum(1), prevState(false)
{
}

Button::Button(uint8_t _io1, uint8_t _io2)
: pressedCb(NULL), unpressedCb(NULL), io(-1), io1(_io1), io2(_io2), pinNum(2)
{
}

bool Button::isPressed(void)
{
  int retry = 5;
  if (pinNum == 1){
    if (digital_read((ePin_t)io) == 0){
      while (retry--){
        if (digital_read((ePin_t)io))
          return false;
        delay(5);
      }
      if (digital_read((ePin_t)io) == 0)
        return true;
    }
      return false;
  }else{
    if ((digital_read((ePin_t)io1) == 0) && (digital_read((ePin_t)io2) == 0)){
      while (retry--){
        if (digital_read((ePin_t)io1) || digital_read((ePin_t)io2))
          return false;
        delay(5);
      }
      if ((digital_read((ePin_t)io1) == 0) && (digital_read((ePin_t)io2) == 0))
        return true;
    }
    return false;
  }
}

void Button::setPressedCallback(CBFunc cb)
{
  static char name[] = {"buttonX"};
  if (pinNum == 1)
    name[6] = io + '0';
  else
    name[6] = io1 + io2 + '0';
  if (pressedCb == NULL && unpressedCb == NULL){
    xTaskCreatePinnedToCore(Button::taskLoop, name, 8 * 1024, this, 5, NULL, 1);
  }
  pressedCb = cb;
}

void Button::setUnPressedCallback(CBFunc cb)
{
  static char name[] = {"buttonX"};
  if (pinNum == 1)
    name[6] = io + '0';
  else
    name[6] = io1 + io2 + '0';
  if (unpressedCb == NULL && pressedCb == NULL){
    xTaskCreatePinnedToCore(Button::taskLoop, name, 8 * 1024, this, 5, NULL, 1);
  }
  unpressedCb = cb;
}

AHT20::AHT20(TwoWire &wire):DFRobot_AHT20(wire){
  xTaskCreatePinnedToCore(AHT20::taskLoop, "aht20",  4 * 1024, this, 2, NULL, 1);
}

float AHT20::getData(eAHT20Data_t type)
{
  switch (type){
  case eAHT20TempC:
    return getTemperature_C();
  case eAHT20TempF:
    return getTemperature_F();
  case eAHT20HumiRH:
    return getHumidity_RH();
  default:
    break;
  }
  return -1;
}

RGB::RGB(uint8_t pin, uint16_t num, uint8_t bright, neoPixelType type) : Adafruit_NeoPixel(num, pin, type)
{
  c[0] = 0, c[1] = 0, c[2] = 0;
  _brightness = bright;
  setBrightness((uint8_t)map(_brightness, 0, 9, 0, 255));
}

#if defined(BOARD_UNIHIKER_K10)
static rmt_obj_t *k10OnboardRgbRmt = NULL;

void RGB::show(void)
{
  if (!pixels || numBytes == 0 || pin < 0) {
    return;
  }

  if (!k10OnboardRgbRmt) {
    k10OnboardRgbRmt = rmtInit(pin, RMT_TX_MODE, RMT_MEM_64);
    if (!k10OnboardRgbRmt) {
      return;
    }
    rmtSetTick(k10OnboardRgbRmt, 100);
  }

  const size_t numBits = numBytes * 8;
  rmt_data_t *rmt_data = (rmt_data_t *)malloc(sizeof(rmt_data_t) * numBits);
  if (!rmt_data) {
    return;
  }

  for (size_t i = 0; i < numBytes; i++) {
    uint8_t data = pixels[i];
    for (int j = 0; j < 8; j++) {
      const size_t idx = i * 8 + j;
      if (data & 0x80) {
        rmt_data[idx].duration0 = 8;
        rmt_data[idx].level0 = 1;
        rmt_data[idx].duration1 = 4;
        rmt_data[idx].level1 = 0;
      } else {
        rmt_data[idx].duration0 = 4;
        rmt_data[idx].level0 = 1;
        rmt_data[idx].duration1 = 8;
        rmt_data[idx].level1 = 0;
      }
      data <<= 1;
    }
  }

  rmtWriteBlocking(k10OnboardRgbRmt, rmt_data, numBits);
  free(rmt_data);
}
#else
void RGB::show(void)
{
  Adafruit_NeoPixel::show();
}
#endif

void RGB::write(int8_t index, uint8_t r, uint8_t g, uint8_t b)
{
  index = index == 0 ? 2 : index == 2 ? 0 : index;
  if (index >= 0 && index <= 2){
    setRangeColor(index, index, rgbToColor(round(r), round(g), round(b)));
  }else{
    setRangeColor(0, 2, rgbToColor(round(r), round(g), round(b)));
  }
  delay(5);
}

void RGB::brightness(uint8_t b)
{
  if (_brightness == b)
    return;
  _brightness = _max(_min(b, 9), 0);
  setBrightness((uint8_t)map(_brightness, 0, 9, 0, 255));
}

uint8_t RGB::brightness()
{
  return _brightness;
}

void RGB::setRangeColor(int16_t start, int16_t end, uint32_t c)
{

  if (start < 0 || end < 0){
    start = 0;
    end = PIXEL_COUNT - 1;
  }

  if (end < start){
    uint16_t num = end;
    end = start;
    start = num;
  }

  if (PIXEL_COUNT <= 0)
    return;

  start = _max((int)start, 0);
  start = _min((int)start, (int)PIXEL_COUNT);
  end = _max((int)end, 0);
  end = _min((int)end, (int)PIXEL_COUNT);

  for (uint16_t n = start; n < end + 1; n++){
    setPixelColor(n, c);
  }
  show();
}

uint32_t RGB::rgbToColor(uint8_t r, uint8_t g, uint8_t b)
{
  uint32_t cr = r;
  uint32_t cg = g;
  uint32_t cb = b;
  return ((cr << 16) | (cg << 8)) | cb;
}

UNIHIKER_K10::UNIHIKER_K10(TwoWire &wire):_pWire(&wire)
{
}
UNIHIKER_K10::~UNIHIKER_K10(void)
{
}


static void gesture_task(void *time)
{
  UNIHIKER_K10 *self = (UNIHIKER_K10 *)time;
  TickType_t xLastWakeTime;
  const TickType_t xFrequency = pdMS_TO_TICKS(100); //Specify the task's running period as 100ms.
  uint8_t buf[6];
  while(1){
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
    self->readData(0x19,0x27,buf,1);
    if((buf[0] & 0x0f) == 0x0f){
      self->readData(0x19,0xA8,buf,6);
      self->accX = (int)((buf[1]<< 8 | buf[0]) >> 4);
      self->accY = (int)((buf[3]<< 8 | buf[2]) >> 4);
      self->accZ = (int)((buf[5]<< 8 | buf[4]) >> 4);
      if((self->accX & 0x800) == 0x800){
        self->accX -= 4096;
      }  
      if((self->accY & 0x800) == 0x800){
        self->accY -= 4096;
      } 
      if((self->accZ & 0x800) == 0x800){
        self->accZ -= 4096;
      }  
    }
    self->readData(0x19, 0x35, buf, 1);
    if((buf[0]&0x60) == 0x60){
      _gesture = ScreenUp;
    }else if((buf[0]&0x50) == 0x50){
      _gesture = ScreenDown;
    }else if((buf[0]&0x41) == 0x41){
      _gesture = TiltLeft;
    }else if((buf[0]&0x42) == 0x42){
      _gesture = TiltRight;
    }else if((buf[0]&0x44) == 0x44){
      _gesture = TiltForward;
    }else if((buf[0]&0x48) == 0x48){
      _gesture = TiltBack;
    }else if(buf[0] != 0){
      _gesture = Shake;
    }
  }
  vTaskDelete(NULL);
}

void UNIHIKER_K10::begin(void)
{
  xI2SMutex = xSemaphoreCreateMutex(); // Create an I2S mutex
  xSPIlMutex = xSemaphoreCreateMutex(); // Create an SPI mutex
  //xLvglMutex = xSemaphoreCreateMutex();
  
  init_board();
  delay(10);
  digital_write(eLCD_BLK,0);
  delay(100);
  digital_write(eLCD_BLK,1);
  delay(100);
  digital_write(eLCD_BLK,0);
  digital_write(eAmp_Gain,0);
  _pWire->begin();
  _pWire->beginTransmission(LTR303ALS_ADDR);
  uint8_t ret = _pWire->endTransmission(); 
  if(ret != 0){
	  printf("LTR303ALS Device not found, please check if the device is connected.");
  }
  writeCommand(LTR303ALS_ADDR,LTR303ALS_CTRL,LTR303ALS_GAIN_MODE);
  writeCommand(LTR303ALS_ADDR,LTR303ALS_MEAS_RATE,LTR303ALS_INTEG_RATE);
  if(!rgb){
    rgb = new RGB;
  }
  rgb->setRangeColor(0,2,0);
  if (!buttonA){
    buttonA = new Button(eP5_KeyA);
  } 
  if (!buttonB){
    buttonB = new Button(eP11_KeyB);
  }
  if (!buttonAB){
    buttonAB = new Button(eP5_KeyA, eP11_KeyB);
  }
  GT_Font_Init();
  _pWire->beginTransmission(_sc7a20hAddr);
  ret = _pWire->endTransmission();
  if(ret != 0){
	  printf("SC7A20H Device not found, please check if the device is connected.");
  }
  initSC7A20H();// Initialize accelerometer.
  initI2S();
  xTaskCreatePinnedToCore(&gesture_task, "gesture_task", 2 * 1024, this, 5, NULL, 1);
}


/* Display flushing */
static void my_disp_flush( lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p )
{
  uint32_t w = ( area->x2 - area->x1 + 1 );
  uint32_t h = ( area->y2 - area->y1 + 1 );
  tft.startWrite();
  tft.setAddrWindow( area->x1, area->y1, w, h );
  tft.pushColors( ( uint16_t * )&color_p->full, w * h, false );
  tft.endWrite();
  lv_disp_flush_ready( disp_drv );
}


static void cameDisPlayTask(void *xQueueCam)
{
  camera_fb_t *frame = NULL;
  QueueHandle_t _xQueueCam = (QueueHandle_t)xQueueCam;
  lv_img_dsc_t img_camera;                               
  for(;;)
  {
    if(_xQueueCam == nullptr)
      break;
    if (xQueueReceive(_xQueueCam, &frame, portMAX_DELAY)){
      xSemaphoreTake(xLvglMutex, portMAX_DELAY);
      if(camPlayState == 1){
        img_camera.header.cf = LV_IMG_CF_TRUE_COLOR;
        img_camera.header.always_zero = 0;
        img_camera.header.w = frame->width;
        img_camera.header.h = frame->height;
        img_camera.data_size = 153600 / LV_IMG_PX_SIZE_ALPHA_BYTE;
        img_camera.data = (const uint8_t*)frame->buf;//(const uint8_t*)camBuf;
        lv_img_set_src(_cam,&img_camera);
        lv_task_handler();
      }
      xSemaphoreGive(xLvglMutex);
      esp_camera_fb_return(frame);
    }
    vTaskDelay(1);
  }
  vTaskDelete(NULL);
}


void UNIHIKER_K10::initScreen(int dir, int frame)
{
  if(!xLvglMutex){
    _dir = dir;
    static lv_disp_drv_t disp_drv;
    static lv_disp_draw_buf_t draw_buf;
    xLvglMutex = xSemaphoreCreateMutex();
    lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(screenWidth * screenHeight* sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    assert(buf1);
    lv_init();//init lvgl
    digital_write(eLCD_BLK,1);
    lv_disp_draw_buf_init( &draw_buf, buf1, NULL, screenWidth * screenHeight);
    lv_disp_drv_init( &disp_drv );
    /*Change the following line to your display resolution*/
    if(_dir == 0 || _dir == 2){
      disp_drv.hor_res = 240;
      disp_drv.ver_res = 320;
    }else if(_dir == 1 || _dir == 3){
      disp_drv.hor_res = 320;
      disp_drv.ver_res = 240;
    }else{
      disp_drv.hor_res = 240;
      disp_drv.ver_res = 320;
    }
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register( &disp_drv );
    configFPS = 1;
    tft.begin();          /* TFT init */
    tft.setRotation(_dir); /* Landscape orientation, flipped */        
    this->_scr = lv_scr_act();
    lv_obj_clear_flag(this->_scr,LV_OBJ_FLAG_SCROLLABLE);
  }
} 

void UNIHIKER_K10::setScreenBackground(uint32_t color)
{
  bgColor = color;
  xSemaphoreTake(xLvglMutex, portMAX_DELAY);
  lv_obj_set_style_bg_color(this->_scr, lv_color_hex(color), 0);
  lv_task_handler();
  xSemaphoreGive(xLvglMutex);
}

void UNIHIKER_K10::creatCanvas(void)
{
  if(!canvas){
    canvas = new Canvas(this->_scr, _dir);
  }
  this->setScreenBackground(0xffffff);
}

void UNIHIKER_K10::initBgCamerImage(void)
{
  _cam = lv_img_create(this->_scr);
  xSemaphoreTake(xLvglMutex, portMAX_DELAY);
  lv_obj_set_pos(_cam, 0, 0); // Set the position of the image display.
  lv_obj_set_size(_cam,240,320);// Set the size of the image
  lv_obj_clear_flag(_cam,LV_OBJ_FLAG_SCROLLABLE);
  xSemaphoreGive(xLvglMutex);
  if(!xQueueCamer){
    xQueueCamer = xQueueCreate(2, sizeof(camera_fb_t *));
    register_camera(PIXFORMAT_RGB565, FRAMESIZE_QVGA, 2, xQueueCamer);//FRAMESIZE_HVGA  FRAMESIZE_QVGA FRAMESIZE_240X240
    xTaskCreatePinnedToCore(&cameDisPlayTask, "cameDisPlayTask", 4 * 1024, (void*)xQueueCamer, 5, &cameraDisplayTaskHandle, 1);// Create a camera display task, task level 5 (general level) 
  }else{
    register_camera(PIXFORMAT_RGB565, FRAMESIZE_QVGA, 2, xQueueCamer);//FRAMESIZE_HVGA  FRAMESIZE_QVGA FRAMESIZE_240X240
    xTaskCreatePinnedToCore(&cameDisPlayTask, "cameDisPlayTask", 4 * 1024, (void*)xQueueAI, 5, &cameraDisplayTaskHandle, 1);
  }
}

void UNIHIKER_K10::setBgCamerImage(bool sta)
{
  if(sta){
    camPlayState = 1;
    xSemaphoreTake(xLvglMutex, portMAX_DELAY);
    lv_obj_clear_flag(_cam, LV_OBJ_FLAG_HIDDEN);
    lv_task_handler();
    xSemaphoreGive(xLvglMutex);
  }else{
    camPlayState = 0;
    xSemaphoreTake(xLvglMutex, portMAX_DELAY);
    lv_obj_add_flag(_cam, LV_OBJ_FLAG_HIDDEN);
    lv_task_handler();
    xSemaphoreGive(xLvglMutex);
  }
}

void UNIHIKER_K10::initSDFile(void)
{
  while(1){
    if(SD.begin()){
      printf("initialization done.");
      break;
    }
    printf("initialization failed!");
  }
  lv_fs_fatfs_init();
}

// BMP文件头结构体
struct BMPFileHeader
{
  uint16_t bfType;
  uint32_t fileSize;
  uint16_t reserved1;
  uint16_t reserved2;
  uint32_t offset;
  uint32_t headerSize;
  int32_t width;
  int32_t height;
  uint16_t planes;
  uint16_t bitsPerPixel;
  uint32_t compression;
  uint32_t dataSize;
  int32_t hResolution;
  int32_t vResolution;
  uint32_t colors;
  uint32_t importantColors;
  uint32_t maskR;
  uint32_t maskG;
  uint32_t maskB;
}__attribute__((packed));


void UNIHIKER_K10::photoSaveToTFCard(const char *imagePath)
{
  digital_write(eAmp_Gain,1);
  BMPFileHeader bmpHeader;
  lv_fs_file_t phFile;
  lv_fs_res_t ret = 0;
  uint16_t pixel = 0;
  uint16_t data = 0;
  camera_fb_t *frame = NULL;
  // Initialize BMP file header
  bmpHeader.bfType = 0x4D42; // ASCII code for "BM" characters
  bmpHeader.reserved1 = 0;
  bmpHeader.reserved2 = 0;
  bmpHeader.offset = 54; // Data starts at byte 54
  bmpHeader.headerSize = 40; // Size of information header
  bmpHeader.planes = 1; // Must be 1
  bmpHeader.bitsPerPixel = 16; // 16 bits per pixel (RGB565)
  bmpHeader.compression = 3; // No compression
  bmpHeader.hResolution = 0; // Pixels per meter
  bmpHeader.vResolution = 0; // Pixels per meter
  bmpHeader.colors = 0; // Use all colors
  bmpHeader.importantColors = 0; // All colors are important
  bmpHeader.maskR = 0xf800; // All colors are important
  bmpHeader.maskG = 0x07e0; // All colors are important
  bmpHeader.maskB = 0x001f; // All colors are important
  xSemaphoreTake(xSPIlMutex, portMAX_DELAY);
  ret = lv_fs_open(&phFile,imagePath,LV_FS_MODE_WR);
  xSemaphoreGive(xSPIlMutex);
  if (ret != LV_FS_RES_OK){
    printf("Failed to open file for writing\n");
    return;
  }
  //GGGBBBBB RRRRRGGG
  xSemaphoreTake(xLvglMutex, portMAX_DELAY);
  if(xQueueReceive(xQueueCamer, &frame, portMAX_DELAY)){
    xSemaphoreGive(xLvglMutex);
    bmpHeader.fileSize = 54 + (frame->width * frame->height * 2) + 12; // 54 is the size of the BMP file header
    bmpHeader.width = frame->width;
    bmpHeader.height = frame->height;
    bmpHeader.dataSize = frame->width * frame->height * 2; // Image data size
    xSemaphoreTake(xSPIlMutex, portMAX_DELAY);
    ret = lv_fs_write(&phFile,(uint8_t *)&bmpHeader,sizeof(bmpHeader),NULL);
    xSemaphoreGive(xSPIlMutex);
    if(ret != LV_FS_RES_OK){
      lv_fs_close(&phFile);
    }
    
    for(int i = frame->height - 1; i >= 0; i--){
      for(int j = 0; j < frame->width; j++){
        pixel = ((uint16_t *)frame->buf)[i * frame->width + j];
        data = pixel >> 8 | pixel << 8;
        xSemaphoreTake(xSPIlMutex, portMAX_DELAY);
        lv_fs_write(&phFile,(uint8_t *)&data,sizeof(data),NULL);
        xSemaphoreGive(xSPIlMutex);
      }
    }
  }
  
  lv_fs_close(&phFile);
  esp_camera_fb_return(frame);
  digital_write(eAmp_Gain,0);
}

void UNIHIKER_K10::photoSaveToTFCard(String imagePath)
{
  photoSaveToTFCard(imagePath.c_str());
}


// Callback function to get the glyph descriptor
bool myGetGlyphDscCb_16(const lv_font_t * font, lv_font_glyph_dsc_t * dsc_out, uint32_t unicode_letter, uint32_t unicode_letter_next)
{
  dsc_out->box_h = 12;   /* Height of the glyph bitmap (in pixels) */
  dsc_out->box_w = 16;   /* Width of the glyph bitmap (in pixels) */
  if(unicode_letter < 128){
    dsc_out->adv_w = ASCII_GetInterval(unicode_letter,ASCII_12_A);   /* Letter spacing */
  }else{
    dsc_out->adv_w = 12;   
  }
  dsc_out->ofs_x = 0;    /* X offset of the glyph bitmap (in pixels) */
  dsc_out->ofs_y = 0;    /* Y offset of the glyph bitmap (in pixels), relative to the baseline */
  dsc_out->bpp = 1;      /* Bits per pixel: 1/2/4/8 */
  dsc_out->is_placeholder = false;
  return true;           /* true: glyph found; false: glyph not found */
}

// Callback function to get the glyph bitmap
const uint8_t * myGetGlyphBitmapCb_16(const lv_font_t * font, uint32_t unicode_letter)
{
  // Return pointer to glyph bitmap data
  // Replace the example return value below with actual bitmap data for character "你"
  static uint16_t u2g;
  if (unicode_letter < 128) {
    ASCII_GetData(unicode_letter, ASCII_12_A, _pBits);
  } else {
    u2g = U2G(unicode_letter);
    gt_12_GetData((u2g >> 8) & 0xff, u2g & 0xff, _pBits);
  }
  return (const uint8_t*)_pBits; // Return pointer to array containing bitmap data for character "你"
}

// Callback function to get the glyph descriptor
bool myGetGlyphDscCb_24(const lv_font_t * font, lv_font_glyph_dsc_t * dsc_out, uint32_t unicode_letter, uint32_t unicode_letter_next)
{
  // Set glyph descriptor, including glyph width, height, offset, etc.
  dsc_out->box_h = 24;   /* Height of the glyph bitmap (in pixels) */
  dsc_out->box_w = 24;   /* Width of the glyph bitmap (in pixels) */
  if (unicode_letter < 128) {
    dsc_out->adv_w = ASCII_GetInterval(unicode_letter, ASCII_24_B);   /* Letter spacing */
  } else {
    dsc_out->adv_w = 24;   /* Letter spacing */
  }
  dsc_out->ofs_x = 0;    /* X offset of the glyph bitmap (in pixels) */
  dsc_out->ofs_y = 0;    /* Y offset of the glyph bitmap (in pixels), relative to the baseline */
  dsc_out->bpp = 1;      /* Bits per pixel: 1/2/4/8 */
  dsc_out->is_placeholder = false;
  return true;           /* true: glyph found; false: glyph not found */
}


// Callback function to get the glyph bitmap
const uint8_t * myGetGlyphBitmapCb_24(const lv_font_t * font, uint32_t unicode_letter)
{
  // Return pointer to glyph bitmap data
  // Replace the example return value below with actual bitmap data for character "你"
  static uint16_t u2g;
  if (unicode_letter < 128) {
    ASCII_GetData(unicode_letter, ASCII_24_B, _pBits);
  } else {
    u2g = U2G(unicode_letter);
    GBK_24_GetData((u2g >> 8) & 0xff, u2g & 0xff, _pBits);
  }
  return (const uint8_t*)_pBits; // Return pointer to array containing bitmap data for character "你"
}


const lv_font_t my_custom_font_16 = {
  .get_glyph_dsc = myGetGlyphDscCb_16,
  .get_glyph_bitmap = myGetGlyphBitmapCb_16,
  .line_height = 15,  // Line height
  .base_line = 3,     // Baseline
  .subpx = LV_FONT_SUBPX_NONE,
  .underline_position = 0,
  .underline_thickness = 0,
  .dsc = NULL,
  .fallback = NULL,
#if LV_USE_USER_DATA
  .user_data = NULL,
#endif
};  // Font parsing structure for the font chip

const lv_font_t my_custom_font_24 = {
  .get_glyph_dsc = myGetGlyphDscCb_24,
  .get_glyph_bitmap = myGetGlyphBitmapCb_24,
  .line_height = 27,
  .base_line = 3,
  .subpx = LV_FONT_SUBPX_NONE,
  .underline_position = 0,
  .underline_thickness = 0,
  .dsc = NULL,
  .fallback = NULL,
#if LV_USE_USER_DATA
  .user_data = NULL,
#endif
};  // Font parsing structure for the font chip


Canvas::Canvas(void *obj,int dir)
{
  lv_obj_t *_obj = (lv_obj_t *)obj;
    
  if(!_canvas){
    xSemaphoreTake(xLvglMutex, portMAX_DELAY);
    _canvas = lv_canvas_create(_obj);
    lv_color_t *canvasBuf= (lv_color_t *)heap_caps_malloc(240 * 320* sizeof(lv_color_t)*8, MALLOC_CAP_SPIRAM);//MALLOC_CAP_SPIRAM
    assert(canvasBuf);
    if(dir == 0 || dir == 2){
      lv_canvas_set_buffer(_canvas,canvasBuf,240,320,LV_IMG_CF_TRUE_COLOR_ALPHA);
    }else if(dir == 1 || dir == 3){
      lv_canvas_set_buffer(_canvas,canvasBuf,320,240,LV_IMG_CF_TRUE_COLOR_ALPHA);
    }
    lv_canvas_fill_bg(_canvas,lv_color_make(0x0,0x0,0x0),LV_OPA_TRANSP);
    lv_task_handler();
    xSemaphoreGive(xLvglMutex);
  }
}

void Canvas::updateCanvas(void)
{
  xSemaphoreTake(xLvglMutex, portMAX_DELAY);
  lv_task_handler();
  xSemaphoreGive(xLvglMutex);
}

void Canvas::canvasClear(void)
{
  xSemaphoreTake(xLvglMutex, portMAX_DELAY);
  lv_canvas_fill_bg(_canvas,lv_color_make(0x00,0x00,0x00),LV_OPA_TRANSP);
  lv_task_handler();
  xSemaphoreGive(xLvglMutex);
}

void Canvas::canvasDrawBitmap(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* bitmap)
{
  lv_img_dsc_t image; 
  image.header.always_zero = 0;
  image.header.w = w;
  image.header.h = h;
  image.data_size = w * h * 2;
  image.header.cf = LV_IMG_CF_TRUE_COLOR;
  image.data = bitmap,
  this->canvasDrawImage(x,y,&image);
}

void Canvas::canvasDrawImage(int16_t x, int16_t y, const void *dir)
{
  xSemaphoreTake(xLvglMutex, portMAX_DELAY);
  lv_draw_img_dsc_t img_dsc;
  lv_draw_img_dsc_init(&img_dsc);
  lv_canvas_draw_img(_canvas,x,y,dir,&img_dsc);
  xSemaphoreGive(xLvglMutex);
}

void Canvas::canvasDrawImage(int16_t x, int16_t y, String imagePath)
{
  this->canvasDrawImage(x,y,imagePath.c_str());
}

void Canvas::canvasText(const char* text, uint8_t row, uint32_t color)
{
  uint16_t x,y,_h;
  if(_dir == 0 || _dir == 2){
    if (row > 13)
      return;
  }else if(_dir == 1 || _dir == 3){
    if (row > 9)
      return;
  }
  _h = row == 1 ? 1 : row;
  y = _h > 13 ? (13*24 +5) : ((_h-1)*24 +5);
  x = 9;
  
  this->canvasClear(row);
  xSemaphoreTake(xLvglMutex, portMAX_DELAY); 
  lv_draw_label_dsc_t label_dsc;
  lv_draw_label_dsc_init(&label_dsc);
  label_dsc.color = lv_color_hex(color);
  label_dsc.align = LV_TEXT_ALIGN_LEFT;
  label_dsc.font = &my_custom_font_24;
  label_dsc.flag = LV_TEXT_FLAG_EXPAND;
  
  if(_dir == 0 || _dir == 2){
    lv_canvas_draw_text(_canvas, x, y, 320, &label_dsc, text);
  }else if(_dir == 1 || _dir == 3){
    lv_canvas_draw_text(_canvas, x, y, 320, &label_dsc, text);
  }
  xSemaphoreGive(xLvglMutex);
}

void Canvas::canvasClear(uint8_t row)
{
  uint16_t x = 0, w = 0, h = 0;
  uint16_t y = row == 1 ? 5 : 24 * (row - 1) + 5;
  if(_dir == 0 || _dir == 2){
    w = 240;
    h = row == 1 ? 24 + 0 : row > 13 ? 320 - y
                                             : 24;
  }else if(_dir == 1 || _dir == 3){
      w = 320;
      h = row == 1 ? 24 + 0 : row > 13 ? 240 - y
                                             : 24;
  }
  this->clearLocalCanvas(x,y,w,h);
}

void Canvas::clearLocalCanvas(uint16_t x,uint16_t y,uint16_t w,uint16_t h)
{
  if (xQueueCamer != NULL) {
    // 摄像头模式：用透明填充，让摄像头画面在文字行透出，不盖住画面
    size_t bufSize = LV_IMG_BUF_SIZE_TRUE_COLOR_ALPHA(w, h);
    uint8_t *_canvasNullBuf = (uint8_t *)heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM);
    assert(_canvasNullBuf);
    lv_color_t clearColor = lv_color_black();
    const uint8_t pxSize = LV_IMG_PX_SIZE_ALPHA_BYTE;
    for (uint32_t i = 0; i < (uint32_t)w * h; i++) {
      uint8_t *px = _canvasNullBuf + i * pxSize;
      memcpy(px, &clearColor, sizeof(lv_color_t));
      px[sizeof(lv_color_t)] = LV_OPA_TRANSP;
    }
    xSemaphoreTake(xLvglMutex, portMAX_DELAY);
    lv_canvas_copy_buf(_canvas, _canvasNullBuf, x, y, w, h);
    xSemaphoreGive(xLvglMutex);
    heap_caps_free(_canvasNullBuf);
  } else {
    // 非摄像头模式：用背景色不透明矩形覆盖，可清除透明底和有色文字
    this->canvasRectangle(x, y, w, h, bgColor, bgColor, true);
  }
}

void Canvas::canvasText(float text, uint8_t row, uint32_t color)
{
  this->canvasText(String(text).c_str(),row,color);
}

void Canvas::canvasText(String text, uint8_t row, uint32_t color)
{
  this->canvasText(text.c_str(),row,color);
}


void Canvas::insertLineBreaks(const char* src, char* dest, int charPerLine)
{
  int currentLength = 0;
  int j = 0;

  for (int i = 0; src[i] != '\0';) {
    char c = src[i];
    dest[j++] = c;

    if ((c & 0x80) == 0) {  // ASCII character, 1 byte
      currentLength += 1;
      i += 1;
    } else if ((c & 0xE0) == 0xC0) {  // 2-byte character
      currentLength += 1;
      dest[j++] = src[i + 1];
      i += 2;
    } else if ((c & 0xF0) == 0xE0) {  // 3-byte character
      currentLength += 1;
      dest[j++] = src[i + 1];
      dest[j++] = src[i + 2];
      i += 3;
    } else if ((c & 0xF8) == 0xF0) {  // 4-byte character
      currentLength += 1;
      dest[j++] = src[i + 1];
      dest[j++] = src[i + 2];
      dest[j++] = src[i + 3];
      i += 4;
    }
    
    if (currentLength >= charPerLine) {
      dest[j++] = '\n';
      currentLength = 0;
    }
  }
  dest[j] = '\0';
}

void Canvas::canvasText(const char* text, int16_t x, int16_t y, uint32_t color, eFontSize_t font, int count, bool autoClean) 
{
  uint16_t _x = x;
  uint16_t _y = y;
  uint16_t _w = 0;
  uint16_t _h = 0;
  char _taxt[1024] = "\0";
  if(autoClean){
    if (_dir == 0 || _dir == 2) {
      _x = (_x > 240) ? 240 : _x;
      _y = (_y > 320) ? 320 : _y;
      _w = _max(0, 240 - _x); // Clear the entire row after the x coordinate
      _h = _max(0, 320 - _y); // Clear the entire column after the y coordinate
      if(xQueueCamer){
          this->clearLocalCanvas(_x-1, _y-1, _w+1, _h+1);
      }else{
          this->canvasRectangle(_x-1, _y-1, _w+1, _h+1,bgColor,bgColor,true);
      }
    }else{
      _x = (_x > 320) ? 320 : _x;
      _y = (_y > 240) ? 240 : _y;
      _w = _max(0, 320 - x); // Clear the entire column after the x coordinate
      _h = _max(0, 240 - y); // Clear the entire row after the y coordinate
      if(xQueueCamer){
        this->clearLocalCanvas(_x-1, _y-1, _w+1, _h+1);
      }else{
        this->canvasRectangle(_x-1, _y-1, _w+1, _h+1,bgColor,bgColor,true);
      }
    }
  }
    
  _h = font == Canvas::eCNAndENFont24 ? 24 : 16;
    
  xSemaphoreTake(xLvglMutex, portMAX_DELAY);
    
    
  lv_draw_label_dsc_t label_dsc;
  lv_draw_label_dsc_init(&label_dsc);
  label_dsc.color = lv_color_hex(color);
  label_dsc.align = LV_TEXT_ALIGN_LEFT;
  label_dsc.opa = LV_OPA_COVER;
  if (font < 3) {
    label_dsc.font = &my_custom_font_24;
  }else{
    label_dsc.font = &my_custom_font_16;
  }
  label_dsc.flag = LV_TEXT_FLAG_EXPAND;
  int start = 0;
  if(count  < 50){
    insertLineBreaks(text,_taxt,count);
    int start = 0;
    int len = strlen(_taxt);
    if(len > 1024){
      len = 1024;
    }
    for (int i = 0; i <= len; i++) {
      if (_taxt[i] == '\n' || _taxt[i] == '\0') {
        char line[1024];
        strncpy(line, _taxt + start, i - start);
        line[i - start] = '\0';
        if (_dir == 0 || _dir == 2) {
          lv_canvas_draw_text(_canvas, x, y, 240, &label_dsc, line);
        } else if (_dir == 1 || _dir == 3) {
          lv_canvas_draw_text(_canvas, x, y, 320, &label_dsc, line);
        }
        y += _h;
        start = i + 1;
      }
    }
  }else{
    int len = strlen(text);
    if(len > 1024){
      len = 1024;
    }
    for (int i = 0; i <= len; i++) {
      if (text[i] == '\n' || text[i] == '\0') {
        char line[1024];
        strncpy(line, text + start, i - start);
        line[i - start] = '\0';
        if (_dir == 0 || _dir == 2) {
          lv_canvas_draw_text(_canvas, x, y, 240, &label_dsc, line);
        } else if (_dir == 1 || _dir == 3) {
          lv_canvas_draw_text(_canvas, x, y, 320, &label_dsc, line);
        }
        y += _h;
        start = i + 1;
      }
    }
  }
    
  xSemaphoreGive(xLvglMutex);
}

void Canvas::canvasText(float text, int16_t x, int16_t y, uint32_t color, eFontSize_t font, int count, bool autoClean)
{
  this->canvasText(String(text).c_str(),x,y,color,font,count,autoClean);
}

void Canvas::canvasText(String text, int16_t x, int16_t y, uint32_t color, eFontSize_t font, int count, bool autoClean)
{
  this->canvasText(text.c_str(),x,y,color,font,count,autoClean);
}

void Canvas::canvasPoint(int16_t x, int16_t y, uint32_t color)
{
  xSemaphoreTake(xLvglMutex, portMAX_DELAY);
  lv_draw_arc_dsc_t arc_dsc;
  lv_draw_arc_dsc_init(&arc_dsc);
  arc_dsc.color = lv_color_hex(color);
  arc_dsc.opa = LV_OPA_COVER;
  arc_dsc.width=this->_lineW;
  lv_canvas_draw_arc(_canvas,x,y,this->_lineW,0,360,&arc_dsc);
  xSemaphoreGive(xLvglMutex);
}

void Canvas::canvasSetLineWidth(uint8_t w )
{
  this->_lineW = w;
}

void Canvas::canvasLine(int x1, int y1, int x2, int y2, uint32_t color)
{
  lv_point_t _points[2]={{0,0},{0,0}};
  _points[0].x = x1;
  _points[0].y = y1;
  _points[1].x = x2;
  _points[1].y = y2;
  xSemaphoreTake(xLvglMutex, portMAX_DELAY);
  lv_draw_line_dsc_t line_dsc;
  lv_draw_line_dsc_init(&line_dsc);
  line_dsc.color = lv_color_hex(color);
  line_dsc.width = this->_lineW;
  lv_canvas_draw_line(_canvas,_points,2,&line_dsc);
  xSemaphoreGive(xLvglMutex);
}

void Canvas::canvasCircle(int x, int y, int r, uint32_t color, uint32_t bg_color, bool fill)
{
  xSemaphoreTake(xLvglMutex, portMAX_DELAY);
  lv_draw_arc_dsc_t arc_dsc;
  lv_draw_arc_dsc_init(&arc_dsc);
  if(fill){
    arc_dsc.color = lv_color_hex(bg_color);
    arc_dsc.opa = LV_OPA_COVER;
    arc_dsc.width=r-1;
    lv_canvas_draw_arc(_canvas,x,y,r-1,0,360,&arc_dsc);
    arc_dsc.color = lv_color_hex(color);
    arc_dsc.width=this->_lineW;
    lv_canvas_draw_arc(_canvas,x,y,r,0,360,&arc_dsc);
  }else{
    arc_dsc.color = lv_color_hex(color);
    arc_dsc.opa = LV_OPA_COVER;
    arc_dsc.width=this->_lineW;
    lv_canvas_draw_arc(_canvas,x,y,r,0,360,&arc_dsc);
  }
  xSemaphoreGive(xLvglMutex);
}

void Canvas::canvasRectangle(int x, int y, int w, int h, uint32_t color, uint32_t bg_color, bool fill)
{
  xSemaphoreTake(xLvglMutex, portMAX_DELAY);
  lv_draw_rect_dsc_t rect_dsc;
  lv_draw_rect_dsc_init(&rect_dsc);
  if(!fill){
    rect_dsc.bg_opa = LV_OPA_TRANSP;
  }else{
    rect_dsc.bg_color = lv_color_hex(bg_color);
  }
  
  rect_dsc.border_color = lv_color_hex(color);
  rect_dsc.border_width = this->_lineW;
  lv_canvas_draw_rect(_canvas,x,y,w,h,&rect_dsc);
  xSemaphoreGive(xLvglMutex);
}






void UNIHIKER_K10::initI2S(void)
{
  //初始化I2S
  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX), // the mode must be set according to DSP configuration
      .sample_rate = 16000,                                // must be the same as DSP configuration
      .bits_per_sample = (i2s_bits_per_sample_t)16,        // must be the same as DSP configuration //16
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,         // must be the same as DSP configuration //I2S_CHANNEL_FMT_RIGHT_LEFT I2S_CHANNEL_FMT_ONLY_LEFT
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags       = ESP_INTR_FLAG_LEVEL2,   //2
      .dma_buf_count          = 3, 
      .dma_buf_len            = 300, 
      .use_apll               = false, 
      .tx_desc_auto_clear     = true, 
      .fixed_mclk             = 0, 
      .mclk_multiple          = I2S_MCLK_MULTIPLE_DEFAULT, 
      .bits_per_chan          = I2S_BITS_PER_CHAN_16BIT
  };
    
  i2s_pin_config_t pin_config = {
      .mck_io_num = IIS_MCLK,
      .bck_io_num = IIS_BLCK,                  
      .ws_io_num = IIS_LRCK,              
      .data_out_num = IIS_DOUT, 
      .data_in_num = IIS_DSIN                  
  };
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  i2s_zero_dma_buffer(I2S_NUM_0);
}



 


const char *const dadadadum = "r4:2|g|g|g|eb:8|r:2|f|f|f|d:8|";
const char *const entertainer = "d4:1|d#|e|c5:2|e4:1|c5:2|e4:1|c5:3|c:1|d|d#|e|c|d|e:2|b4:1|d5:2|c:4|";
const char *const prelude = "c4:1|e|g|c5|e|g4|c5|e|c4|e|g|c5|e|g4|c5|e|c4|d|g|d5|f|g4|d5|f|c4|d|g|d5|f|g4|d5|f|b3|d4|g|d5|f|g4|d5|f|b3|d4|g|d5|f|g4|d5|f|c4|e|g|c5|e|g4|c5|e|c4|e|g|c5|e|g4|c5|e|";
const char *const ode = "e4|e|f|g|g|f|e|d|c|c|d|e|e:6|d:2|d:8|e:4|e|f|g|g|f|e|d|c|c|d|e|d:6|c:2|c:8|";
const char *const nyan = "f#5:2|g#|c#:1|d#:2|b4:1|d5:1|c#|b4:2|b|c#5|d|d:1|c#|b4:1|c#5:1|d#|f#|g#|d#|f#|c#|d|b4|c#5|b4|d#5:2|f#|g#:1|d#|f#|c#|d#|b4|d5|d#|d|c#|b4|c#5|d:2|b4:1|c#5|d#|f#|c#|d|c#|b4|c#5:2|b4|c#5|b4|f#:1|g#|b:2|f#:1|g#|b|c#5|d#|b4|e5|d#|e|f#|b4:2|b|f#:1|g#|b|f#|e5|d#|c#|b4|f#|d#|e|f#|b:2|f#:1|g#|b:2|f#:1|g#|b|b|c#5|d#|b4|f#|g#|f#|b:2|b:1|a#|b|f#|g#|b|e5|d#|e|f#|b4:2|c#5|";
const char *const ringtone = "c4:1|d|e:2|g|d:1|e|f:2|a|e:1|f|g:2|b|c5:4|";
const char *const funk = "c2:2|c|d#|c:1|f:2|c:1|f:2|f#|g|c|c|g|c:1|f#:2|c:1|f#:2|f|d#|";
const char *const blues = "c2:2|e|g|a|a#|a|g|e|c2:2|e|g|a|a#|a|g|e|f|a|c3|d|d#|d|c|a2|c2:2|e|g|a|a#|a|g|e|g|b|d3|f|f2|a|c3|d#|c2:2|e|g|e|g|f|e|d|";
const char *const birthday = "c4:3|c:1|d:4|c:4|f|e:8|c:3|c:1|d:4|c:4|g|f:8|c:3|c:1|c5:4|a4|f|e|d|a#:3|a#:1|a:4|f|g|f:8|";
const char *const wedding = "c4:4|f:3|f:1|f:8|c:4|g:3|e:1|f:8|c:4|f:3|a:1|c5:4|a4:3|f:1|f:4|e:3|f:1|g:8|";
const char *const funeral = "c3:4|c:3|c:1|c:4|d#:3|d:1|d:3|c:1|c:3|b2:1|c3:4|";
const char *const punchline = "c4:3|g3:1|f#|g|g#:3|g|r|b|c4|";
const char *const baddy = "c3:3|r|d:2|d#|r|c|r|f#:8|";
const char *const chase = "a4:1|b|c5|b4|a:2|r|a:1|b|c5|b4|a:2|r|a:2|e5|d#|e|f|e|d#|e|b4:1|c5|d|c|b4:2|r|b:1|c5|d|c|b4:2|r|b:2|e5|d#|e|f|e|d#|e|";
const char *const baDing = "b5:1|e6:3|";
const char *const wawawawaa = "e3:3|r:1|d#:3|r:1|d:4|r:1|c#:8|";
const char *const JumpUp = "c5:1|d|e|f|g|";
const char *const jumpDown = "g5:1|f|e|d|c|";
const char *const PowerUp = "g4:1|c5|e|g:2|e:1|g:3|";
const char *const powerDown = "g5:1|d#|c|g4:2|b:1|c5:3|";
int freqTable[] = {31, 33, 35, 37, 39, 41, 44, 46, 49, 52, 55, 58, 62, 65, 69, 73, 78, 82, 87, 92, 98, 104, 110, 117, 123, 131, 139, 147, 156, 165, 175, 185, 196, 208, 220, 233, 247, 262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494, 523, 554, 587, 622, 659, 698, 740, 784, 831, 880, 932, 988, 1047, 1109, 1175, 1245, 1319, 1397, 1480, 1568, 1661, 1760, 1865, 1976, 2093, 2217, 2349, 2489, 2637, 2794, 2960, 3136, 3322, 3520, 3729, 3951, 4186};





void Music::playMusic(Melodies melodies, MelodyOptions options)
{
  buzzMelody.currentDuration = 4; //Default duration (Crotchet)
  buzzMelody.currentOctave = 4;   //Middle octave
  buzzMelody.beatsPerMinute = 15;
  buzzMelody.melody = String(getMelody(melodies));  
  buzzMelody.opt = options;
  buzzMelody._stopPlayTone = false;
  if(buzzMelody.melody == "null")
    return;
  _playing = true;
  xTaskCreatePinnedToCore(Music::taskLoop, "taskLoop", 2*1024, this, 5, &buzzMelody.task, ARDUINO_RUNNING_CORE);
  if(options == Once || options == Forever){
    while(_playing){
      vTaskDelay(1);
    }
  }else{
    return;
  }
}

void Music::playTone(int freq, int beat)
{
  size_t bbytes_read;
  int16_t buf[2];
  const int SAMPLE_RATE = 8000; 
  uint32_t clk = i2s_get_clk(I2S_NUM_0);
  i2s_set_sample_rates(I2S_NUM_0,8000);
  for(int i = 0; i < beat; ++i) {
    int16_t sample = (int16_t)(32767.0 * sin(i * TWO_PI * freq / SAMPLE_RATE));
    buf[0] = sample; // Left channel
    buf[1] = sample; // Right channel
    i2s_write(I2S_NUM_0, (char*)buf, sizeof(buf), &bbytes_read, portMAX_DELAY);
  }
  i2s_zero_dma_buffer(I2S_NUM_0);
  i2s_set_sample_rates(I2S_NUM_0,clk);
}

void Music::playNextNote(String tone)
{
  // cache elements
  String currNote = tone;
  int currentDuration = buzzMelody.currentDuration;
  int currentOctave = buzzMelody.currentOctave;
  String currLen = currNote;
  int note = 0;
  boolean isrest = false;
  int beatPos;
  boolean parsingOctave = true;
  for(int pos = 0; pos < currLen.length(); pos++){
    char noteChar = currNote[pos];
    switch (noteChar){
    case 'c':
    case 'C':
      note = 1;
      break;
    case 'd':
    case 'D':
      note = 3;
      break;
    case 'e':
    case 'E':
      note = 5;
      break;
    case 'f':
    case 'F':
      note = 6;
      break;
    case 'g':
    case 'G':
      note = 8;
      break;
    case 'a':
    case 'A':
      note = 10;
      break;
    case 'b':
    case 'B':
      note = 12;
      break;
    case 'r':
    case 'R':
      isrest = true;
      break;
    case '#':
      note++;
      break;
    //case 'b': note--; break;
    case ':':
      parsingOctave = false;
      beatPos = pos;
      break;
    default:
      if (parsingOctave)
        currentOctave = (int)currNote[pos] - 48;
    }
  }
  if (!parsingOctave){
    currentDuration = (int)currNote[beatPos + 1] - 48;
  }
  int beat = (60000 / buzzMelody.beatsPerMinute) / 4;//
  if (isrest){
  }else{
    int keyNumber = note + (12 * (currentOctave - 1));
    int frequency = keyNumber >= 0 && keyNumber < sizeof(freqTable) / sizeof(freqTable[0]) ? freqTable[keyNumber] : 0;
    playTone(frequency, currentDuration * beat);
  }
  buzzMelody.currentDuration = currentDuration;
  buzzMelody.currentOctave = currentOctave;
}

const char *Music::getMelody(Melodies melody)
{
  switch (melody){
  case DADADADUM:
    return dadadadum;
  case ENTERTAINER:
    return entertainer;
  case PRELUDE:
    return prelude;
  case ODE:
    return ode;
  case NYAN:
    return nyan;
  case RINGTONE:
    return ringtone;
  case FUNK:
    return funk;
  case BLUES:
    return blues;
  case BIRTHDAY:
    return birthday;
  case WEDDING:
    return wedding;
  case FUNERAL:
    return funeral;
  case PUNCHLINE:
    return punchline;
  case BADDY:
    return baddy;
  case CHASE:
    return chase;
  case BA_DING:
    return baDing;
  case WAWAWAWAA:
    return wawawawaa;
  case JUMP_UP:
    return JumpUp;
  case JUMP_DOWN:
    return jumpDown;
  case POWER_UP:
    return PowerUp;
  case POWER_DOWN:
    return powerDown;
  default:
    return "null";
  }
}

void Music::stopPlayTone(void)
{
  if (_playing){ // If background playback is detected, remove the background task
    _playing = false;
    if (buzzMelody.task){
      buzzMelody._stopPlayTone = true;
      buzzMelody.task = NULL;
    }
    i2s_zero_dma_buffer(I2S_NUM_0);// Clear data in I2S to prevent strange noises  
  }else{ 
  }
}

void taskPlayMusic(void *arg)
{
  playMusicState = 1;
  _stopState = 0;
  const char* path = (const char*) arg;
  xSemaphoreTake(xI2SMutex, portMAX_DELAY);
  lv_fs_file_t file;
  lv_fs_res_t ret = 0;
  int sampleRate = 0;
  uint8_t buffer[1024]={0};
  uint32_t readbyt = 0;
  size_t bytes_read;
  uint32_t clk = i2s_get_clk(I2S_NUM_0);
  xSemaphoreTake(xSPIlMutex, portMAX_DELAY);
  ret = lv_fs_open(&file,path,LV_FS_MODE_RD);
  xSemaphoreGive(xSPIlMutex);
  if(ret != LV_FS_RES_OK){
    printf("open file error\n");
    xSemaphoreGive(xI2SMutex);
    vTaskDelete(NULL);
    return;
  }
  if(lv_fs_read(&file,buffer, 44,NULL) != LV_FS_RES_OK){
    printf("read head error!!\n");
    lv_fs_close(&file);
    xSemaphoreGive(xI2SMutex);
    vTaskDelete(NULL);
    return; 
  }
  sampleRate = buffer[24] | (buffer[25] << 8) | (buffer[26] << 16) | (buffer[27] << 24);
  //bitDepth = buffer[34] | (buffer[35] << 8);
  i2s_set_sample_rates(I2S_NUM_0,sampleRate);
  while(1){
    xSemaphoreTake(xSPIlMutex, portMAX_DELAY);
    ret = lv_fs_read(&file,buffer, 1024,&readbyt);
    xSemaphoreGive(xSPIlMutex);
    if(ret != LV_FS_RES_OK){
      break;
    }else{
      if(readbyt < 1024){
        i2s_zero_dma_buffer(I2S_NUM_0);
        break;
      }else{
        i2s_write(I2S_NUM_0, (char*)buffer, sizeof(buffer), &bytes_read, portMAX_DELAY);
      }
    }
    if(_stopMusic == 1){
      _stopState = 1;
      _stopMusic = 0;
      break;
    }
  }
  _stopState = 1;
  lv_fs_close(&file);
  i2s_zero_dma_buffer(I2S_NUM_0);
  i2s_set_sample_rates(I2S_NUM_0,clk);
  xSemaphoreGive(xI2SMutex);
  playMusicState = 0;
  vTaskDelete(NULL);
}

void Music::playTFCardAudio(const char* path)
{
  xTaskCreatePinnedToCore(&taskPlayMusic, "taskMusic", 4*1024, (void*)path, 5, NULL, ARDUINO_RUNNING_CORE);
}

void Music::playTFCardAudio(String path)
{
  this->playTFCardAudio(path.c_str());
}

void Music::stopPlayAudio(void)
{
  if(_stopState == 0){
    _stopMusic= 1;
  }
}

void Music::recordSaveToTFCard(const char* path, uint8_t time)
{
  lv_fs_res_t ret = 0;
  size_t bytes_read;
  lv_fs_file_t phFile;
  char *_recordBuf=(char *)heap_caps_malloc(6400 * sizeof(char), MALLOC_CAP_SPIRAM);//MALLOC_CAP_SPIRAM;
  if(_recordBuf == NULL){
    printf("record save to tfcard malloc error");
    return;
  }
  digital_write(eAmp_Gain,1);
  byte header[44];  // WAVE file header
  //char communicationData[32];  // Temporary storage for collected data
  int byteRate = 64000;  // Number of bytes collected per second, calculated as: sample rate x number of channels x bits per sample / 8
  int waveDataSize = time * byteRate;  // Total bytes of data
  int numCommunicationData = 6400;  // Amount of data collected each time, 32 bytes
  int len = waveDataSize / numCommunicationData;

  xSemaphoreTake(xSPIlMutex, portMAX_DELAY);  // Take SPI mutex before writing file header
  ret = lv_fs_open(&phFile, path, LV_FS_MODE_WR);
  xSemaphoreGive(xSPIlMutex);

  if (ret != LV_FS_RES_OK) {
    printf("Failed to open file for writing\n");
    return;
  }
    
  // createWavHeader(header, waveDataSize, 44100, 2, byteRate, 4);
  createWavHeader(header, waveDataSize, 16000, 2, byteRate, 4);  // Create WAV file header

  xSemaphoreTake(xSPIlMutex, portMAX_DELAY);  // Take SPI mutex before writing file header
  lv_fs_write(&phFile, (uint8_t *)&header, sizeof(header), NULL);
  xSemaphoreGive(xSPIlMutex);
  // i2s_set_sample_rates(I2S_NUM_0, 44100);  // Set I2S frequency to 44.1k
 
  xSemaphoreTake(xI2SMutex, portMAX_DELAY);
  for (int j = 0; j < len; ++j) {
    i2s_read(I2S_NUM_0, _recordBuf, numCommunicationData, &bytes_read, portMAX_DELAY);
    xSemaphoreTake(xSPIlMutex, portMAX_DELAY);  // Take SPI mutex before writing data
    lv_fs_write(&phFile, (uint8_t *)_recordBuf, numCommunicationData, NULL);
    xSemaphoreGive(xSPIlMutex);
  }
  

  xSemaphoreTake(xSPIlMutex, portMAX_DELAY);  // Take SPI mutex before closing file
  lv_fs_close(&phFile);  // Close file
  xSemaphoreGive(xSPIlMutex);

  xSemaphoreGive(xI2SMutex);
  digital_write(eAmp_Gain,0);
  heap_caps_free(_recordBuf);
}


void Music::recordSaveToTFCard(String path,  uint8_t time)
{
  this->recordSaveToTFCard(path.c_str(),time);
}


void UNIHIKER_K10::writeCommand(uint8_t addr,uint8_t arg, uint8_t cmd)
{
  _pWire->beginTransmission(addr);
  _pWire->write(arg);
  _pWire->write(cmd);
  _pWire->endTransmission();
}

void Music::createWavHeader(byte* header, int totalDataLen, int longSampleRate, uint8_t channels, int byteRate, uint8_t blockAlign)
{
  header[0] = 'R';
  header[1] = 'I';
  header[2] = 'F';
  header[3] = 'F';
  unsigned int fileSizeMinus8 = totalDataLen + 44 - 8;
  header[4] = (byte)(fileSizeMinus8 & 0xFF);
  header[5] = (byte)((fileSizeMinus8 >> 8) & 0xFF);
  header[6] = (byte)((fileSizeMinus8 >> 16) & 0xFF);
  header[7] = (byte)((fileSizeMinus8 >> 24) & 0xFF);
  header[8] = 'W';
  header[9] = 'A';
  header[10] = 'V';
  header[11] = 'E';
  header[12] = 'f';
  header[13] = 'm';
  header[14] = 't';
  header[15] = ' ';
  header[16] = 0x10;  // linear PCM
  header[17] = 0x00;
  header[18] = 0x00;
  header[19] = 0x00;
  header[20] = 0x01;  // linear PCM
  header[21] = 0x00;
  //monoral
  header[22] = (byte)(channels & 0xff);  
  header[23] = 0x00;
   //采样率
  header[24] = (byte) (longSampleRate & 0xff);
  header[25] = (byte) ((longSampleRate >> 8) & 0xff);
  header[26] = (byte) ((longSampleRate >> 16) & 0xff);
  header[27] = (byte) ((longSampleRate >> 24) & 0xff);
  // byte Rate
  header[28] = (byte) (byteRate & 0xff);
  header[29] = (byte) ((byteRate >> 8) & 0xff);
  header[30] = (byte) ((byteRate >> 16) & 0xff);
  header[31] = (byte) ((byteRate >> 24) & 0xff);
  // block align
  header[32] = (byte)(blockAlign & 0xff); 
  header[33] = 0x00;
  // bits per sample
  header[34] = 0x10;  
  header[35] = 0x00;
  header[36] = 'd';
  header[37] = 'a';
  header[38] = 't';
  header[39] = 'a';
  header[40] = (byte)(totalDataLen & 0xFF);
  header[41] = (byte)((totalDataLen >> 8) & 0xFF);
  header[42] = (byte)((totalDataLen >> 16) & 0xFF);
  header[43] = (byte)((totalDataLen >> 24) & 0xFF);
}

uint8_t UNIHIKER_K10::readData( uint8_t addr, uint8_t arg, void* pBuf, size_t size)
{
  uint8_t ret = 0;
  if(pBuf == NULL){
    printf("pBuf ERROR!! : null pointer");
    return 0;
  }
  uint8_t *_pBuf = (uint8_t *)pBuf;
  _pWire->beginTransmission(addr);
  _pWire->write(arg);
  if(_pWire->endTransmission() != 0){
      return 0;
  }
  ret = _pWire->requestFrom(addr, (uint8_t) size);
  for(uint16_t i = 0; i < size; i++){
    _pBuf[i] = (char)_pWire->read();
  }
  return ret;
}

//static uint16_t lastAlsValue = 0;
uint16_t UNIHIKER_K10::readALS(void)
{
  uint8_t buf[4];
  uint16_t _als_ch1  = 0;
  uint16_t _als_ch0  = 0;
  uint16_t _als  = 0;
  float _ratio = 0;

  if(readData(LTR303ALS_ADDR,LTR303_DATA_CH1_0,buf,4) == 4){
    _als_ch1 = (buf[1] << 8) | buf[0]; // 0x89 + 0x88
    _als_ch0 = (buf[3] << 8) | buf[2]; // 0x8B + 0x8A
    _ratio = _als_ch1/(_als_ch0+_als_ch1);
    if(_ratio<0.45){
      _als = (1.7743*_als_ch0 + 1.1059*_als_ch1);
    }else if(_ratio<0.64 && _ratio>=0.45){
      _als = (4.2785*_als_ch0 - 1.9548*_als_ch1);
    }else if(_ratio<0.85 && _ratio>=0.64){
      _als = (0.5926*_als_ch0 + 0.1185*_als_ch1);
    }else{
      _als = 0;
    }
  }
  return _als;
}

uint64_t UNIHIKER_K10::readMICData(void)
{
  int64_t MICData= 0;
  size_t bbytes_read;
  uint8_t dataBuf[2*2*6];
  int64_t dataBuf1[6];
  i2s_read(I2S_NUM_0,dataBuf,sizeof(dataBuf),&bbytes_read,portMAX_DELAY);
  #if 1
  for(uint8_t i = 1; i < 6;){
    dataBuf1[i] = dataBuf[i*2] << 8 | dataBuf[i*2+1];
    dataBuf1[i] *= dataBuf1[i];
    MICData += dataBuf1[i];
    i+=2;
  }
  
  MICData = MICData / 6;
  MICData = sqrt(MICData);
  #else
  for(uint8_t i = 1; i < 50;i++){
    dataBuf1[i] = dataBuf[i*4] << 8 | dataBuf[i*4+1];
    Serial.println(dataBuf1[i]);
    dataBuf1[i] = (dataBuf1[i] +32788) *1024 /65536;
    //Serial.println(dataBuf1[i]);
    MICData += dataBuf1[i];
  }
  Serial.println("--------------------");
  #endif
  return (uint64_t)MICData;
}

void UNIHIKER_K10::initSC7A20H(void)
{
  uint8_t data=0;
  uint8_t buf[2];
  readData(_sc7a20hAddr, 0x24, buf, 1);
  data = buf[0];
  data |= 0x08;
  writeCommand(_sc7a20hAddr, 0x24, data);
  //AOI1 CONFIG
  data = 0x00;
  data |= 0x40;
  data |= 0x03;
  data |= 0x0c;
  data |= 0x38;
  writeCommand(_sc7a20hAddr, 0x30, data);

  readData(_sc7a20hAddr, 0x21, buf, 1);
  data = buf[0];
  data |= 0x81;
  writeCommand(_sc7a20hAddr, 0x21, data);

  data = 0x60;
  writeCommand(_sc7a20hAddr, 0x32, data);

  data = 0x02;
  writeCommand(_sc7a20hAddr, 0x33, data);

  readData(_sc7a20hAddr, 0x22, buf, 1);
  data = buf[0];
  data |= 0x40;
  writeCommand(_sc7a20hAddr, 0x21, data);

  readData(_sc7a20hAddr, 0x24, buf, 1);
  data = buf[0];
  data |= 0x02;
  writeCommand(_sc7a20hAddr, 0x24, data);

  readData(_sc7a20hAddr, 0x25, buf, 1);
  data = buf[0];
  data |= 0x02;
  writeCommand(_sc7a20hAddr, 0x25, data);

  data = 0x00;
  data |= 0xc0;
  data |= 0x3f;
  writeCommand(_sc7a20hAddr, 0x34, data);

  readData(_sc7a20hAddr, 0x21, buf, 1);
  data = buf[0];
  data |= 0xfd;
  writeCommand(_sc7a20hAddr, 0x21, data);

  data = 0x18;
  writeCommand(_sc7a20hAddr, 0x36, data);

  data = 0x02;
  writeCommand(_sc7a20hAddr, 0x37, data);

  readData(_sc7a20hAddr, 0x25, buf, 1);
  data = buf[0];
  data |= 0x20;
  writeCommand(_sc7a20hAddr, 0x21, data);

  readData(_sc7a20hAddr, 0x23, buf, 1);
  data = 0x80;
  data |= 0x08;
  data |= 0x00;
  writeCommand(_sc7a20hAddr, 0x23, data);

  //power
  data = 0x27;
  writeCommand(_sc7a20hAddr, 0x20, data);
}

int UNIHIKER_K10::getAccelerometerX(void)
{
  return this->accX;
}

int UNIHIKER_K10::getAccelerometerY(void)
{
  return this->accY;
}

int UNIHIKER_K10::getAccelerometerZ(void)
{
  return this->accZ;
}

int UNIHIKER_K10::getStrength(void)
{
  int x = getAccelerometerX();
  int y = getAccelerometerY();
  int z = getAccelerometerZ();
  return sqrt(x * x + y * y + z * z);
}

bool UNIHIKER_K10::isGesture(Gesture gesture)
{
  bool ret = false;
  switch(gesture){
    case Shake:
      if(_gesture == Shake){
        ret = true;
        _gesture = GestureNone;
      }
    break;
    case ScreenUp :
      if(_gesture == ScreenUp){
        ret = true;
        _gesture = GestureNone;
      }
    break;
    case ScreenDown :
      if(_gesture == ScreenDown){
        ret = true;
        _gesture = GestureNone;
      }
    break;
    case TiltLeft :
      if(_gesture == TiltLeft){
        ret = true;
        _gesture = GestureNone;
      }
    break;
    case TiltRight :
      if(_gesture == TiltRight){
        ret = true;
        _gesture = GestureNone;
      }
    break;
    case TiltForward :
      if(_gesture == TiltForward){
        ret = true;
        _gesture = GestureNone;
      }
    break;
    case TiltBack:
      if(_gesture == TiltBack){
        ret = true;
        _gesture = GestureNone;
      }
    break;
    default:
    break;
  }
  return ret;
}


void UNIHIKER_K10::canvasDrawCode(String code)
{
  if(!qrCode){
    if(!_cam){
      qrCode =  lv_qrcode_create(this->_scr, 230, lv_color_hex3(0x000000), lv_color_hex3(0xeef));
    }else{
      qrCode =  lv_qrcode_create(_cam, 230, lv_color_hex3(0x000000), lv_color_hex3(0xeef));
    }
  }
  xSemaphoreTake(xLvglMutex, portMAX_DELAY);// Added with exception handling for deletion
  lv_qrcode_update(qrCode, code.c_str(), strlen(code.c_str()));
  lv_obj_center(qrCode);
  xSemaphoreGive(xLvglMutex);
}

void  UNIHIKER_K10::clearCode(void)
{
  xSemaphoreTake(xLvglMutex, portMAX_DELAY);
  if(qrCode){
    lv_qrcode_delete(qrCode);
    delay(10);
    qrCode = NULL;
    lv_task_handler();
  }
  xSemaphoreGive(xLvglMutex);
}

uint8_t UNIHIKER_K10::getPlayMusicState(void)
{
  return playMusicState;
}