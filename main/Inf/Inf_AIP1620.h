#ifndef PROJECT_NAME_INF_AIP1620_H
#define PROJECT_NAME_INF_AIP1620_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** @brief AiP1620 显示屏的数字位数量。 */
#define INF_AIP1620_DIGIT_COUNT 4U

/** @brief 表示数字位不显示任何内容的特殊值。 */
#define INF_AIP1620_DIGIT_BLANK UINT8_MAX

  typedef enum
  {
    INF_AIP1620_BRIGHTNESS_1 = 0,
    INF_AIP1620_BRIGHTNESS_2,
    INF_AIP1620_BRIGHTNESS_3,
    INF_AIP1620_BRIGHTNESS_4,
    INF_AIP1620_BRIGHTNESS_5,
    INF_AIP1620_BRIGHTNESS_COUNT,
  } inf_aip1620_brightness_t;

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
   * @brief AiP1620 一帧逻辑显示内容。
   *
   * digits[] 对应各数字显示位，每位可取 0~9 或
   * INF_AIP1620_DIGIT_BLANK。
   */
  typedef struct
  {
    uint8_t digits[INF_AIP1620_DIGIT_COUNT]; /**< 四个数字显示位。 */
    bool colon_enabled;                      /**< 是否点亮中间冒号。 */
    uint8_t icon_mask;                       /**< 要点亮的图标位掩码。 */
  } inf_aip1620_frame_t;

  /** 初始化同步硬件驱动。该层不创建任务、不持有消息队列。 */
  esp_err_t Inf_AIP1620_Init(void);

  /** 关闭显示并将通信 GPIO 释放为高阻态。 */
  esp_err_t Inf_AIP1620_Deinit(void);

  /** 写入一帧完整的显示内容。 */
  esp_err_t Inf_AIP1620_WriteFrame(const inf_aip1620_frame_t *frame);

  /** 清空显示 RAM，但不改变显示开关和亮度。 */
  esp_err_t Inf_AIP1620_Clear(void);

  /** 开启或关闭显示输出；关闭时保留 RAM 内容。 */
  esp_err_t Inf_AIP1620_SetEnabled(bool enabled);

  /** 设置产品定义的五档亮度。 */
  esp_err_t Inf_AIP1620_SetBrightness(inf_aip1620_brightness_t brightness);

#ifdef __cplusplus
}
#endif

#endif /* PROJECT_NAME_INF_AIP1620_H */
