/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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
#include "stm32f1xx_hal.h"

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
#define LED_Pin GPIO_PIN_13
#define LED_GPIO_Port GPIOC
#define VIN_MONITOR2_Pin GPIO_PIN_1
#define VIN_MONITOR2_GPIO_Port GPIOA
#define Coin_IN_Pin GPIO_PIN_4
#define Coin_IN_GPIO_Port GPIOA
#define Hopper_Pin GPIO_PIN_5
#define Hopper_GPIO_Port GPIOA
#define PayOut_Pin GPIO_PIN_6
#define PayOut_GPIO_Port GPIOA
#define Output4_Pin GPIO_PIN_7
#define Output4_GPIO_Port GPIOA
#define MEM_WP_Pin GPIO_PIN_1
#define MEM_WP_GPIO_Port GPIOB
#define Total_In_Meter_Pin GPIO_PIN_15
#define Total_In_Meter_GPIO_Port GPIOB
#define Total_In_Meter_EXTI_IRQn EXTI15_10_IRQn
#define TotalBETmeter_Pin GPIO_PIN_4
#define TotalBETmeter_GPIO_Port GPIOB
#define TotalBETmeter_EXTI_IRQn EXTI4_IRQn
#define HopperMeter_Pin GPIO_PIN_6
#define HopperMeter_GPIO_Port GPIOB
#define HopperMeter_EXTI_IRQn EXTI9_5_IRQn
#define Refill_Meter_Pin GPIO_PIN_7
#define Refill_Meter_GPIO_Port GPIOB
#define Refill_Meter_EXTI_IRQn EXTI9_5_IRQn
#define HandPayMeter_Pin GPIO_PIN_8
#define HandPayMeter_GPIO_Port GPIOB
#define HandPayMeter_EXTI_IRQn EXTI9_5_IRQn
#define PULSO6_Pin GPIO_PIN_9
#define PULSO6_GPIO_Port GPIOB
#define PULSO6_EXTI_IRQn EXTI9_5_IRQn

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
