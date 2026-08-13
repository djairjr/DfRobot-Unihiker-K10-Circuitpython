#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>
#include "soc/soc_caps.h"

#define BOARD_UNIHIKER_K10 1

#define USB_VID 0x303a
#define USB_PID 0x1001

#define EXTERNAL_NUM_INTERRUPTS 46
#define NUM_DIGITAL_PINS        48
#define NUM_ANALOG_INPUTS       20

// Some boards have too low voltage on this pin (board design bug)
// Use different pin with 3V and connect with 48
// and change this setup for the chosen pin (for example 38)
static const uint8_t LED_BUILTIN = SOC_GPIO_PIN_COUNT+48;
#define BUILTIN_LED  LED_BUILTIN // backward compatibility
#define LED_BUILTIN LED_BUILTIN
#define RGB_BUILTIN LED_BUILTIN
#define RGB_BRIGHTNESS 64

#define analogInputToDigitalPin(p)  (((p)<20)?(analogChannelToDigitalPin(p)):-1)
#define digitalPinToInterrupt(p)    (((p)<48)?(p):-1)
#define digitalPinHasPWM(p)         (p < 46)

static const uint8_t TX = -1;
static const uint8_t RX = -1;

static const uint8_t SDA = 47;
static const uint8_t SCL = 48;

static const uint8_t SS    = 40;
static const uint8_t MOSI  = 21;//21;
static const uint8_t MISO  = 41;
static const uint8_t SCK   = 12;//12;


static const uint8_t A0 = 1;
static const uint8_t A1 = 2;
static const uint8_t A2 = 3;
static const uint8_t A3 = 4;
static const uint8_t A4 = 5;
static const uint8_t A5 = 6;
static const uint8_t A6 = 7;
static const uint8_t A7 = 8;
static const uint8_t A8 = 9;
static const uint8_t A9 = 10;
static const uint8_t A10 = 11;
static const uint8_t A11 = 12;
static const uint8_t A12 = 13;
static const uint8_t A13 = 14;
static const uint8_t A14 = 15;
static const uint8_t A15 = 16;
static const uint8_t A16 = 17;
static const uint8_t A17 = 18;
static const uint8_t A18 = 19;
static const uint8_t A19 = 20;

static const uint8_t T1 = 1;
static const uint8_t T2 = 2;
static const uint8_t T3 = 3;
static const uint8_t T4 = 4;
static const uint8_t T5 = 5;
static const uint8_t T6 = 6;
static const uint8_t T7 = 7;
static const uint8_t T8 = 8;
static const uint8_t T9 = 9;
static const uint8_t T10 = 10;
static const uint8_t T11 = 11;
static const uint8_t T12 = 12;
static const uint8_t T13 = 13;
static const uint8_t T14 = 14;
// 原生IO
static const uint8_t P0 = 1;
static const uint8_t P1 = 2;
static const uint8_t P2 = 3;
// 扩展IO
static const uint8_t P3 = 14;
static const uint8_t P4 = 13;
static const uint8_t P5 = 12; //key A
static const uint8_t P6 = 11; 
static const uint8_t P8 = 8;
static const uint8_t P9 = 9;
static const uint8_t P10 = 10;
static const uint8_t P11 = 2; //key B
static const uint8_t P12 = 3;
static const uint8_t P13 = 4;
static const uint8_t P14 = 5;
static const uint8_t P15 = 6;
static const uint8_t P16 = 7;

#endif /* Pins_Arduino_h */
