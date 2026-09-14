#ifndef PROJECT_NAME_APP_DISPLAY_AIP1620_H
#define PROJECT_NAME_APP_DISPLAY_AIP1620_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define APP_DISPLAY_AIP1620_DIGIT_COUNT 4U
#define APP_DISPLAY_AIP1620_BLINK_MIN_INTERVAL_MS 50U

  typedef enum
  {
    APP_DISPLAY_AIP1620_BRIGHTNESS_1 = 0,
    APP_DISPLAY_AIP1620_BRIGHTNESS_2,
    APP_DISPLAY_AIP1620_BRIGHTNESS_3,
    APP_DISPLAY_AIP1620_BRIGHTNESS_4,
    APP_DISPLAY_AIP1620_BRIGHTNESS_5,
    APP_DISPLAY_AIP1620_BRIGHTNESS_COUNT,
  } app_display_aip1620_brightness_t;

  typedef enum
  {
    APP_DISPLAY_AIP1620_ICON_NONE = 0U,
    APP_DISPLAY_AIP1620_ICON_1 = (1U << 0),
    APP_DISPLAY_AIP1620_ICON_2 = (1U << 1),
    APP_DISPLAY_AIP1620_ICON_3 = (1U << 2),
    APP_DISPLAY_AIP1620_ICON_4 = (1U << 3),
    APP_DISPLAY_AIP1620_ICON_ALL = 0x0FU,
  } app_display_aip1620_icon_t;

  typedef enum
  {
    APP_DISPLAY_AIP1620_BLINK_NONE = 0U,
    APP_DISPLAY_AIP1620_BLINK_DIGIT_1 = (1U << 0),
    APP_DISPLAY_AIP1620_BLINK_DIGIT_2 = (1U << 1),
    APP_DISPLAY_AIP1620_BLINK_DIGIT_3 = (1U << 2),
    APP_DISPLAY_AIP1620_BLINK_DIGIT_4 = (1U << 3),
    APP_DISPLAY_AIP1620_BLINK_COLON = (1U << 4),
    APP_DISPLAY_AIP1620_BLINK_ICON_1 = (1U << 5),
    APP_DISPLAY_AIP1620_BLINK_ICON_2 = (1U << 6),
    APP_DISPLAY_AIP1620_BLINK_ICON_3 = (1U << 7),
    APP_DISPLAY_AIP1620_BLINK_ICON_4 = (1U << 8),
    APP_DISPLAY_AIP1620_BLINK_ALL = 0x01FFU,
  } app_display_aip1620_blink_t;

  /**
   * 初始化显示服务。此后所有显示请求都通过消息队列交给唯一显示任务执行。
   */
  esp_err_t App_DisplayAIP1620_Init(void);

  /** 停止显示任务、删除消息队列并释放底层驱动。 */
  esp_err_t App_DisplayAIP1620_Deinit(void);

  esp_err_t App_DisplayAIP1620_Clear(void);
  esp_err_t App_DisplayAIP1620_SetEnabled(bool enabled);
  esp_err_t App_DisplayAIP1620_SetBrightness(
      app_display_aip1620_brightness_t brightness);
  esp_err_t App_DisplayAIP1620_ShowTime(uint8_t hour, uint8_t minute,
                                        bool colon_enabled);
  esp_err_t App_DisplayAIP1620_ShowNumber(uint16_t number, bool leading_zero);
  esp_err_t App_DisplayAIP1620_SetColon(bool enabled);
  esp_err_t App_DisplayAIP1620_SetIcons(uint8_t icon_mask);
  esp_err_t App_DisplayAIP1620_SetIcon(uint8_t icon_mask, bool enabled);

  /** 启动或更新闪烁策略；显示内容仍可在闪烁期间正常更新。 */
  esp_err_t App_DisplayAIP1620_StartBlink(uint16_t content_mask,
                                          uint32_t interval_ms);

  /** 停止闪烁并立即恢复完整显示内容。 */
  esp_err_t App_DisplayAIP1620_StopBlink(void);

#ifdef __cplusplus
}
#endif

#endif /* PROJECT_NAME_APP_DISPLAY_AIP1620_H */
