#ifndef __AT32F421_WK_CONFIG_H
#define __AT32F421_WK_CONFIG_H

#include <stdint.h>

/* Host-test stub of the WorkBench GPIO aliases used by the board layer. */
typedef struct {
    uint8_t unused;
} gpio_type;

#define GPIOA ((gpio_type*)0)
#define GPIOB ((gpio_type*)0)
#define GPIOC ((gpio_type*)0)

#define GPIO_PINS_1 0x0002U

#define CTRL_COM_1_GPIO_PORT GPIOA
#define CTRL_COM_1_PIN       GPIO_PINS_1
#define CTRL_CV_1_GPIO_PORT  GPIOB
#define CTRL_CV_1_PIN        GPIO_PINS_1
#define CTRL_COM_2_GPIO_PORT GPIOA
#define CTRL_COM_2_PIN       GPIO_PINS_1
#define CTRL_CV_2_GPIO_PORT  GPIOB
#define CTRL_CV_2_PIN        GPIO_PINS_1
#define CTRL_COM_3_GPIO_PORT GPIOA
#define CTRL_COM_3_PIN       GPIO_PINS_1
#define CTRL_CV_3_GPIO_PORT  GPIOB
#define CTRL_CV_3_PIN        GPIO_PINS_1
#define CTRL_COM_4_GPIO_PORT GPIOA
#define CTRL_COM_4_PIN       GPIO_PINS_1
#define CTRL_CV_4_GPIO_PORT  GPIOB
#define CTRL_CV_4_PIN        GPIO_PINS_1
#define CTRL_COM_5_GPIO_PORT GPIOA
#define CTRL_COM_5_PIN       GPIO_PINS_1
#define CTRL_CV_5_GPIO_PORT  GPIOB
#define CTRL_CV_5_PIN        GPIO_PINS_1
#define CTRL_COM_6_GPIO_PORT GPIOA
#define CTRL_COM_6_PIN       GPIO_PINS_1
#define CTRL_CV_6_GPIO_PORT  GPIOB
#define CTRL_CV_6_PIN        GPIO_PINS_1
#define CTRL_COM_7_GPIO_PORT GPIOA
#define CTRL_COM_7_PIN       GPIO_PINS_1
#define CTRL_CV_7_GPIO_PORT  GPIOB
#define CTRL_CV_7_PIN        GPIO_PINS_1
#define CTRL_COM_8_GPIO_PORT GPIOA
#define CTRL_COM_8_PIN       GPIO_PINS_1
#define CTRL_CV_8_GPIO_PORT  GPIOB
#define CTRL_CV_8_PIN        GPIO_PINS_1

void gpio_bits_set(gpio_type* port, uint16_t pin);
void gpio_bits_reset(gpio_type* port, uint16_t pin);

#endif
