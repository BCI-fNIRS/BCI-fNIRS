/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "stdint.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define SAMPLE_COUNT 40U
#define NUM_TASKS 2U
#define NUM_BUFFERS 2U

#define NO_TRANSMISSION_IN_PROGRESS 255U

#define BUFFER_EMPTY 0U
#define BUFFER_FULL 1U

#define SAMPLING_INACTIVE 0U
#define SAMPLING_ACTIVE 1U

/* LED Source Macros */
#define LED_735_S1 1U
#define LED_850_S1 2U
#define LED_735_S2 3U
#define LED_850_S2 4U
#define LED_735_S3 5U
#define LED_850_S3 6U
#define LED_735_S4 7U
#define LED_850_S4 8U

/* MUX Macros */
#define MUX_A 'a'
#define MUX_B 'b'

#define DETECTOR_0 0U
#define DETECTOR_1 1U
#define DETECTOR_2 2U
#define DETECTOR_3 3U
#define DETECTOR_4 4U
#define DETECTOR_5 5U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* This is what we send over UART to the PC. */
typedef struct {
	uint32_t uniqueHeader;
	uint16_t adcSamples[SAMPLE_COUNT];
	uint32_t uniqueEnder;	//The future vision here is a checksum.
} DataPacket_t;

/* This is an object that is being passed around. It mimics OOP but in C instead. */
typedef struct {
	DataPacket_t dataPacket;
	volatile uint8_t bufferFullFlag;
} DataBuffer_t;

/* This holds info about the specific configurations needed for a particular sample collection event. */
typedef struct {
    uint8_t led_source;        // e.g., LED_735_S1
    char mux_select;           // 'a' or 'b' for MUXA or MUXB
    uint8_t mux_input_value;   // 0 to 5
    uint32_t compare_value;    // 50μs or 100μs
} SamplingSequence_t;
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
ADC_HandleTypeDef hadc2;

TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;

UART_HandleTypeDef huart4;

/* USER CODE BEGIN PV */
/**
 * @brief This index is used to iterate through the buffer storing ADC samples.
 */
volatile uint8_t adcSampleIndex = 0;

/**
 * @brief This index is used to denote which buffer (0 or 1) is being filled by the ADC.
 * This is used by dataBuffers array to switch between which buffer is being referred to.
 */
volatile uint8_t bufferNumIndex = 0;

/**
 * @brief This is a flag that allows TIMER3 to do its job of collecting ADC data or switching buffer.
 * This flag is lowered once 40 ADC samples have been collected, effectively rendering
 * TIMER3 idle until the next 10ms sampling interval. This flag approach is a bandaid method
 * since you cannot disable TIMER3 (200us) and start it again within the IRQ handler of TIMER2 (10ms).
 */
volatile uint8_t samplingActive = SAMPLING_ACTIVE;

/**
 * @brief This flag indicates whether a UART transmission is in progress or not.
 */
volatile uint8_t transmittingBuffer = NO_TRANSMISSION_IN_PROGRESS; // Invalid index to indicate no transmission in progress

/**
 * @brief This instantiates two buffers to be ping-ponged for time efficiency.
 */
DataBuffer_t dataBuffers[NUM_BUFFERS];

/**
 * @brief Timer variables, used in initialization and output compare period calculations.
 * TIM3 & TIM2 are on APB1 bus matrix. Refer to page 139 of STM32H723ZG ref. manual
 * Timer Period = (Prescaler + 1) * (ARR + 1) / f_clk (Hz)
 */
#define F_CLK 175000000UL
#define TIM2_PSC 1749U
#define TIM2_ARR 1999U
#define TIM3_PSC 69U
#define TIM3_ARR 999U

/* Old 10ms and 200us configurations.
#define TIM2_PSC 1749U
#define TIM2_ARR 999U
#define TIM3_PSC 69U
#define TIM3_ARR 499U
*/

/**
 * @brief Long separation output compare period;
 * OC Match Time = (Prescaler + 1) * OC.Pulse / f_clk (Hz)
 * OC is tied to TIM3, which is APB1 bus matrix (f_clk = 175MHz)
 */
#define LONG_OC_PRD 180U                                                        //USER-DEFINED, desired compare period (in microseconds)
#define LONG_OC_PULSE (((uint64_t)LONG_OC_PRD * F_CLK) / ((TIM3_PSC + 1) * 1000000UL))    //CALCULATED, DO NOT TOUCH!

#define LONGMA_OC_PRD 360U                                                       //USER-DEFINED, desired compare period (in microseconds)
#define LONGMA_OC_PULSE (((uint64_t)LONGMA_OC_PRD * F_CLK) / ((TIM3_PSC + 1) * 1000000UL))    //CALCULATED, DO NOT TOUCH!

/**
 * @brief Short separation output compare period;
 * OC Match Time = (Prescaler + 1) * OC.Pulse / f_clk (Hz)
 * OC is tied to TIM3, which is APB1 bus matrix (f_clk = 175MHz)
 */
#define SHORT_OC_PRD 40U                                                        //USER-DEFINED, desired compare period (in microseconds)
#define SHORT_OC_PULSE (((uint64_t)SHORT_OC_PRD * F_CLK) / ((TIM3_PSC + 1) * 1000000UL))  //CALCULATED, DO NOT TOUCH!

#define SHORTMA_OC_PRD 80U                                                        //USER-DEFINED, desired compare period (in microseconds)
#define SHORTMA_OC_PULSE (((uint64_t)SHORT_OC_PRD * F_CLK) / ((TIM3_PSC + 1) * 1000000UL))  //CALCULATED, DO NOT TOUCH!


SamplingSequence_t sequence[] = {
    // For LED_735_S1
    {LED_735_S1, MUX_A, DETECTOR_0, LONG_OC_PULSE},
    {LED_735_S1, MUX_A, DETECTOR_1, LONG_OC_PULSE},
    {LED_735_S1, MUX_A, DETECTOR_2, SHORT_OC_PULSE},
    {LED_735_S1, MUX_A, DETECTOR_4, LONG_OC_PULSE},
    {LED_735_S1, MUX_A, DETECTOR_5, LONG_OC_PULSE},
    // For LED_850_S1
    {LED_850_S1, MUX_A, DETECTOR_0,  LONGMA_OC_PULSE},
    {LED_850_S1, MUX_A, DETECTOR_1,  LONGMA_OC_PULSE},
    {LED_850_S1, MUX_A, DETECTOR_2, SHORTMA_OC_PULSE},
    {LED_850_S1, MUX_A, DETECTOR_4,  LONGMA_OC_PULSE},
    {LED_850_S1, MUX_A, DETECTOR_5,  LONGMA_OC_PULSE},
    // For LED_735_S2
    {LED_735_S2, MUX_A, DETECTOR_2, LONG_OC_PULSE},
    {LED_735_S2, MUX_A, DETECTOR_3, LONG_OC_PULSE},
    {LED_735_S2, MUX_A, DETECTOR_4, SHORT_OC_PULSE},
    {LED_735_S2, MUX_B, DETECTOR_0, LONG_OC_PULSE},
    {LED_735_S2, MUX_B, DETECTOR_1, LONG_OC_PULSE},
    // For LED_850_S2
    {LED_850_S2, MUX_A, DETECTOR_2,  LONGMA_OC_PULSE},
    {LED_850_S2, MUX_A, DETECTOR_3,  LONGMA_OC_PULSE},
    {LED_850_S2, MUX_A, DETECTOR_4, SHORTMA_OC_PULSE},
    {LED_850_S2, MUX_B, DETECTOR_0,  LONGMA_OC_PULSE},
    {LED_850_S2, MUX_B, DETECTOR_1,  LONGMA_OC_PULSE},
    // For LED_735_S3
    {LED_735_S3, MUX_A, DETECTOR_4, LONG_OC_PULSE},
    {LED_735_S3, MUX_A, DETECTOR_5, LONG_OC_PULSE},
    {LED_735_S3, MUX_B, DETECTOR_0, SHORT_OC_PULSE},
    {LED_735_S3, MUX_B, DETECTOR_2, LONG_OC_PULSE},
    {LED_735_S3, MUX_B, DETECTOR_3, LONG_OC_PULSE},
    // For LED_850_S3
    {LED_850_S3, MUX_A, DETECTOR_4,  LONGMA_OC_PULSE},
    {LED_850_S3, MUX_A, DETECTOR_5,  LONGMA_OC_PULSE},
    {LED_850_S3, MUX_B, DETECTOR_0, SHORTMA_OC_PULSE},
    {LED_850_S3, MUX_B, DETECTOR_2,  LONGMA_OC_PULSE},
    {LED_850_S3, MUX_B, DETECTOR_3,  LONGMA_OC_PULSE},
    // For LED_735_S4
    {LED_735_S4, MUX_B, DETECTOR_0, LONG_OC_PULSE},
    {LED_735_S4, MUX_B, DETECTOR_1, LONG_OC_PULSE},
    {LED_735_S4, MUX_B, DETECTOR_2, SHORT_OC_PULSE},
    {LED_735_S4, MUX_B, DETECTOR_4, LONG_OC_PULSE},
    {LED_735_S4, MUX_B, DETECTOR_5, LONG_OC_PULSE},
    // For LED_850_S4
    {LED_850_S4, MUX_B, DETECTOR_0,  LONGMA_OC_PULSE},
    {LED_850_S4, MUX_B, DETECTOR_1,  LONGMA_OC_PULSE},
    {LED_850_S4, MUX_B, DETECTOR_2, SHORTMA_OC_PULSE},
    {LED_850_S4, MUX_B, DETECTOR_4,  LONGMA_OC_PULSE},
    {LED_850_S4, MUX_B, DETECTOR_5,  LONGMA_OC_PULSE}
};

#define SEQUENCE_LENGTH (sizeof(sequence) / sizeof(SamplingSequence_t))

volatile SamplingSequence_t current_sequence;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_UART4_Init(void);
static void MX_TIM2_Init(void);
static void MX_TIM3_Init(void);
static void MX_ADC2_Init(void);
/* USER CODE BEGIN PFP */
void UpdateSequenceState(void);
void SetMuxInputs(char mux_select, uint8_t mux_input_value);
void Check_Buffer_Full_Task(void);
void Buffers_Overflow_Error_Task(void);
void SetTIAHigh(char mux_select);
void SetTIALow(void);
void TurnOnLED(uint8_t led_source);
void TurnOffAllLEDs(void);
void StartADCConversion(char mux_select);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/**
 * @brief Task Array for Round-Robin Scheduler
 */
void (*TaskArray[NUM_TASKS])(void) = 
{
	Check_Buffer_Full_Task,
	Buffers_Overflow_Error_Task
};

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  /* MCU LED active indicates no errors and everything is functional. */
  //HAL_GPIO_WritePin(MCU_LED_GPIO_Port, MCU_LED_Pin, GPIO_PIN_SET);

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* Configure the peripherals common clocks */
  PeriphCommonClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_UART4_Init();
  MX_TIM2_Init();
  MX_TIM3_Init();
  MX_ADC2_Init();
  /* USER CODE BEGIN 2 */
  SetTIALow();
  HAL_Delay(10);  //Ensures TIA is fully discharged.

  /* Initializes the two buffer objects with data. */
	for (uint8_t i = 0; i < NUM_BUFFERS; i++)
	{
		dataBuffers[i].dataPacket.uniqueHeader = 0xFFFFFFFF;
		dataBuffers[i].dataPacket.uniqueEnder = 0xDEADBEEF;
		dataBuffers[i].bufferFullFlag = BUFFER_EMPTY;
	}

  /* Initializes the first sequence to kick things off. */
  UpdateSequenceState();

  /* Drives MUX Enable LOW to start receiving input changes. (Inverse logic) */
  HAL_GPIO_WritePin(MUX_ENABLE_GPIO_Port, MUX_ENABLE_Pin, GPIO_PIN_RESET);

  /* Selects the sensor and starts charging the TIA for the first 200us interval. */
  SetMuxInputs(current_sequence.mux_select, current_sequence.mux_input_value);
  TurnOnLED(current_sequence.led_source);
  SetTIAHigh(current_sequence.mux_select);

  /* Reset TIM2 and TIM3 counter */
  __HAL_TIM_SET_COUNTER(&htim3, 0);
  __HAL_TIM_SET_COUNTER(&htim2, 0);

  /* Kicks off 10ms timer, 200us timer, and Compare Capture */
	if (HAL_TIM_Base_Start_IT(&htim2) != HAL_OK){ Error_Handler(); }
	if (HAL_TIM_Base_Start_IT(&htim3) != HAL_OK){ Error_Handler(); }
	if (HAL_TIM_OC_Start_IT(&htim3, TIM_CHANNEL_1) != HAL_OK){ Error_Handler(); }

  // Round-Robin Scheduler Variables
  uint8_t currentTask = 0;

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    // Call the current task
		TaskArray[currentTask]();

		// Move to the next task
		currentTask = (currentTask + 1) % NUM_TASKS;
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

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 43;
  RCC_OscInitStruct.PLL.PLLP = 1;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 6144;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief Peripherals Common Clock Configuration
  * @retval None
  */
void PeriphCommonClock_Config(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  /** Initializes the peripherals clock
  */
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInitStruct.PLL2.PLL2M = 1;
  PeriphClkInitStruct.PLL2.PLL2N = 24;
  PeriphClkInitStruct.PLL2.PLL2P = 2;
  PeriphClkInitStruct.PLL2.PLL2Q = 2;
  PeriphClkInitStruct.PLL2.PLL2R = 2;
  PeriphClkInitStruct.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_3;
  PeriphClkInitStruct.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
  PeriphClkInitStruct.PLL2.PLL2FRACN = 0;
  PeriphClkInitStruct.AdcClockSelection = RCC_ADCCLKSOURCE_PLL2;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_MultiModeTypeDef multimode = {0};
  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV2;
  hadc1.Init.Resolution = ADC_RESOLUTION_16B;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DR;
  hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.LeftBitShift = ADC_LEFTBITSHIFT_NONE;
  hadc1.Init.OversamplingMode = DISABLE;
  hadc1.Init.Oversampling.Ratio = 1;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure the ADC multi-mode
  */
  multimode.Mode = ADC_MODE_INDEPENDENT;
  if (HAL_ADCEx_MultiModeConfigChannel(&hadc1, &multimode) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_3;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_64CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  sConfig.OffsetSignedSaturation = DISABLE;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief ADC2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC2_Init(void)
{

  /* USER CODE BEGIN ADC2_Init 0 */

  /* USER CODE END ADC2_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC2_Init 1 */

  /* USER CODE END ADC2_Init 1 */

  /** Common config
  */
  hadc2.Instance = ADC2;
  hadc2.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV2;
  hadc2.Init.Resolution = ADC_RESOLUTION_16B;
  hadc2.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc2.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc2.Init.LowPowerAutoWait = DISABLE;
  hadc2.Init.ContinuousConvMode = DISABLE;
  hadc2.Init.NbrOfConversion = 1;
  hadc2.Init.DiscontinuousConvMode = DISABLE;
  hadc2.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc2.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc2.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DR;
  hadc2.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc2.Init.LeftBitShift = ADC_LEFTBITSHIFT_NONE;
  hadc2.Init.OversamplingMode = DISABLE;
  hadc2.Init.Oversampling.Ratio = 1;
  if (HAL_ADC_Init(&hadc2) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_10;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_64CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  sConfig.OffsetSignedSaturation = DISABLE;
  if (HAL_ADC_ConfigChannel(&hadc2, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC2_Init 2 */

  /* USER CODE END ADC2_Init 2 */

}

/**
  * @brief TIM2 Initialization Function
  * TIM2 is on APB1 bus matrix. Refer to page 139 of STM32H723ZG ref. manual
  * Timer Period = (Prescaler + 1) * (ARR + 1) / f_clk (Hz)
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = TIM2_PSC;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = TIM2_ARR;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

}

/**
  * @brief TIM3 Initialization Function
  * TIM3 is on APB1 bus matrix. Refer to page 139 of STM32H723ZG ref. manual
  * Timer Period = (Prescaler + 1) * (ARR + 1) / f_clk (Hz)
  * OC Match Time = (Prescaler + 1) * OC.Pulse / f_clk (Hz)
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = TIM3_PSC;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = TIM3_ARR;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_OC_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_TIMING;
  sConfigOC.Pulse = LONG_OC_PULSE;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_OC_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */
  /**
  * @brief IMPORTANT! Preload ENABLE ensures that a compare period update won't take effect
  * until the next timer cycle. This prevents the error where let's say a 90us compare period
  * updates the compare period to 180us. Then when the timer reaches 180us, it activates again,
  * which is something we do not want. We want a singular compare period event per timer interval.
  */
  __HAL_TIM_ENABLE_OCxPRELOAD(&htim3, TIM_CHANNEL_1);
  /* USER CODE END TIM3_Init 2 */

}

/**
  * @brief UART4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_UART4_Init(void)
{

  /* USER CODE BEGIN UART4_Init 0 */

  /* USER CODE END UART4_Init 0 */

  /* USER CODE BEGIN UART4_Init 1 */

  /* USER CODE END UART4_Init 1 */
  huart4.Instance = UART4;
  huart4.Init.BaudRate = 230400;
  huart4.Init.WordLength = UART_WORDLENGTH_8B;
  huart4.Init.StopBits = UART_STOPBITS_1;
  huart4.Init.Parity = UART_PARITY_NONE;
  huart4.Init.Mode = UART_MODE_TX_RX;
  huart4.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart4.Init.OverSampling = UART_OVERSAMPLING_16;
  huart4.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart4.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart4.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart4) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart4, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart4, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart4) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN UART4_Init 2 */

  /* USER CODE END UART4_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
/* USER CODE BEGIN MX_GPIO_Init_1 */
/* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, MUX_ENABLE_Pin|MUXB_S1_Pin|MUXB_S2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, MUXB_S0_Pin|MUXA_S2_Pin|MUXA_S1_Pin|MUXA_S0_Pin
                          |TIA_RST_B_Pin|LED_850_S4_Pin|LED_735_S4_Pin|LED_850_S3_Pin
                          |MCU_LED_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, LED_735_S3_Pin|LED_850_S2_Pin|LED_735_S2_Pin|TIA_RST_A_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, LED_850_S1_Pin|LED_735_S1_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : MUX_ENABLE_Pin MUXB_S1_Pin MUXB_S2_Pin */
  GPIO_InitStruct.Pin = MUX_ENABLE_Pin|MUXB_S1_Pin|MUXB_S2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : MUXB_S0_Pin MUXA_S2_Pin MUXA_S1_Pin MUXA_S0_Pin
                           TIA_RST_B_Pin LED_850_S4_Pin LED_735_S4_Pin LED_850_S3_Pin
                           MCU_LED_Pin */
  GPIO_InitStruct.Pin = MUXB_S0_Pin|MUXA_S2_Pin|MUXA_S1_Pin|MUXA_S0_Pin
                          |TIA_RST_B_Pin|LED_850_S4_Pin|LED_735_S4_Pin|LED_850_S3_Pin
                          |MCU_LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pins : LED_735_S3_Pin LED_850_S2_Pin LED_735_S2_Pin TIA_RST_A_Pin */
  GPIO_InitStruct.Pin = LED_735_S3_Pin|LED_850_S2_Pin|LED_735_S2_Pin|TIA_RST_A_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : LED_850_S1_Pin LED_735_S1_Pin */
  GPIO_InitStruct.Pin = LED_850_S1_Pin|LED_735_S1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

/* USER CODE BEGIN MX_GPIO_Init_2 */
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
/**
  * @brief Updates the current_sequence object to contain values for the
  * configurations for the next data sample collection event.
  * @param None
  * @retval None
  */
void UpdateSequenceState(void)
{
  static uint8_t index = 0;

  /* Load the next sequence step */
  current_sequence = sequence[index];

  /* Update the timer's compare value */
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, current_sequence.compare_value);

  uint32_t debug_value = __HAL_TIM_GET_COMPARE(&htim3, TIM_CHANNEL_1);
  if (debug_value != current_sequence.compare_value){ Error_Handler(); }

  /* Increment index for next update */
  index = (index + 1) % SEQUENCE_LENGTH;
}

/**
  * @brief Takes the number defined in the current sequence entry
  * and toggles MUX GPIO inputs accordingly.
  * @param mux_select denoting MUXA or MUXB.
  * @param mux_input_value denoting 0 - 5 for sensor number.
  * @retval None
  */
void SetMuxInputs(char mux_select, uint8_t mux_input_value)
{
  uint8_t bit0 = (mux_input_value >> 0) & 0x01; // LSB
  uint8_t bit1 = (mux_input_value >> 1) & 0x01;
  uint8_t bit2 = (mux_input_value >> 2) & 0x01;

  if (mux_select == MUX_A)
  {
    HAL_GPIO_WritePin(MUXA_S0_GPIO_Port, MUXA_S0_Pin, bit0 ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MUXA_S1_GPIO_Port, MUXA_S1_Pin, bit1 ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MUXA_S2_GPIO_Port, MUXA_S2_Pin, bit2 ? GPIO_PIN_SET : GPIO_PIN_RESET);
  }
  else if (mux_select == MUX_B)
  {
    HAL_GPIO_WritePin(MUXB_S0_GPIO_Port, MUXB_S0_Pin, bit0 ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MUXB_S1_GPIO_Port, MUXB_S1_Pin, bit1 ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MUXB_S2_GPIO_Port, MUXB_S2_Pin, bit2 ? GPIO_PIN_SET : GPIO_PIN_RESET);
  }
  else
  {
    Error_Handler();
    return; //Error code
  }
}

/**
  * @brief Toggles TIA to HIGH. Which TIA is chosen to
  * toggle depends on whether it is MUXA or MUXB.
  * @param mux_select denoting MUXA or MUXB.
  * @retval None
  */
void SetTIAHigh(char mux_select)
{ 
  if (mux_select == MUX_A)
  {
    HAL_GPIO_WritePin(TIA_RST_A_GPIO_Port, TIA_RST_A_Pin, GPIO_PIN_RESET);
  }
  else if (mux_select == MUX_B)
  {
    HAL_GPIO_WritePin(TIA_RST_B_GPIO_Port, TIA_RST_B_Pin, GPIO_PIN_RESET);
  }
  else{Error_Handler();}
  HAL_GPIO_WritePin(MCU_LED_GPIO_Port, MCU_LED_Pin, GPIO_PIN_SET);
}

/**
  * @brief Toggles all TIA GPIOs to LOW.
  * @param None
  * @retval None
  */
void SetTIALow(void)
{
  HAL_GPIO_WritePin(TIA_RST_A_GPIO_Port, TIA_RST_A_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(TIA_RST_B_GPIO_Port, TIA_RST_B_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(MCU_LED_GPIO_Port, MCU_LED_Pin, GPIO_PIN_RESET);
}

/**
  * @brief Toggles specified LED to HIGH.
  * @param led_source Source specified by the sequence.
  * @retval None
  */
void TurnOnLED(uint8_t led_source)
{
  switch (led_source)
  {
    case LED_735_S1:
      HAL_GPIO_WritePin(LED_735_S1_GPIO_Port, LED_735_S1_Pin, GPIO_PIN_SET);
      break;
    case LED_850_S1:
      HAL_GPIO_WritePin(LED_850_S1_GPIO_Port, LED_850_S1_Pin, GPIO_PIN_SET);
      break;
    case LED_735_S2:
      HAL_GPIO_WritePin(LED_735_S2_GPIO_Port, LED_735_S2_Pin, GPIO_PIN_SET);
      break;
    case LED_850_S2:
      HAL_GPIO_WritePin(LED_850_S2_GPIO_Port, LED_850_S2_Pin, GPIO_PIN_SET);
      break;
    case LED_735_S3:
      HAL_GPIO_WritePin(LED_735_S3_GPIO_Port, LED_735_S3_Pin, GPIO_PIN_SET);
      break;
    case LED_850_S3:
      HAL_GPIO_WritePin(LED_850_S3_GPIO_Port, LED_850_S3_Pin, GPIO_PIN_SET);
      break;
    case LED_735_S4:
      HAL_GPIO_WritePin(LED_735_S4_GPIO_Port, LED_735_S4_Pin, GPIO_PIN_SET);
      break;
    case LED_850_S4:
      HAL_GPIO_WritePin(LED_850_S4_GPIO_Port, LED_850_S4_Pin, GPIO_PIN_SET);
      break;
    default:
      /* Handle invalid led_source */
      Error_Handler();
      break;
  }
}

/**
  * @brief Toggles all LEDs to LOW.
  * @param None
  * @retval None
  */
void TurnOffAllLEDs(void)
{
  HAL_GPIO_WritePin(LED_735_S1_GPIO_Port, LED_735_S1_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LED_850_S1_GPIO_Port, LED_850_S1_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LED_735_S2_GPIO_Port, LED_735_S2_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LED_850_S2_GPIO_Port, LED_850_S2_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LED_735_S3_GPIO_Port, LED_735_S3_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LED_850_S3_GPIO_Port, LED_850_S3_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LED_735_S4_GPIO_Port, LED_735_S4_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LED_850_S4_GPIO_Port, LED_850_S4_Pin, GPIO_PIN_RESET);
}

/**
  * @brief Select corresponding ADC and trigger sample conversion.
  * @param mux_select denoting MUXA (tied to ADC1) or MUXB (tied to ADC2).
  * @retval None
  */
void StartADCConversion(char mux_select)
{
  if (mux_select == MUX_A)
  {
    HAL_ADC_Start_IT(&hadc1);
  }
  else if (mux_select == MUX_B)
  {
    HAL_ADC_Start_IT(&hadc2);
  }
}

/**
  * @brief Checks if either data buffer has flag raised indicating their buffer is full.
  * If full, transmits the buffer over UART.
  * @param None
  * @retval None
  */
void Check_Buffer_Full_Task(void)
{
  // Iterate over the buffers to check if any buffer is full and not being transmitted
  for (uint8_t i = 0; i < NUM_BUFFERS; i++)
  {
    if ((dataBuffers[i].bufferFullFlag == BUFFER_FULL) && (transmittingBuffer == NO_TRANSMISSION_IN_PROGRESS))
    {
      // Stores the index of the buffer being transmitted so we can refer to it to clear its flag in UART transmit complete callback.
      transmittingBuffer = i;
      HAL_UART_Transmit_IT(&huart4, (uint8_t*)&dataBuffers[i].dataPacket, sizeof(DataPacket_t));
      
      break; // Only handle one buffer at a time
    }
  }
}

/**
  * @brief Callback function upon UART transmission completion.
  * Resets transmission flag so other buffers can be transmitted.
  */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  // Transmission is complete, resets flag to allow other transmissions to proceed.
  dataBuffers[transmittingBuffer].bufferFullFlag = BUFFER_EMPTY; // Reset the buffer full flag
  transmittingBuffer = NO_TRANSMISSION_IN_PROGRESS;
}

/**
 * @brief IRQ handler callback function for when the timers have overflown.
 * For 10ms overflown event:
 * 1) Sets sampling active flag to begin the 40x ADC sampling cycle again.
 * 2) Selects the channel via MUX.
 * 3) Start charging the TIA for the first 200us cycle.
 * 
 * For 200us overflown event:
 * 1) Selects the channel via MUX.
 * 2) Start charging the TIA for the next 200us cycle.
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* Code to execute every 10 ms (TIM2) */
	if (htim->Instance == TIM2)
	{
		__HAL_TIM_CLEAR_IT(&htim2, TIM_IT_UPDATE);

    /* Reset TIM3 counter */
    __HAL_TIM_SET_COUNTER(&htim3, 0);

    /* Set MUX inputs based on current_sequence */
    SetMuxInputs(current_sequence.mux_select, current_sequence.mux_input_value);

    /* Turn on the LED specified in current sequence */
    TurnOnLED(current_sequence.led_source);

    /* Start charging the TIA. */
    SetTIAHigh(current_sequence.mux_select);

		// Raises the flag for TIMER3 (200us) to start doing its job again.
		samplingActive = SAMPLING_ACTIVE;
	}
  /* Code to execute every 200 µs (TIM3) */
	else if (htim->Instance == TIM3)
	{
    __HAL_TIM_CLEAR_IT(&htim3, TIM_IT_UPDATE);

		if (samplingActive == SAMPLING_ACTIVE)
		{
      /* Set MUX inputs based on current_sequence */
      SetMuxInputs(current_sequence.mux_select, current_sequence.mux_input_value);

      /* Turn on the LED specified in current sequence */
      TurnOnLED(current_sequence.led_source);

      /* Start charging the TIA. */
      SetTIAHigh(current_sequence.mux_select);
		}
	}
}

/**
 * @brief Output compare event callback function. This is used
 * to trigger the ADC conversion mid of 200us interval.
 */
void HAL_TIM_OC_DelayElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM3)
  {
    if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1)
    {
			if (samplingActive == SAMPLING_ACTIVE)
			{
        /* Start ADC conversion based on mux select */
        StartADCConversion(current_sequence.mux_select);
			}
    }
  }
}

/**
 * @brief Callback function for a complete ADC sample conversion.
 * Stores the ADC sample in the data buffer, and switches the data buffer
 * if all 40 samples have been collected and the buffer is full.
 */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
{
	if((hadc->Instance == ADC1) || (hadc->Instance == ADC2))
	{
		// Processes the ADC conversion result
		uint16_t adcValue = HAL_ADC_GetValue(hadc);

		// Stores ADC value in the current buffer
    if (dataBuffers[bufferNumIndex].bufferFullFlag == BUFFER_EMPTY)
    {
      dataBuffers[bufferNumIndex].dataPacket.adcSamples[adcSampleIndex] = adcValue;
    }
		else{ Error_Handler(); }

    // Increments adcSampleIndex after storing the value.
    adcSampleIndex++;

    // Resets the TIA & turns off LED.
    SetTIALow();
    TurnOffAllLEDs();

    // Check if buffer is full.
    if (adcSampleIndex >= SAMPLE_COUNT)
    {
      __disable_irq();

      // Buffer is full
      dataBuffers[bufferNumIndex].bufferFullFlag = BUFFER_FULL; // Set buffer full flag.
      adcSampleIndex = 0;                                       // Reset sample index.

      // Switch to the next buffer.
      bufferNumIndex = (bufferNumIndex + 1) % NUM_BUFFERS;

			// Sampling is complete.
      samplingActive = SAMPLING_INACTIVE;

      __enable_irq();
		}

    /* Updates the sequence to the next entry for the next interval. */
    UpdateSequenceState();
	}
}

/**
 * @brief Flags an error exception when both buffers are detected to be full.
 * The implication is that the UART transmission speed is inadequate for data consumption.
 */
void Buffers_Overflow_Error_Task(void)
{
	if(dataBuffers[0].bufferFullFlag == BUFFER_FULL && dataBuffers[1].bufferFullFlag == BUFFER_FULL)
	{
		__disable_irq();
    HAL_GPIO_WritePin(MCU_LED_GPIO_Port, MCU_LED_Pin, GPIO_PIN_RESET);
		//TODO: Probably disable TIMERx, and ADC here.
		while (1)
		{
		}
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
  HAL_GPIO_WritePin(MCU_LED_GPIO_Port, MCU_LED_Pin, GPIO_PIN_RESET);
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
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
