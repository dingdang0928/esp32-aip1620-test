#ifndef PROJECT_NAME_INF_AIP1620_H
#define PROJECT_NAME_INF_AIP1620_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** 四位数字显示，位置从左到右为 0~3。 */
#define INF_AIP1620_DIGIT_COUNT 4U

  /** 产品使用的五档亮度；底层对应 AiP1620 的 0、1、2、3、7 档。 */
  typedef enum
  {
    INF_AIP1620_BRIGHTNESS_1 = 0,
    INF_AIP1620_BRIGHTNESS_2,
    INF_AIP1620_BRIGHTNESS_3,
    INF_AIP1620_BRIGHTNESS_4,
    INF_AIP1620_BRIGHTNESS_5,
    INF_AIP1620_BRIGHTNESS_COUNT,
  } inf_aip1620_brightness_t;

  /** GRID6 上四个产品图标的掩码，从上到下对应 bit0~bit3。 */
  typedef enum
  {
    INF_AIP1620_ICON_NONE = 0U,
    INF_AIP1620_ICON_1 = (1U << 0),
    INF_AIP1620_ICON_2 = (1U << 1),
    INF_AIP1620_ICON_3 = (1U << 2),
    INF_AIP1620_ICON_4 = (1U << 3),
    INF_AIP1620_ICON_ALL = 0x0FU,
  } inf_aip1620_icon_t;

  /** 初始化 GPIO 和显示 RAM，并开启显示。 */
  esp_err_t Inf_AIP_1620_Init(void);

  /** 关闭显示、清空软件缓存，并将通信 GPIO 释放为高阻态。 */
  esp_err_t Inf_AIP_1620_Deinit(void);

  /** 清空数字、冒号和图标，显示仍保持开启。 */
  void Inf_AIP_1620_ClearAll(void);

  /** 开启或关闭显示输出；关闭时保留显示 RAM 内容。 */
  void Inf_AIP_1620_Display_Enable(void);
  void Inf_AIP_1620_Display_Disable(void);

  /** 设置产品亮度档位。 */
  esp_err_t Inf_AIP_1620_Set_Brightness_Level(
      inf_aip1620_brightness_t brightness);

  /** 显示 0~9999，leading_zero 为 true 时补前导零。 */
  void Inf_AIP_1620_Display_Number(uint16_t number, bool leading_zero);

  /** 一次刷新 HH:MM，避免逐位刷新造成闪烁。 */
  esp_err_t Inf_AIP_1620_Display_Time(uint8_t hour, uint8_t minute,
                                      bool colon_enable);

  /** 控制中间冒号。 */
  void Inf_AIP_1620_Display_Mid_Dot(bool enable);

  /** 控制产品图标，可组合 inf_aip1620_icon_t 中的多个掩码。 */
  void Inf_AIP_1620_Display_Icons(uint8_t icon_mask);

#ifdef __cplusplus
}
#endif

#endif /* PROJECT_NAME_INF_AIP1620_H */
