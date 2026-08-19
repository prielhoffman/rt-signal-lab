/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "dac.h"
#include "dma.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <math.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define ADC_BUFFER_LENGTH          400U
#define ADC_HALF_LENGTH            (ADC_BUFFER_LENGTH / 2U)
#define SAMPLE_RATE_HZ             20000U
#define HALF_BUFFER_DEADLINE_MS    ((ADC_HALF_LENGTH * 1000U) / SAMPLE_RATE_HZ)
#define UART_REPORT_INTERVAL_BLOCKS 20U

#define PROCESSING_NONE           0U
#define PROCESSING_FIRST_HALF     1U
#define PROCESSING_SECOND_HALF    2U

#define DAC_TABLE_LENGTH    200U
#define DAC_MIDPOINT        2048.0f
#define DAC_AMPLITUDE       1800.0f
#define TWO_PI              6.28318530718f

#define MIDPOINT_THRESHOLD_RAW  2048U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static uint16_t adc_buffer[ADC_BUFFER_LENGTH];

static volatile uint8_t adc_first_half_ready = 0U;
static volatile uint8_t adc_second_half_ready = 0U;
static volatile uint8_t adc_processing_half = PROCESSING_NONE;
static volatile uint32_t buffer_overrun_count = 0U;

static uint32_t processed_half_count = 0U;
static uint32_t last_processing_time_ms = 0U;
static uint32_t max_processing_time_ms = 0U;
static uint32_t deadline_miss_count = 0U;

static char uart_tx_buffer[128];

static uint16_t dac_waveform[DAC_TABLE_LENGTH];

static uint16_t previous_sample_raw = 0U;
static uint8_t previous_sample_valid = 0U;
static uint8_t first_crossing_found = 0U;

static uint32_t total_sample_count = 0U;
static uint32_t previous_crossing_sample = 0U;
static uint32_t measured_period_samples = 0U;
static uint32_t measured_frequency_millihz = 0U;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void ProcessAdcHalf(uint32_t start_index, uint8_t half_id);
static void GenerateSineWave(void);
static void UpdateFrequencyEstimate(uint16_t sample_raw);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void ProcessAdcHalf(uint32_t start_index, uint8_t half_id){
  adc_processing_half = half_id;
  uint32_t processing_start_ms = HAL_GetTick();

  uint16_t min_raw = adc_buffer[start_index];
  uint16_t max_raw = adc_buffer[start_index];
  uint32_t sum_raw = 0U;
  uint64_t sum_squares_raw = 0U;

  for (uint32_t offset = 0U; offset < ADC_HALF_LENGTH; offset++){
    uint16_t sample_raw = adc_buffer[start_index + offset];

    UpdateFrequencyEstimate(sample_raw);

    if (sample_raw < min_raw){
      min_raw = sample_raw;
    }

    if (sample_raw > max_raw){
      max_raw = sample_raw;
    }

    sum_raw += sample_raw;
    sum_squares_raw += (uint64_t)sample_raw * sample_raw;
  }

  processed_half_count++;

  uint32_t average_raw = sum_raw / ADC_HALF_LENGTH;
  uint32_t peak_to_peak_raw = (uint32_t)max_raw - min_raw;

  double mean_square = (double)sum_squares_raw / (double)ADC_HALF_LENGTH;

  uint32_t rms_raw = (uint32_t)(sqrt(mean_square) + 0.5);

  uint32_t min_mv = ((uint32_t)min_raw * 3300U) / 4095U;
  uint32_t max_mv = ((uint32_t)max_raw * 3300U) / 4095U;
  uint32_t average_mv = (average_raw * 3300U) / 4095U;
  uint32_t peak_to_peak_mv = (peak_to_peak_raw * 3300U) / 4095U;
  uint32_t rms_mv = (rms_raw * 3300U) / 4095U;

  if ((processed_half_count % UART_REPORT_INTERVAL_BLOCKS) == 0U){
	  int uart_tx_length = snprintf(uart_tx_buffer, sizeof(uart_tx_buffer), "b=%lu min=%lu max=%lu avg=%lu p2p=%lu rms=%lu " "f=%lu.%03luHz period=%lu ov=%lu miss=%lu\r\n",
	      (unsigned long)processed_half_count, (unsigned long)min_mv, (unsigned long)max_mv, (unsigned long)average_mv, (unsigned long)peak_to_peak_mv,
	      (unsigned long)rms_mv, (unsigned long)(measured_frequency_millihz / 1000U), (unsigned long)(measured_frequency_millihz % 1000U),
	      (unsigned long)measured_period_samples, (unsigned long)buffer_overrun_count, (unsigned long)deadline_miss_count);

    if ((uart_tx_length > 0) && (uart_tx_length < (int)sizeof(uart_tx_buffer))){
      if (HAL_UART_Transmit( &huart2, (uint8_t *)uart_tx_buffer, (uint16_t)uart_tx_length, 100U) != HAL_OK){
        Error_Handler();
      }
    }
  }

  last_processing_time_ms = HAL_GetTick() - processing_start_ms;

  if (last_processing_time_ms > max_processing_time_ms){
    max_processing_time_ms = last_processing_time_ms;
  }

  if (last_processing_time_ms >= HALF_BUFFER_DEADLINE_MS){
    deadline_miss_count++;
  }

  adc_processing_half = PROCESSING_NONE;
}

static void GenerateSineWave(void)
{
  for (uint32_t index = 0U; index < DAC_TABLE_LENGTH; index++){
    float phase = (TWO_PI * (float)index) / (float)DAC_TABLE_LENGTH;

    float sample = DAC_MIDPOINT + (DAC_AMPLITUDE * sinf(phase));

    dac_waveform[index] = (uint16_t)(sample + 0.5f);
  }
}

static void UpdateFrequencyEstimate(uint16_t sample_raw){
  if (previous_sample_valid != 0U){
    if ((previous_sample_raw < MIDPOINT_THRESHOLD_RAW) && (sample_raw >= MIDPOINT_THRESHOLD_RAW)){
      if (first_crossing_found != 0U){
        uint32_t period_samples = total_sample_count - previous_crossing_sample;

        if (period_samples != 0U){
          measured_period_samples = period_samples;

          measured_frequency_millihz = ((SAMPLE_RATE_HZ * 1000U) + (period_samples / 2U)) / period_samples;
        }
      }

      previous_crossing_sample = total_sample_count;
      first_crossing_found = 1U;
    }
  }

  previous_sample_raw = sample_raw;
  previous_sample_valid = 1U;
  total_sample_count++;
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART2_UART_Init();
  MX_DAC1_Init();
  MX_ADC1_Init();
  MX_TIM6_Init();
  /* USER CODE BEGIN 2 */
  if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK){
    Error_Handler();
  }

  static const uint8_t boot_message[] = "rt-signal-lab: boot OK\r\n";

  if (HAL_UART_Transmit(&huart2, boot_message, sizeof(boot_message) - 1U, 100U) != HAL_OK){
    Error_Handler();
  }

  GenerateSineWave();

  if (HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_1, (uint32_t *)dac_waveform, DAC_TABLE_LENGTH, DAC_ALIGN_12B_R) != HAL_OK){
    Error_Handler();
  }

  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_buffer, ADC_BUFFER_LENGTH) != HAL_OK){
    Error_Handler();
  }

  if (HAL_TIM_Base_Start(&htim6) != HAL_OK){
    Error_Handler();
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	  if (adc_first_half_ready != 0U){
	    adc_first_half_ready = 0U;
	    ProcessAdcHalf(0U, PROCESSING_FIRST_HALF);
	  }

	  if (adc_second_half_ready != 0U){
	    adc_second_half_ready = 0U;
	    ProcessAdcHalf(ADC_HALF_LENGTH, PROCESSING_SECOND_HALF);
	  }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV1;
  RCC_OscInitStruct.PLL.PLLN = 8;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc){
  if (hadc->Instance == ADC1){
    if ((adc_first_half_ready != 0U) || (adc_processing_half == PROCESSING_FIRST_HALF)){
      buffer_overrun_count++;
    }

    adc_first_half_ready = 1U;
  }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc){
  if (hadc->Instance == ADC1){
    if ((adc_second_half_ready != 0U) || (adc_processing_half == PROCESSING_SECOND_HALF)){
      buffer_overrun_count++;
    }

    adc_second_half_ready = 1U;
  }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
