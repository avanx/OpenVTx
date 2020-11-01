#ifndef __ARDUINO_H_
#define __ARDUINO_H_

#include "gd32f1x0.h"
#include <stdint.h>
#include <string.h>

uint32_t millis(void);
void delay(uint32_t ms);
void delayMicroseconds(uint32_t us);

/***********************************************************/

#define true 1
#define false 0

#define LOW 0
#define HIGH 1

#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) (bitvalue ? bitSet(value, bit) : bitClear(value, bit))
#define bitRead(value, bit) (((value) >> (bit)) & 0x01)

/***********************************************************/

#define GPIO_NUM_PINS   16
#define GPIO(PORT, NUM) (((PORT) - 'A') * GPIO_NUM_PINS + (NUM))
#define GPIO2PORT(PIN)  ((PIN) / GPIO_NUM_PINS)
#define GPIO2BIT(PIN)   (1U << GPIO2IDX(PIN))
#define GPIO2IDX(PIN)   ((PIN) % GPIO_NUM_PINS)


#define PA0  GPIO('A', 0)
#define PA1  GPIO('A', 1)
#define PA2  GPIO('A', 2)
#define PA3  GPIO('A', 3)
#define PA4  GPIO('A', 4)
#define PA5  GPIO('A', 5)
#define PA6  GPIO('A', 6)
#define PA7  GPIO('A', 7)
#define PA8  GPIO('A', 8)
#define PA9  GPIO('A', 9)
#define PA10 GPIO('A', 10)
#define PA11 GPIO('A', 11)
#define PA12 GPIO('A', 12)
#define PA13 GPIO('A', 13)
#define PA14 GPIO('A', 14)
#define PA15 GPIO('A', 15)
#define PB0  GPIO('B', 0)
#define PB1  GPIO('B', 1)
#define PB2  GPIO('B', 2)
#define PB3  GPIO('B', 3)
#define PB4  GPIO('B', 4)
#define PB5  GPIO('B', 5)
#define PB6  GPIO('B', 6)
#define PB7  GPIO('B', 7)
#define PB8  GPIO('B', 8)
#define PB9  GPIO('B', 9)
#define PB10 GPIO('B', 10)
#define PB11 GPIO('B', 11)
#define PB12 GPIO('B', 12)
#define PB13 GPIO('B', 13)
#define PB14 GPIO('B', 14)
#define PB15 GPIO('B', 15)
#define PC0  GPIO('C', 0)
#define PC1  GPIO('C', 1)
#define PC2  GPIO('C', 2)
#define PC3  GPIO('C', 3)
#define PC4  GPIO('C', 4)
#define PC5  GPIO('C', 5)
#define PC6  GPIO('C', 6)
#define PC7  GPIO('C', 7)
#define PC8  GPIO('C', 8)
#define PC9  GPIO('C', 9)
#define PC10 GPIO('C', 10)
#define PC11 GPIO('C', 11)
#define PC12 GPIO('C', 12)
#define PC13 GPIO('C', 13)
#define PC14 GPIO('C', 14)
#define PC15 GPIO('C', 15)
#define PD0  GPIO('D', 0)
#define PD1  GPIO('D', 1)
#define PD2  GPIO('D', 2)
#define PD3  GPIO('D', 3)
#define PD4  GPIO('D', 4)
#define PD5  GPIO('D', 5)
#define PD6  GPIO('D', 6)
#define PD7  GPIO('D', 7)
#define PD8  GPIO('D', 8)
#define PD9  GPIO('D', 9)
#define PD10 GPIO('D', 10)
#define PD11 GPIO('D', 11)
#define PD12 GPIO('D', 12)
#define PD13 GPIO('D', 13)
#define PD14 GPIO('D', 14)
#define PD15 GPIO('D', 15)
#define PF0  GPIO('F', 0)
#define PF1  GPIO('F', 1)
#define PF2  GPIO('F', 2)
#define PF3  GPIO('F', 3)
#define PF4  GPIO('F', 4)
#define PF5  GPIO('F', 5)
#define PF6  GPIO('F', 6)
#define PF7  GPIO('F', 7)
#define PF8  GPIO('F', 8)
#define PF9  GPIO('F', 9)
#define PF10 GPIO('F', 10)
#define PF11 GPIO('F', 11)
#define PF12 GPIO('F', 12)
#define PF13 GPIO('F', 13)
#define PF14 GPIO('F', 14)
#define PF15 GPIO('F', 15)

/***********************************************************/

enum {
    OUTPUT = 0,
    INPUT = 1,
    INPUT_PULLUP = 2,
};

void pinMode(uint32_t pin, uint8_t type);
void digitalWrite(uint32_t pin, uint8_t val);

/***********************************************************/

void Serial_begin(uint32_t baud);
uint8_t Serial_available(void);
uint8_t Serial_read(void);
void Serial_write(uint8_t data);
void Serial_flush(void);

void UART1_HalfDuplexCmd(uint8_t state);

#endif /* __ARDUINO_H_ */
