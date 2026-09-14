#ifndef PROJECT_NAME_APP_CLOCK_DISPLAY_H
#define PROJECT_NAME_APP_CLOCK_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

#include "APP_DisplayAIP1620.h"
#include "Inf_RTC.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

  /** 初始化 RTC、显示消息队列和时钟刷新任务。 */
  esp_err_t App_ClockDisplay_Init(void);

  /** 停止时钟刷新任务并释放显示服务。 */
  esp_err_t App_ClockDisplay_Deinit(void);

  /** 可由蓝牙、网络、按键或其他校时模块调用。 */
  esp_err_t App_ClockDisplay_SetTime(const inf_rtc_time_t *time);

  esp_err_t App_ClockDisplay_SetEnabled(bool enabled);
  esp_err_t App_ClockDisplay_SetBrightness(
      app_display_aip1620_brightness_t brightness);
  esp_err_t App_ClockDisplay_SetIcons(uint8_t icon_mask);

  /** 使用最下方的 ICON4 以 500 ms 间隔提示低电量。 */
  esp_err_t App_ClockDisplay_SetLowBattery(bool low_battery);

#ifdef __cplusplus
}
#endif

#endif /* PROJECT_NAME_APP_CLOCK_DISPLAY_H */
