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

  /**
   * 可闪烁的显示内容掩码。多个目标可使用按位或组合，例如：
   * INF_AIP1620_BLINK_DIGIT_1 | INF_AIP1620_BLINK_COLON。
   */
  typedef enum
  {
    INF_AIP1620_BLINK_NONE = 0U,
    INF_AIP1620_BLINK_DIGIT_1 = (1U << 0),
    INF_AIP1620_BLINK_DIGIT_2 = (1U << 1),
    INF_AIP1620_BLINK_DIGIT_3 = (1U << 2),
    INF_AIP1620_BLINK_DIGIT_4 = (1U << 3),
    INF_AIP1620_BLINK_COLON = (1U << 4),
    INF_AIP1620_BLINK_ICON_1 = (1U << 5),
    INF_AIP1620_BLINK_ICON_2 = (1U << 6),
    INF_AIP1620_BLINK_ICON_3 = (1U << 7),
    INF_AIP1620_BLINK_ICON_4 = (1U << 8),
    INF_AIP1620_BLINK_ALL = 0x01FFU,
  } inf_aip1620_blink_content_t;

  /** 初始化 GPIO 和显示 RAM，并开启显示。 */
  esp_err_t Inf_AIP_1620_Init(void);

  /** 关闭显示、清空软件缓存，并将通信 GPIO 释放为高阻态。 */
  esp_err_t Inf_AIP_1620_Deinit(void);

  /** 一次刷新 HH:MM，避免逐位刷新造成闪烁。 */
  // 核心函数,APP层显示主要依靠该函数进行显示
  esp_err_t Inf_AIP_1620_Display_Time(uint8_t hour, uint8_t minute,
                                      bool colon_enable);

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

  /** 控制中间冒号。 */
  void Inf_AIP_1620_Display_Mid_Dot(bool enable);

  /** 控制产品图标，可组合 inf_aip1620_icon_t 中的多个掩码。 */
  void Inf_AIP_1620_Display_Icons(uint8_t icon_mask);

  /**
   * 启动或更新内容闪烁。content_mask 可组合多个显示目标；interval_ms 是每次
   * 亮/灭状态保持的时间，最小为 50 ms。原显示内容会被保留并继续接受更新。
   */
  esp_err_t Inf_AIP_1620_Blink_Start(uint16_t content_mask,
                                     uint32_t interval_ms);

  /** 停止闪烁并立即恢复完整显示内容；未启动时调用也返回 ESP_OK。 */
  esp_err_t Inf_AIP_1620_Blink_Stop(void);

#ifdef __cplusplus
}
#endif

#endif /* PROJECT_NAME_INF_AIP1620_H */
