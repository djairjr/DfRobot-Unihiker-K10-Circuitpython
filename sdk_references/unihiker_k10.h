/*!
 * @file unihiker_k10.h
 * @brief This is the driver library for UniHiker K10
 * @copyright   Copyright (c) 2010 DFRobot Co.Ltd (http://www.dfrobot.com)
 * @license     The MIT License (MIT)
 * @author [TangJie](jie.tang@dfrobot.com)
 * @version  V1.0
 * @date  2024-6-21 
 */

#ifndef _UNIHIKER_K10_H_
#define _UNIHIKER_K10_H_
#include "DFRobot_AHT20.h"

#include "Arduino.h"
#include "who_camera.h"
#include "Adafruit_NeoPixel.h"
#include <Wire.h>
#include "lvgl.h"
#include "driver/i2s.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ai.hpp"
#include "initBoard.h"
#include "app_wifi.h"
#include "app_httpd.hpp"
#include "app_mdns.h"
#include "SD.h"
#include "../TFT_eSPI/TFT_eSPI.h"
#include "GT30L24A3W.h"
#include "esp_code_scanner.h"
#include "lv_qrcode.h"



#define PIXEL_PIN          46
#define PIXEL_COUNT        3 

#define IIS_BLCK            0
#define IIS_LRCK            38
#define IIS_DSIN            39
#define IIS_DOUT            45
#define IIS_MCLK            3

#define MSA311_ADDR          0x62

#define LTR303ALS_ADDR       0x29
#define LTR303ALS_STATUS     0x8C
#define LTR303ALS_CTRL       0x80
#define LTR303ALS_GAIN_MODE  0x01//GAIN_1 MODE_ACTIVE
#define LTR303ALS_MEAS_RATE  0x85
#define LTR303ALS_INTEG_RATE 0x03//INTEGTIME_100 MEASRATE_500   
#define LTR303_DATA_CH1_0    0x88
#define LTR303_DATA_CH1_1    0x89
#define LTR303_DATA_CH0_0    0x8A
#define LTR303_DATA_CH0_1    0x8B

// Print free stack space
#define STACKSPACE 
#ifdef  STACKSPACE
#define STACKSPACE_LVGL       0// LVGL task stack remaining space
#define STACKSPACE_CAMDISPLAY 0 // Remaining stack space for camera display task

#define STACKSPACE_COLLECT    0 // Remaining stack space for I2S microphone collection task

#define STACKSPACE_ANALYSIS   0 // Remaining stack space for recognition task


#endif

extern SemaphoreHandle_t xI2SMutex;

typedef void (*CBFunc)(void);

class Canvas{
public:
    typedef enum{
        eCNAndENFont24,
        eJapanFont24x24,
        eKoreanFont24x24,
        eGreeceFont12x24,
        eCNAndENFont16,
    }eFontSize_t;
    
    /**
     * @fn Canvas
     * @brief Canvas constructor
     * @param obj Object to construct
     * @param dir Creation direction
     */
    Canvas(void *obj, int dir);

    /**
     * @fn updateCanvas
     * @brief Update screen display Note: Automatically updates when using the camera
     */
    void updateCanvas(void);

    /**
     * @fn canvasClear
     * @brief Clear the canvas Note: Clears properties on the canvas
     */
    void canvasClear(void);

    /**
     * @fn canvasDrawBitmap
     * @brief Display a local image on the canvas
     * @param x Display coordinate X
     * @param y Display coordinate Y
     * @param w Image width
     * @param h Image height
     * @param bitmap Image data
     */
    void canvasDrawBitmap(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* bitmap);

    /**
     * @fn canvasDrawImage
     * @brief Load an image from the SD card
     * @param x Display coordinate X
     * @param y Display coordinate Y
     * @param dir Image path
     */
    void canvasDrawImage(int16_t x, int16_t y, const void *dir);
    void canvasDrawImage(int16_t x, int16_t y, String imagePath);

    /**
     * @fn canvasText
     * @brief Display text on the canvas
     * @param text Text content
     * @param row Row
     * @param color Display color
     */
    void canvasText(float text, uint8_t row, uint32_t color);
    void canvasText(String text, uint8_t row, uint32_t color);
    void canvasText(const char* text, uint8_t row, uint32_t color);

    /**
     * @fn canvasClear
     * @brief Clear text on the canvas
     * @param row Row to clear
     */
    void canvasClear(uint8_t row);

    /**
     * @fn clearLocalCanvas
     * @brief Clear partial content on the canvas
     * @param obj Canvas object
     * @param x Starting X coordinate
     * @param y Starting Y coordinate
     * @param w Length
     * @param h Width
     */
    void clearLocalCanvas(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

    /**
     * @fn canvasText
     * @brief Display text on the canvas
     * @param text Text content
     * @param x Display coordinate X
     * @param y Display coordinate Y
     * @param color Display color
     * @param font Font size
     * @param count Line break count
     * //Needs discussion
     */
    void canvasText(float text, int16_t x, int16_t y, uint32_t color, eFontSize_t font, int count, bool autoClean);
    void canvasText(String text, int16_t x, int16_t y, uint32_t color, eFontSize_t font, int count, bool autoClean);
    void canvasText(const char* text, int16_t x, int16_t y, uint32_t color, eFontSize_t font, int count, bool autoClean);

    /**
     * @fn canvasPoint
     * @brief Draw a point
     * @param x Display coordinate X
     * @param y Display coordinate Y
     * @param color Display color
     */
    void canvasPoint(int16_t x, int16_t y, uint32_t color);

    /**
     * @fn canvasSetLineWidth
     * @brief Set line width
     * @param w Line width
     */
    void canvasSetLineWidth(uint8_t w = 10);

    /**
     * @fn canvasLine
     * @brief Draw a line
     * @param x1, y1 Starting coordinates
     * @param x2, y2 Ending coordinates
     * @param color Color
     */
    void canvasLine(int x1, int y1, int x2, int y2, uint32_t color);

    /**
     * @fn canvasCircle
     * @brief Draw a circle
     * @param x Display coordinate X
     * @param y Display coordinate Y
     * @param r Circle radius
     * @param color Border color
     * @param bg_color Background color
     * @param fill Whether to fill
     */
    void canvasCircle(int x, int y, int r, uint32_t color, uint32_t bg_color, bool fill);

    /**
     * @fn canvasRectangle
     * @brief Draw a rectangle
     * @param x Display coordinate X
     * @param y Display coordinate Y
     * @param w Rectangle width
     * @param h Rectangle height
     * @param color Border color
     * @param bg_color Background color
     * @param fill Whether to fill
     */
    void canvasRectangle(int x, int y, int w, int h, uint32_t color, uint32_t bg_color, bool fill);


    

private:
    int _lineW = 5;// Line width
    void insertLineBreaks(const char* src, char* dest, int charPerLine);

    
};

class AHT20 : public DFRobot_AHT20
{
public:
    enum eAHT20Data_t
    {
        eAHT20TempC,
        eAHT20TempF,
        eAHT20HumiRH,
    };
    /**
     * @fn AHT20
     * @brief AHT20 constructor
     * @param wire Communication properties
     */
    AHT20(TwoWire &wire = Wire);

    /**
     * @fn getData
     * @brief Get AHT20 data
     * @param type Data selection
     */
    float getData(eAHT20Data_t type);

private:
    static void taskLoop(void *param)
    {
        AHT20 *self = (AHT20 *)param;
        uint8_t status;

        while((status = self->begin()) != 0){
            printf("AHT20 sensor initialization failed.\n");
            delay(1000);
            break;
        }
        TickType_t xLastWakeTime;
        const TickType_t xFrequency = pdMS_TO_TICKS(1000); // Specify the task to run every 1000ms
        while (1)
        {
            if(self->startMeasurementReady(true)){
            }
           vTaskDelayUntil(&xLastWakeTime, xFrequency);
        }
    }

};


class Button
{
public:
    /**
     * @fn Button
     * @brief Constructor for a single button
     * @param _io Button pin
     */
    Button(uint8_t _io);

    /**
     * @fn Button
     * @brief Constructor for a combination of buttons
     * @param _io1 Button pin 1
     * @param _io2 Button pin 2
     */
    Button(uint8_t _io1, uint8_t _io2);

    /**
     * @fn isPressed
     * @brief Check if the button is pressed
     */
    bool isPressed(void);

    /**
     * @fn setPressedCallback
     * @brief Set callback function for button press
     * @param _cb Callback function
     */
    void setPressedCallback(CBFunc _cb);

    /**
     * @fn setUnPressedCallback
     * @brief Set callback function for button release
     * @param _cb Callback function
     */
    void setUnPressedCallback(CBFunc _cb);


private:
    CBFunc pressedCb;
    CBFunc unpressedCb;
    uint8_t io;
    uint8_t io1;
    uint8_t io2;
    uint8_t pinNum;
    bool prevState;

    static void taskLoop(void *param)
    {
        Button *self = (Button *)param;
        while (1)
        {
            bool isPressed = self->isPressed();
            if (self->prevState != isPressed)
            {
                if (isPressed && self->pressedCb)
                {
                    self->pressedCb();
                }
                if ((!isPressed) && self->unpressedCb)
                {
                    self->unpressedCb();
                }
                self->prevState = isPressed;
            }
            vTaskDelay(1);
        }
    }
};

class RGB : public Adafruit_NeoPixel
{
public:
    /**
     * @fn RGB
     * @brief Constructor for RGB lights
     * @param pin Pin configuration
     * @param num Number of lights
     * @param bright Brightness of lights
     * @param type Display type
     */
    RGB(uint8_t pin = PIXEL_PIN, uint16_t num = PIXEL_COUNT, uint8_t bright = 5, neoPixelType type = NEO_GRB + NEO_KHZ800);

    /**
     * @fn write
     * @brief Set RGB color for a specific light
     * @param index Light index
     * @param r Red component
     * @param g Green component
     * @param b Blue component
     */
    void write(int8_t index, uint8_t r, uint8_t g, uint8_t b);

    /**
     * @fn write
     * @brief Set RGB color for a specific light
     * @param index Light index
     * @param color Color value
     */
    void write(int8_t index, uint32_t color) { write(index, 0xff & (color >> 16), 0xff & (color >> 8), 0xff & color); }

    /**
     * @fn setRangeColor
     * @brief Set color for a range of lights
     * @param start Starting light index
     * @param end Ending light index
     * @param c Color value
     */
    void setRangeColor(int16_t start, int16_t end, uint32_t c);

    void show(void);

    /**
     * @fn brightness
     * @brief Set brightness of the lights
     * @param b Brightness value
     */
    void brightness(uint8_t b);

    uint8_t brightness();

private:
    uint32_t rgbToColor(uint8_t r, uint8_t g, uint8_t b);
    int _brightness;
    uint32_t c[3];
};






enum Melodies {
        DADADADUM = 0,
        ENTERTAINER,
        PRELUDE,
        ODE,
        NYAN,
        RINGTONE,
        FUNK,
        BLUES,
        BIRTHDAY,
        WEDDING,
        FUNERAL,
        PUNCHLINE,
        BADDY,
        CHASE,
        BA_DING,
        WAWAWAWAA,
        JUMP_UP,
        JUMP_DOWN,
        POWER_UP,
        POWER_DOWN,
};

enum MelodyOptions {
    Once = 1, // Play once
    Forever = 2, // Loop indefinitely
    OnceInBackground = 4, // Play once in the background
    ForeverInBackground = 8 // Loop indefinitely in the background
};



class Music{
public:
    
    /**
     * @fn Music
     * @brief Music constructor
     */
    Music(void){};

    /**
     * @fn playMusic
     * @brief Play built-in music in the background
     * @param melodies Music selection
     * @param options Playback options
     */
    void playMusic(Melodies melodies, MelodyOptions options = OnceInBackground);

    /**
     * @brief Play specified tone
     * @param frequency Frequency of the tone
     * @param samples Beats: 1 beat = 8000, 1/2 beat = 4000, and so on
     */
    void playTone(int freq, int beat);

    /**
     * @fn stopPlayTone
     * @brief Stop tone playback
     */
    void stopPlayTone(void);

    /**
     * @fn playTFCardAudio
     * @brief Play music from SD card
     * @param path File path
     */
    void playTFCardAudio(const char* path);
    void playTFCardAudio(String path);

    /**
     * @fn stopPlayAudio
     * @brief Stop playing music from SD card
     */
    void stopPlayAudio(void);

    /**
     * @fn recordSaveToTFCard
     * @brief Record audio to SD card
     * @param path Storage path
     * @param time Recording duration
     */
    void recordSaveToTFCard(const char* path,  uint8_t time);
    void recordSaveToTFCard(String path,  uint8_t time);
    


private:
    int16_t  _pin;
    uint16_t _freq;
    bool     _on;
    bool     _playing;
    float ticks;
    float tempo;
    

    struct buzzMelody_t
    {
        String melody;
        int currentDuration;
        int currentOctave;
        int beatsPerMinute;
        MelodyOptions opt;
        TaskHandle_t task;
        uint8_t _stopPlayTone;
    } buzzMelody;

    void  playNextNote(String tone);
    const char* getMelody(Melodies melody);
    void createWavHeader(byte* header, int totalDataLen, int longSampleRate, uint8_t channels, int byteRate, uint8_t blockAlign);
    static void taskLoop(void *param){
        Music *self = (Music *)param;
        self->_playing = true;
        int head=0, trail=0, index=0;
        xSemaphoreTake(xI2SMutex, portMAX_DELAY);
        while (self->_playing) {
            
            for(; index < self->buzzMelody.melody.length(); index++){
                if(self->buzzMelody.melody[index] == '|'){
                    trail = index;
                    self->playNextNote(self->buzzMelody.melody.substring(head, trail));
                    index++;
                    head = index;
                    break;
                }
            }
            if (head >= self->buzzMelody.melody.length()) {
                if(self->buzzMelody.opt == Forever || 
                    self->buzzMelody.opt == ForeverInBackground) {
                    head=0, trail=0, index=0;
                }else{
                    break;
                }
            }
            if(self->buzzMelody._stopPlayTone){
                i2s_zero_dma_buffer(I2S_NUM_0); // Clear data in I2S to prevent strange noises
                break;
            }
        }
        xSemaphoreGive(xI2SMutex);

        self->_playing = false;
        self->buzzMelody.task = NULL;
        vTaskDelete(NULL); // Delete the task after playback completion
    }

};

 
enum Gesture {
    Shake = 0,          // Shake gesture
    ScreenDown = 1,     // Screen facing down
    ScreenUp = 2,       // Screen facing up
    TiltLeft = 3,       // Tilt left
    TiltRight = 4,      // Tilt right
    TiltBack = 5,       // Tilt backward
    TiltForward = 6,    // Tilt forward
    GestureNone = 7     // No gesture detected
};


/**
 * @fn unhiker K10
 */
class UNIHIKER_K10{
public:
    Canvas *canvas = NULL;
    RGB *rgb = NULL;
    Button *buttonA = NULL, *buttonB = NULL, *buttonAB = NULL;
    /**
     * @fn UNIHIKER_K10
     * @brief UNIHIKER K10 board constructor
     */
    UNIHIKER_K10(TwoWire &wire = Wire);

    /**
     * @fn ~UNIHIKER_K10
     * @brief UNIHIKER K10 board destructor
     */
    ~UNIHIKER_K10(void);

    /**
     * @fn begin
     * @brief Initialize UNIHIKER K10 board
     */
    void begin(void);

    /**
     * @fn initScreen
     * @brief Initialize the screen
     * @param dir Screen orientation
     * @param frame Screen display frame rate
     */
    void initScreen(int dir = 2, int frame = 0);

    /**
     * @fn setScreenBackground
     * @brief Set the screen background color
     * @param color Background color
     */
    void setScreenBackground(uint32_t color);

    /**
     * @fn creatCanvas
     * @brief Create a canvas
     */
    void creatCanvas(void);

    /**
     * @fn initBgCamerImage
     * @brief Initialize the camera image
     */
    void initBgCamerImage(void);

    /**
     * @fn setBgCamerImage
     * @brief Set whether to display the camera image on the screen
     * @param sta Configuration status
     */
    void setBgCamerImage(bool sta = true);

    /**
     * @fn initSDFile
     * @brief Initialize the SD card file system
     */
    void initSDFile(void);

    /**
     * @fn photoSaveToTFCard
     * @brief Save a photo to the SD card
     * @param imagePath Path to save the photo
     */
    void photoSaveToTFCard(const char *imagePath);
    void photoSaveToTFCard(String imagePath);

    /**
     * @brief Get the ambient light intensity
     * @return uint16_t Ambient light intensity
     */
    uint16_t readALS(void);

    /**
     * @brief Get the UV light intensity
     * @return uint16_t UV light intensity
     */
    //uint16_t readUVS(void);

    /**
     * @brief Read microphone data
     * @return int16_t Microphone data
     */
    uint64_t readMICData(void);

    /**
     * @fn getAccelerometerX
     * @brief Get accelerometer X-axis data
     */
    int getAccelerometerX(void);

    /**
     * @fn getAccelerometerY
     * @brief Get accelerometer Y-axis data
     */
    int getAccelerometerY(void);

    /**
     * @fn getAccelerometerZ
     * @brief Get accelerometer Z-axis data
     */
    int getAccelerometerZ(void);

    /**
     * @fn getStrength
     * @brief Get strength
     */
    int getStrength(void);

    /**
     * @fn isGesture
     * @brief Detect gesture
     * @param gesture Gesture state
     */
    bool isGesture(Gesture gesture);

    /**
     * @fn canvasDrawCode
     * @brief Display QR code
     * @param code QR code to display
     */
    void canvasDrawCode(String code);

    /**
     * @fn clearCode
     * @brief Clear QR code from display
     */
    void clearCode(void);

    /**
     * @fn getPlayMusicState
     * @brief get play music state
     * @return return play state
     * @retval 0 stop
     * @retval 1 start
     */

    uint8_t getPlayMusicState(void);

    uint8_t readData(uint8_t addr,uint8_t arg, void* pBuf, size_t size);
    int accX;
    int accY;
    int accZ;
    void initI2S(void);

private:
    
    void initSC7A20H(void);
    void writeCommand(uint8_t addr,uint8_t arg, uint8_t cmd);
    
    TwoWire *_pWire;
    
    lv_obj_t *  _scr = NULL;
    
    uint8_t _enableCamera = 0;
    uint8_t _sc7a20hAddr = 0x19;

    
    
   

};

#endif
