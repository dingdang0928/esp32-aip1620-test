#include <stdio.h>
#include "Inf/Inf_AIP1620.h"
#include "Inf_AIP1620.h"
#include "Inf_RTC.h"
#include "Inf_RGB.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static void RTC_DisplayToAIP1620(void);
inf_rtc_time_t set_time = {
    .year = 2026, .month = 9, .day = 11, .hour = 10, .minute = 35, .second = 0};
static void RTC_TEST(void);

extern volatile uint8_t RTC_FLAG;
void app_main(void)
{
  inf_rgb_init();
  Inf_AIP_1620_Power_Test();
  RTC_TEST();
  bool rtc_flag_old = RTC_FLAG;
  while (1)
  {
    // Inf_RTC_PrintTime();
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (RTC_FLAG)
    {
      RTC_DisplayToAIP1620();
    }
    else if (rtc_flag_old != RTC_FLAG)
    {
      /*
       * true -> false
       * 只会进入一次
       */
      Inf_AIP_1620_ClearAll();
    }
    /* 保存本次状态 */
    rtc_flag_old = RTC_FLAG;
  }
}

static void RTC_DisplayToAIP1620(void)
{
  inf_rtc_time_t rtc_time = {0};

  if (Inf_RTC_GetTime(&rtc_time) != ESP_OK)
  {
    return;
  }

  uint8_t min_tens = rtc_time.minute / 10U;
  uint8_t min_ones = rtc_time.minute % 10U;

  uint8_t sec_tens = rtc_time.second / 10U;
  uint8_t sec_ones = rtc_time.second % 10U;

  Inf_AIP_1620_Display_Digit(0, min_tens);
  Inf_AIP_1620_Display_Digit(1, min_ones);

  Inf_AIP_1620_Display_Digit(2, sec_tens);
  Inf_AIP_1620_Display_Digit(3, sec_ones);

  Inf_AIP_1620_Display_Mid_Dot(true);
}

static void RTC_TEST(void)
{
  Inf_RTC_Init();
  ESP_ERROR_CHECK(Inf_RTC_SetTime(&set_time));
}
