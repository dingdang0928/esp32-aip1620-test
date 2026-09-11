#ifndef PROJECT_NAME_APP_CLOCK_DISPLAY_H
#define PROJECT_NAME_APP_CLOCK_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

#include "Inf_AIP1620.h"
#include "Inf_RTC.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 初始化 RTC、AiP1620 和时钟显示任务。
 * 时间有效时显示 HH:MM；时间无效时显示 00:00。
 */
esp_err_t App_ClockDisplay_Init(void);

/** 上层校时入口，可由按键、蓝牙、网络或调试串口调用。 */
esp_err_t App_ClockDisplay_SetTime(const inf_rtc_time_t *time);

/** 开启/关闭整块显示，关闭时保留显示内容。 */
void App_ClockDisplay_SetEnabled(bool enabled);

/** 设置产品定义的五档亮度。 */
esp_err_t App_ClockDisplay_SetBrightness(
    inf_aip1620_brightness_t brightness);

/** 设置常亮状态图标；多个图标可按位或组合。 */
void App_ClockDisplay_SetIcons(uint8_t icon_mask);

/** 设置低电量状态；开启后使用最下方 ICON4 闪烁提醒。 */
void App_ClockDisplay_SetLowBattery(bool low_battery);

#ifdef __cplusplus
}
#endif

#endif /* PROJECT_NAME_APP_CLOCK_DISPLAY_H */
