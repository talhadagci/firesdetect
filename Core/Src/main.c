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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
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
ADC_HandleTypeDef hadc;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
uint8_t dht_raw[5] = {0};
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_ADC_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static uint32_t read_adc(uint32_t chselr_mask)
{
    /* 0. ADC'nin aktif olduğundan emin ol (kilitlenmeyi önler) */
    if (!(ADC1->CR & ADC_CR_ADEN)) {
        ADC1->CR |= ADC_CR_ADEN;
        while (!(ADC1->ISR & ADC_ISR_ADRDY));
    }

    /* 1. Devam eden herhangi bir dönüşümü durdur */
    if (ADC1->CR & ADC_CR_ADSTART)
    {
        ADC1->CR |= ADC_CR_ADSTP;
        while (ADC1->CR & ADC_CR_ADSTP);
    }

    /* 2. Kanal seçimini temizle ve yenisini ata */
    ADC1->CHSELR = 0;
    for(volatile int i=0; i<50; i++);
    ADC1->CHSELR = chselr_mask;

    /* 3. Stabilizasyon beklemesi */
    for(volatile int i=0; i<100; i++);

    /* 4. DUMMY READ: Önceki kalıntıları temizle */
    ADC1->ISR |= ADC_ISR_EOC; // Bayrağı temizle
    ADC1->CR |= ADC_CR_ADSTART;
    while (!(ADC1->ISR & ADC_ISR_EOC));
    (void)ADC1->DR;

    /* 5. GERÇEK OKUMA */
    ADC1->CR |= ADC_CR_ADSTART;
    while (!(ADC1->ISR & ADC_ISR_EOC));

    return ADC1->DR;
}

static void delay_us(uint32_t us)
{
    uint32_t clk = us * (SystemCoreClock / 1000000U);
    uint32_t ts = SysTick->VAL;
    uint32_t tc;
    do {
        uint32_t te = SysTick->VAL;
        tc = (te > ts) ? (ts + SysTick->LOAD + 1 - te) : (ts - te);
    } while (tc < clk);
}

static void DHT22_SetOutput(void)
{
    GPIO_InitTypeDef g = {0};
    g.Pin = dht22_Pin;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(dht22_GPIO_Port, &g);
}

static void DHT22_SetInput(void)
{
    GPIO_InitTypeDef g = {0};
    g.Pin = dht22_Pin;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(dht22_GPIO_Port, &g);
}

static uint8_t DHT22_Read(float *temperature, float *humidity)
{
    uint8_t data[5] = {0};
    uint32_t count;

    DHT22_SetOutput();
    HAL_GPIO_WritePin(dht22_GPIO_Port, dht22_Pin, GPIO_PIN_RESET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(dht22_GPIO_Port, dht22_Pin, GPIO_PIN_SET);
    delay_us(30);
    DHT22_SetInput();

    count = 0;
    while (HAL_GPIO_ReadPin(dht22_GPIO_Port, dht22_Pin) == GPIO_PIN_SET)
        if (++count > 10000) return 0;

    count = 0;
    while (HAL_GPIO_ReadPin(dht22_GPIO_Port, dht22_Pin) == GPIO_PIN_RESET)
        if (++count > 10000) return 0;

    count = 0;
    while (HAL_GPIO_ReadPin(dht22_GPIO_Port, dht22_Pin) == GPIO_PIN_SET)
        if (++count > 10000) return 0;

    __disable_irq();
    for (int i = 0; i < 40; i++)
    {
        count = 0;
        while (HAL_GPIO_ReadPin(dht22_GPIO_Port, dht22_Pin) == GPIO_PIN_RESET)
            if (++count > 10000) { __enable_irq(); return 0; }

        uint32_t t1 = SysTick->VAL;
        count = 0;
        while (HAL_GPIO_ReadPin(dht22_GPIO_Port, dht22_Pin) == GPIO_PIN_SET)
            if (++count > 10000) { __enable_irq(); return 0; }
        uint32_t t2 = SysTick->VAL;

        uint32_t dur = (t1 >= t2) ? (t1 - t2) : (t1 + SysTick->LOAD + 1 - t2);

        data[i / 8] <<= 1;
        if (dur > 768) data[i / 8] |= 1;
    }
    __enable_irq();

    for (int i = 0; i < 5; i++) dht_raw[i] = data[i];

    *humidity = ((data[0] << 8) | data[1]) / 10.0f;
    uint16_t rawTemp = (data[2] << 8) | data[3];
    *temperature = (rawTemp & 0x8000) ? -((rawTemp & 0x7FFF) / 10.0f) : (rawTemp / 10.0f);

    return 1;
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
  MX_USART2_UART_Init();
  MX_ADC_Init();
  /* USER CODE BEGIN 2 */
  HAL_ADCEx_Calibration_Start(&hadc, ADC_SINGLE_ENDED);
  ADC1->CR |= ADC_CR_ADEN;
  while (!(ADC1->ISR & ADC_ISR_ADRDY));
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  float prevTemp = 0.0f;
  uint8_t firstRead = 1;

  while (1)
  {
    uint32_t mq2Val   = read_adc(1U << 0); /* PA0 -> ADC_IN0 -> MQ2  */
    uint32_t chselr_mq2 = ADC1->CHSELR;
    uint32_t flameVal = read_adc(1U << 8); /* PB0 -> ADC_IN8 -> Flame */
    uint32_t chselr_flame = ADC1->CHSELR;

    {
        char dbg[64];
        int dlen = snprintf(dbg, sizeof(dbg), "[DBG] CHSELR_MQ2=0x%lX CHSELR_FL=0x%lX\r\n",
                            chselr_mq2, chselr_flame);
        HAL_UART_Transmit(&huart2, (uint8_t *)dbg, dlen, 100);
    }

    float temperature = 0.0f, humidity = 0.0f;
    uint8_t dhtOk = DHT22_Read(&temperature, &humidity);

    char buf[96];
    int len;

    int tempInt = (int)temperature;
    int tempDec = (int)((temperature - tempInt) * 10);
    int humInt  = (int)humidity;
    int humDec  = (int)((humidity - humInt) * 10);

    if (dhtOk)
      len = snprintf(buf, sizeof(buf), "MQ2: %lu | Flame: %lu | Temp: %d.%d C | Nem: %d.%d%%\r\n",
                     mq2Val, flameVal, tempInt, tempDec, humInt, humDec);
    else
      len = snprintf(buf, sizeof(buf), "MQ2: %lu | Flame: %lu | DHT22: hata\r\n", mq2Val, flameVal);

    HAL_UART_Transmit(&huart2, (uint8_t *)buf, len, 100);

    uint8_t mq2Alarm   = (mq2Val > 1500);
    uint8_t flameAlarm = (flameVal < 2000);
    uint8_t tempAlarm  = 0;

    if (dhtOk)
    {
      if (!firstRead && (temperature - prevTemp) > 3.0f)
        tempAlarm = 1;
      prevTemp = temperature;
      firstRead = 0;
    }

    uint8_t alarmSkor = mq2Alarm + flameAlarm + tempAlarm;

    if (alarmSkor == 0)
    {
      HAL_UART_Transmit(&huart2, (uint8_t *)"Durum: Normal\r\n", 15, 100);
    }
    else if (alarmSkor == 1)
    {
      if (mq2Alarm)
        HAL_UART_Transmit(&huart2, (uint8_t *)"[UYARI] Duman tespit edildi!\r\n", 30, 100);
      if (flameAlarm)
        HAL_UART_Transmit(&huart2, (uint8_t *)"[UYARI] Alev tespit edildi!\r\n", 29, 100);
      if (tempAlarm)
        HAL_UART_Transmit(&huart2, (uint8_t *)"[UYARI] Ani sicaklik artisi!\r\n", 30, 100);
    }
    else if (alarmSkor == 2)
    {
      HAL_UART_Transmit(&huart2, (uint8_t *)"[TEHLIKE] Yangin tehlikesi! Kontrol edin!\r\n", 43, 100);
    }
    else
    {
      HAL_UART_Transmit(&huart2, (uint8_t *)"[YANGIN] TAHLIYE! TAHLIYE! TAHLIYE!\r\n", 37, 100);
    }

    HAL_Delay(100);
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
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART2;
  PeriphClkInit.Usart2ClockSelection = RCC_USART2CLKSOURCE_PCLK1;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC_Init(void)
{

  /* USER CODE BEGIN ADC_Init 0 */

  /* USER CODE END ADC_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC_Init 1 */

  /* USER CODE END ADC_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc.Instance = ADC1;
  hadc.Init.OversamplingMode = DISABLE;
  hadc.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV1;
  hadc.Init.Resolution = ADC_RESOLUTION_12B;
  hadc.Init.SamplingTime = ADC_SAMPLETIME_3CYCLES_5;
  hadc.Init.ScanConvMode = ADC_SCAN_DIRECTION_FORWARD;
  hadc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc.Init.ContinuousConvMode = ENABLE;
  hadc.Init.DiscontinuousConvMode = DISABLE;
  hadc.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc.Init.DMAContinuousRequests = DISABLE;
  hadc.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc.Init.LowPowerAutoWait = DISABLE;
  hadc.Init.LowPowerFrequencyMode = DISABLE;
  hadc.Init.LowPowerAutoPowerOff = DISABLE;
  if (HAL_ADC_Init(&hadc) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel to be converted.
  */
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = ADC_RANK_CHANNEL_NUMBER;
  if (HAL_ADC_ConfigChannel(&hadc, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel to be converted.
  */
  sConfig.Channel = ADC_CHANNEL_8;
  if (HAL_ADC_ConfigChannel(&hadc, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC_Init 2 */

  /* USER CODE END ADC_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

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
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(dht22_GPIO_Port, dht22_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : dht22_Pin */
  GPIO_InitStruct.Pin = dht22_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(dht22_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

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
