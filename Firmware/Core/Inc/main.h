/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h7xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define MCO_Pin GPIO_PIN_0
#define MCO_GPIO_Port GPIOH
#define TIA_OUT_B_Pin GPIO_PIN_0
#define TIA_OUT_B_GPIO_Port GPIOC
#define TIA_OUT_A_Pin GPIO_PIN_6
#define TIA_OUT_A_GPIO_Port GPIOA
#define MUX_ENABLE_Pin GPIO_PIN_13
#define MUX_ENABLE_GPIO_Port GPIOB
#define MUXB_S0_Pin GPIO_PIN_8
#define MUXB_S0_GPIO_Port GPIOD
#define MUXA_S2_Pin GPIO_PIN_9
#define MUXA_S2_GPIO_Port GPIOD
#define MUXA_S1_Pin GPIO_PIN_10
#define MUXA_S1_GPIO_Port GPIOD
#define MUXA_S0_Pin GPIO_PIN_11
#define MUXA_S0_GPIO_Port GPIOD
#define TIA_RST_B_Pin GPIO_PIN_12
#define TIA_RST_B_GPIO_Port GPIOD
#define LED_850_S4_Pin GPIO_PIN_13
#define LED_850_S4_GPIO_Port GPIOD
#define LED_735_S4_Pin GPIO_PIN_14
#define LED_735_S4_GPIO_Port GPIOD
#define LED_850_S3_Pin GPIO_PIN_15
#define LED_850_S3_GPIO_Port GPIOD
#define LED_735_S3_Pin GPIO_PIN_6
#define LED_735_S3_GPIO_Port GPIOC
#define LED_850_S2_Pin GPIO_PIN_7
#define LED_850_S2_GPIO_Port GPIOC
#define LED_735_S2_Pin GPIO_PIN_8
#define LED_735_S2_GPIO_Port GPIOC
#define TIA_RST_A_Pin GPIO_PIN_9
#define TIA_RST_A_GPIO_Port GPIOC
#define LED_850_S1_Pin GPIO_PIN_8
#define LED_850_S1_GPIO_Port GPIOA
#define LED_735_S1_Pin GPIO_PIN_9
#define LED_735_S1_GPIO_Port GPIOA
#define SWDIO_Pin GPIO_PIN_13
#define SWDIO_GPIO_Port GPIOA
#define SWCLK_Pin GPIO_PIN_14
#define SWCLK_GPIO_Port GPIOA
#define MCU_LED_Pin GPIO_PIN_5
#define MCU_LED_GPIO_Port GPIOD
#define SWO_Pin GPIO_PIN_3
#define SWO_GPIO_Port GPIOB
#define MUXB_S1_Pin GPIO_PIN_6
#define MUXB_S1_GPIO_Port GPIOB
#define MUXB_S2_Pin GPIO_PIN_7
#define MUXB_S2_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
