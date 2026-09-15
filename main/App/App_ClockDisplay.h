/**
 * @file App_ClockDisplay.h
 * @brief RTC 时钟显示服务的应用层接口。
 *
 * 本模块负责初始化 RTC 和 AiP1620 显示服务，并创建时钟任务，每秒读取一次
 * RTC，将当前时间更新到四位数码屏。显示命令最终由 AiP1620 显示任务通过
 * 消息队列串行执行。
 *
 * 典型用法：
 * @code
 * ESP_ERROR_CHECK(App_ClockDisplay_Init());
 * ESP_ERROR_CHECK(
 *     App_ClockDisplay_SetBrightness(APP_DISPLAY_AIP1620_BRIGHTNESS_3));
 *
 * // 应用退出时释放服务。
 * ESP_ERROR_CHECK(App_ClockDisplay_Deinit());
 * @endcode
 */

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

  /**
   * @brief 初始化 RTC 时钟显示服务。
   *
   * 依次初始化内部 RTC、AiP1620 显示服务、时钟控制队列和时钟刷新任务。
   * 时钟任务启动后会立即刷新一次显示，之后每隔 1 秒读取 RTC 并显示为
   * `HH:MM`。RTC 时间无效或读取失败时显示 `00:00`。
   *
   * @retval ESP_OK 初始化成功。
   * @retval ESP_ERR_INVALID_STATE 时钟显示服务已经初始化，或显示服务状态
   *                               不允许再次初始化。
   * @retval ESP_ERR_NO_MEM 创建时钟控制队列或时钟任务失败。
   * @return 其他值表示 RTC 或 AiP1620 显示服务初始化失败。
   */
  esp_err_t App_ClockDisplay_Init(void);

  /**
   * @brief 停止时钟显示服务并释放相关资源。
   *
   * 向时钟任务发送停止命令，等待任务退出，删除时钟控制队列，然后关闭
   * AiP1620 显示服务并释放其资源。时钟任务尚未运行时，本函数仍会尝试关闭
   * AiP1620 显示服务，以清理可能存在的部分初始化状态。
   *
   * @note 不能在时钟任务自身的上下文中调用本函数。
   *
   * @retval ESP_OK 停止成功，或相关服务原本就未初始化。
   * @retval ESP_ERR_INVALID_STATE 在时钟任务自身上下文中调用。
   * @retval ESP_ERR_NO_MEM 创建停止响应队列失败。
   * @retval ESP_ERR_TIMEOUT 停止命令未能在 50 ms 内进入时钟控制队列。
   * @return 其他值表示 AiP1620 显示服务关闭失败。
   */
  esp_err_t App_ClockDisplay_Deinit(void);

  /**
   * @brief 设置 RTC 时间并立即提交一次显示刷新。
   *
   * 先将完整日期和时间写入 RTC；设置成功后，将其中的小时和分钟提交给
   * AiP1620 显示服务。可由蓝牙、网络、按键或其他校时模块调用。后续显示
   * 仍由时钟任务每秒刷新。
   *
   * @param[in] time 要设置的完整 RTC 日期和时间，不能为 `NULL`，各字段必须
   *                 满足 @ref inf_rtc_time_t 的有效范围。
   *
   * @note 返回 `ESP_OK` 表示 RTC 设置成功且显示命令已进入消息队列，不表示
   *       硬件显示已经完成刷新。
   *
   * @retval ESP_OK RTC 设置成功，且显示刷新命令已成功进入消息队列。
   * @retval ESP_ERR_INVALID_ARG `time` 为空或包含无效日期、时间字段。
   * @retval ESP_ERR_INVALID_STATE AiP1620 显示服务尚未初始化。
   * @retval ESP_ERR_TIMEOUT 显示命令未能在规定时间内进入消息队列。
   * @return 其他值表示 RTC 设置失败。
   */
  esp_err_t App_ClockDisplay_SetTime(const inf_rtc_time_t *time);

  /**
   * @brief 开启或关闭时钟显示输出。
   *
   * 关闭显示时保留显示 RAM 和亮度。时钟任务仍会每秒更新 RAM，因此重新
   * 开启显示后会显示最新时间。
   *
   * @param[in] enabled `true` 开启显示，`false` 关闭显示。
   *
   * @retval ESP_OK 设置命令已成功进入 AiP1620 显示队列。
   * @retval ESP_ERR_INVALID_STATE AiP1620 显示服务尚未初始化。
   * @retval ESP_ERR_TIMEOUT 命令未能在规定时间内进入消息队列。
   */
  esp_err_t App_ClockDisplay_SetEnabled(bool enabled);

  /**
   * @brief 设置时钟显示亮度。
   *
   * 该操作不改变当前时间、冒号、图标、闪烁策略和显示开关状态。
   *
   * @param[in] brightness 目标亮度档位，范围为第 1 档至第 5 档。
   *
   * @retval ESP_OK 设置命令已成功进入 AiP1620 显示队列。
   * @retval ESP_ERR_INVALID_ARG `brightness` 不是有效亮度档位。
   * @retval ESP_ERR_INVALID_STATE AiP1620 显示服务尚未初始化。
   * @retval ESP_ERR_TIMEOUT 命令未能在规定时间内进入消息队列。
   */
  esp_err_t App_ClockDisplay_SetBrightness(
      app_display_aip1620_brightness_t brightness);

  /**
   * @brief 一次性设置时钟界面的全部四个图标。
   *
   * `icon_mask` 中置位的图标点亮，未置位的图标熄灭；有效范围之外的高位
   * 会被忽略。传入 `APP_DISPLAY_AIP1620_ICON_NONE` 可熄灭全部图标。该操作
   * 不改变时间、冒号、亮度和显示开关状态。
   *
   * @param[in] icon_mask 由 @ref app_display_aip1620_icon_t
   * 组合而成的图标掩码。
   *
   * @retval ESP_OK 设置命令已成功进入 AiP1620 显示队列。
   * @retval ESP_ERR_INVALID_STATE AiP1620 显示服务尚未初始化。
   * @retval ESP_ERR_TIMEOUT 命令未能在规定时间内进入消息队列。
   */
  esp_err_t App_ClockDisplay_SetIcons(uint8_t icon_mask);

  /**
   * @brief 开启或关闭低电量闪烁提示。
   *
   * 开启时点亮最下方的 ICON4，并使其每隔 500 ms 在显示和隐藏之间切换，
   * 完整闪烁周期约为 1 秒。关闭时停止当前闪烁策略，并熄灭 ICON4。
   *
   * @param[in] low_battery `true` 开启低电量提示，`false` 关闭低电量提示。
   *
   * @note 关闭低电量提示会调用 `App_DisplayAIP1620_StopBlink()`，因此也会
   *       停止当前显示服务中其他内容正在执行的闪烁策略。
   * @note 本函数会连续发送两个显示命令；返回成功表示命令已按顺序进入
   *       消息队列，不表示硬件已经完成显示更新。
   *
   * @retval ESP_OK 低电量提示命令已成功进入 AiP1620 显示队列。
   * @retval ESP_ERR_INVALID_STATE AiP1620 显示服务尚未初始化。
   * @retval ESP_ERR_TIMEOUT 任一命令未能在规定时间内进入消息队列。
   * @return 其他值表示底层显示接口执行失败。
   */
  esp_err_t App_ClockDisplay_SetLowBattery(bool low_battery);

#ifdef __cplusplus
}
#endif

#endif /* PROJECT_NAME_APP_CLOCK_DISPLAY_H */
