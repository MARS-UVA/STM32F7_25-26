/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "adc.h"
#include "can.h"
#include "usart.h"
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
// Latest raw ADC reading from the potentiometer
volatile uint32_t g_adcRaw = 0;

// Latest computed voltage from the potentiometer (in volts)
volatile float    g_voltage = 0.0f;

// Desired motor speed command, normalized 0.0 (stop) to 1.0 (max)
volatile float    g_speedCommand = 0.0f;

// Buffer for UART debug prints
static uint8_t    s_outputBuf[64];
/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for CANTask */
osThreadId_t CANTaskHandle;
const osThreadAttr_t CANTask_attributes = {
  .name = "CANTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};
/* Definitions for PotTask */
osThreadId_t PotTaskHandle;
const osThreadAttr_t PotTask_attributes = {
  .name = "PotTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
void sendCANMessage(CAN_HandleTypeDef *hcan, int identifier, char *message, uint8_t length);
void sendGlobalEnableFrame(CAN_HandleTypeDef *hcan);
/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);
void StartTask02(void *argument);
void StartTask03(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* creation of CANTask */
  CANTaskHandle = osThreadNew(StartTask02, NULL, &CANTask_attributes);

  /* creation of PotTask */
  PotTaskHandle = osThreadNew(StartTask03, NULL, &PotTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartDefaultTask */
}

/* USER CODE BEGIN Header_StartTask02 */
/**
* @brief Function implementing the CANTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask02 */
void StartTask02(void *argument)
{
  /* USER CODE BEGIN StartTask02 */
  // Optional: send a global enable frame once at startup
  sendGlobalEnableFrame(&hcan1);

  for(;;)
  {
    // Take a snapshot of the current desired speed from the pot (0.0 .. 1.0)
    float speedNorm = g_speedCommand;

    // Clamp to [0.0, 1.0] just in case
    if (speedNorm < 0.0f) speedNorm = 0.0f;
    if (speedNorm > 1.0f) speedNorm = 1.0f;

    // Example: map normalized speed to a Q10 fixed-point value (0..1024)
    int16_t spd_q10 = (int16_t)(speedNorm * 1024.0f);

    // Build 8-byte CAN payload (based on your earlier init[] example)
    char init[8] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0xFE, 0x0C};

    // Put the speed into bytes 6 and 7 (little-endian)
    init[6] = (uint8_t)(spd_q10 & 0xFF);
    init[7] = (uint8_t)((spd_q10 >> 8) & 0xFF);

    // Send CAN frame to Kraken X60 (ID = 0x204b540 | 60 as in your code)
    sendCANMessage(&hcan1, 0x204b540 | 60, init, 8);

    // Run this high-priority task about every 10 ms
    osDelay(10);
  }
  /* USER CODE END StartTask02 */
}

/* USER CODE BEGIN Header_StartTask03 */
/**
* @brief Function implementing the PotTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask03 */
void StartTask03(void *argument)
{
  /* USER CODE BEGIN StartTask03 */
  const float ADC_RESOLUTION_COUNTS = 4095.0f;   // 12-bit ADC
  const float REFERENCE_VOLTAGE     = 3.3f;      // VDDA

  for(;;)
  {
    // Start single ADC conversion
    if (HAL_ADC_Start(&hadc1) == HAL_OK)
    {
      // Wait up to 10 ms for conversion to complete
      if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK)
      {
        uint32_t adc = HAL_ADC_GetValue(&hadc1);
        g_adcRaw = adc;

        // Stop ADC after the conversion
        HAL_ADC_Stop(&hadc1);

        // Convert raw ADC to voltage
        g_voltage = ((float)adc / ADC_RESOLUTION_COUNTS) * REFERENCE_VOLTAGE;

        // Map voltage (0..Vref) to normalized speed 0..1
        g_speedCommand = g_voltage / REFERENCE_VOLTAGE;

        // Format a debug string and send over UART3
        int len = snprintf((char *)s_outputBuf, sizeof(s_outputBuf),
                           "ADC=%lu, V=%.3f-/-\r\n",
                           (unsigned long)adc, g_voltage);
        if (len > 0)
        {
          HAL_UART_Transmit(&huart3, s_outputBuf, len, HAL_MAX_DELAY);
        }
      }
      else
      {
        // Timeout: stop ADC to be safe
        HAL_ADC_Stop(&hadc1);
      }
    }

    // Run this lower-priority task about every 100 ms
    osDelay(100);
  }
  /* USER CODE END StartTask03 */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

