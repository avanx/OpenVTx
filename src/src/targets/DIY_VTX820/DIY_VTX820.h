#ifndef __TARGET_DEF_H_
#define __TARGET_DEF_H_

#define MAX_POWER                   1600 // mW

#define SA_NUM_POWER_LEVELS         5 // Max 5 for INAV.
#define POWER_LEVEL_LABEL_LENGTH    3
extern uint8_t saPowerLevelsLut[SA_NUM_POWER_LEVELS];
extern uint8_t saPowerLevelsLabel[SA_NUM_POWER_LEVELS * POWER_LEVEL_LABEL_LENGTH];

#define INDICATOR_POWERDB_LEVEL1    20
#define INDICATOR_POWERDB_LEVEL2    26
#define INDICATOR_POWERDB_LEVEL3    29
#define INDICATOR_POWERDB_LEVEL4    31
#define INDICATOR_POWERDB_LEVEL5    32

#define UART_RX       PA9
#define UART_TX       PA9

#define SPI_SS        PB3
#define SPI_CLOCK     PA15
#define SPI_MOSI      PB4

#define LED1          PA4 // Red (power)
#define LED2          PA3 // Green (connected)
#define LED3          PA2 // Blue (SA message)
#define VREF          PA0
#define VPD           PA1

#define RTC_BIAS      PB5

#define BUTTON        PA6

/******* Target specific declarations *******/
#include <gd32f1x0.h>
#include "gpio.h"
#include "serial.h"

uint32_t millis(void);
void delay(uint32_t ms);
void delayMicroseconds(uint32_t us);

#endif /* __TARGET_DEF_H_ */
