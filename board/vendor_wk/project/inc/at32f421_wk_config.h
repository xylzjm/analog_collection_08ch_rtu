/* add user code begin Header */
/**
 **************************************************************************
 * @file     at32f421_wk_config.h
 * @brief    header file of work bench config
 **************************************************************************
 * Copyright (c) 2025, Artery Technology, All rights reserved.
 *
 * The software Board Support Package (BSP) that is made available to
 * download from Artery official website is the copyrighted work of Artery.
 * Artery authorizes customers to use, copy, and distribute the BSP
 * software and its related documentation for the purpose of design and
 * development in conjunction with Artery microcontrollers. Use of the
 * software is governed by this copyright notice and the following disclaimer.
 *
 * THIS SOFTWARE IS PROVIDED ON "AS IS" BASIS WITHOUT WARRANTIES,
 * GUARANTEES OR REPRESENTATIONS OF ANY KIND. ARTERY EXPRESSLY DISCLAIMS,
 * TO THE FULLEST EXTENT PERMITTED BY LAW, ALL EXPRESS, IMPLIED OR
 * STATUTORY OR OTHER WARRANTIES, GUARANTEES OR REPRESENTATIONS,
 * INCLUDING BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT.
 *
 **************************************************************************
 */
/* add user code end Header */

/* define to prevent recursive inclusion -----------------------------------*/
#ifndef __AT32F421_WK_CONFIG_H
#define __AT32F421_WK_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* includes -----------------------------------------------------------------------*/
#include "stdio.h"
#include "at32f421.h"

/* private includes -------------------------------------------------------------*/
/* add user code begin private includes */

/* add user code end private includes */

/* exported types -------------------------------------------------------------*/
/* add user code begin exported types */

/* add user code end exported types */

/* exported constants --------------------------------------------------------*/
/* add user code begin exported constants */

/* add user code end exported constants */

/* exported macro ------------------------------------------------------------*/
/* add user code begin exported macro */

/* add user code end exported macro */

/* add user code begin dma define */
/* user can only modify the dma define value */
// #define DMA1_CHANNEL1_BUFFER_SIZE   0
// #define DMA1_CHANNEL1_MEMORY_BASE_ADDR   0
// #define DMA1_CHANNEL1_PERIPHERAL_BASE_ADDR  0

// #define DMA1_CHANNEL2_BUFFER_SIZE   0
// #define DMA1_CHANNEL2_MEMORY_BASE_ADDR   0
// #define DMA1_CHANNEL2_PERIPHERAL_BASE_ADDR   0

// #define DMA1_CHANNEL3_BUFFER_SIZE   0
// #define DMA1_CHANNEL3_MEMORY_BASE_ADDR   0
// #define DMA1_CHANNEL3_PERIPHERAL_BASE_ADDR   0

// #define DMA1_CHANNEL4_BUFFER_SIZE   0
// #define DMA1_CHANNEL4_MEMORY_BASE_ADDR   0
// #define DMA1_CHANNEL4_PERIPHERAL_BASE_ADDR   0

// #define DMA1_CHANNEL5_BUFFER_SIZE   0
// #define DMA1_CHANNEL5_MEMORY_BASE_ADDR   0
// #define DMA1_CHANNEL5_PERIPHERAL_BASE_ADDR   0
/* add user code end dma define */

/* Private defines -------------------------------------------------------------*/
#define SW_ADDR1_PIN    GPIO_PINS_13
#define SW_ADDR1_GPIO_PORT    GPIOC
#define CTRL_CV_4_PIN    GPIO_PINS_14
#define CTRL_CV_4_GPIO_PORT    GPIOC
#define CTRL_COM_4_PIN    GPIO_PINS_15
#define CTRL_COM_4_GPIO_PORT    GPIOC
#define CTRL_nCS_8_PIN    GPIO_PINS_0
#define CTRL_nCS_8_GPIO_PORT    GPIOF
#define CTRL_COM_8_PIN    GPIO_PINS_1
#define CTRL_COM_8_GPIO_PORT    GPIOF
#define CTRL_COM_5_PIN    GPIO_PINS_0
#define CTRL_COM_5_GPIO_PORT    GPIOA
#define I2C_SDA_PIN    GPIO_PINS_1
#define I2C_SDA_GPIO_PORT    GPIOA
#define I2C_SCL_PIN    GPIO_PINS_2
#define I2C_SCL_GPIO_PORT    GPIOA
#define CTRL_CV_5_PIN    GPIO_PINS_3
#define CTRL_CV_5_GPIO_PORT    GPIOA
#define CTRL_COM_1_PIN    GPIO_PINS_4
#define CTRL_COM_1_GPIO_PORT    GPIOA
#define CTRL_nCS_5_PIN    GPIO_PINS_0
#define CTRL_nCS_5_GPIO_PORT    GPIOB
#define CTRL_CV_1_PIN    GPIO_PINS_1
#define CTRL_CV_1_GPIO_PORT    GPIOB
#define CTRL_nCS_4_PIN    GPIO_PINS_2
#define CTRL_nCS_4_GPIO_PORT    GPIOB
#define CTRL_nCS_1_PIN    GPIO_PINS_10
#define CTRL_nCS_1_GPIO_PORT    GPIOB
#define CTRL_CV_8_PIN    GPIO_PINS_11
#define CTRL_CV_8_GPIO_PORT    GPIOB
#define CTRL_COM_2_PIN    GPIO_PINS_12
#define CTRL_COM_2_GPIO_PORT    GPIOB
#define CTRL_nCS_7_PIN    GPIO_PINS_13
#define CTRL_nCS_7_GPIO_PORT    GPIOB
#define CTRL_CV_2_PIN    GPIO_PINS_14
#define CTRL_CV_2_GPIO_PORT    GPIOB
#define CTRL_COM_6_PIN    GPIO_PINS_15
#define CTRL_COM_6_GPIO_PORT    GPIOB
#define CTRL_nCS_2_PIN    GPIO_PINS_8
#define CTRL_nCS_2_GPIO_PORT    GPIOA
#define USART_DE_PIN    GPIO_PINS_11
#define USART_DE_GPIO_PORT    GPIOA
#define CTRL_CV_6_PIN    GPIO_PINS_12
#define CTRL_CV_6_GPIO_PORT    GPIOA
#define CTRL_COM_3_PIN    GPIO_PINS_6
#define CTRL_COM_3_GPIO_PORT    GPIOF
#define CTRL_nCS_6_PIN    GPIO_PINS_7
#define CTRL_nCS_6_GPIO_PORT    GPIOF
#define CTRL_CV_3_PIN    GPIO_PINS_4
#define CTRL_CV_3_GPIO_PORT    GPIOB
#define CTRL_CV_7_PIN    GPIO_PINS_5
#define CTRL_CV_7_GPIO_PORT    GPIOB
#define CTRL_COM_7_PIN    GPIO_PINS_6
#define CTRL_COM_7_GPIO_PORT    GPIOB
#define SW_ADDR3_PIN    GPIO_PINS_7
#define SW_ADDR3_GPIO_PORT    GPIOB
#define SW_ADDR2_PIN    GPIO_PINS_8
#define SW_ADDR2_GPIO_PORT    GPIOB
#define CTRL_nCS_3_PIN    GPIO_PINS_9
#define CTRL_nCS_3_GPIO_PORT    GPIOB

/* exported functions ------------------------------------------------------- */
  /* system clock config. */
  void wk_system_clock_config(void);

  /* config periph clock. */
  void wk_periph_clock_config(void);

  /* nvic config. */
  void wk_nvic_config(void);

/* add user code begin exported functions */

/* add user code end exported functions */

#ifdef __cplusplus
}
#endif

#endif
