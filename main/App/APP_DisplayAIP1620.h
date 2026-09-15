#ifndef PROJECT_NAME_APP_DISPLAY_AIP1620_H
#define PROJECT_NAME_APP_DISPLAY_AIP1620_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** @brief AiP1620 显示屏的数字位数量。 */
#define APP_DISPLAY_AIP1620_DIGIT_COUNT 4U

/** @brief 闪烁状态允许的最小切换间隔，单位为毫秒。 */
#define APP_DISPLAY_AIP1620_BLINK_MIN_INTERVAL_MS 50U

  /** @brief 产品对外提供的五档显示亮度。 */
  typedef enum
  {
    APP_DISPLAY_AIP1620_BRIGHTNESS_1 = 0, /**< 第 1 档，最低亮度。 */
    APP_DISPLAY_AIP1620_BRIGHTNESS_2,     /**< 第 2 档亮度。 */
    APP_DISPLAY_AIP1620_BRIGHTNESS_3,     /**< 第 3 档亮度。 */
    APP_DISPLAY_AIP1620_BRIGHTNESS_4,     /**< 第 4 档亮度。 */
    APP_DISPLAY_AIP1620_BRIGHTNESS_5,     /**< 第 5 档，最高亮度。 */
    APP_DISPLAY_AIP1620_BRIGHTNESS_COUNT, /**< 亮度档位数量，不是有效档位。 */
  } app_display_aip1620_brightness_t;

  /**
   * @brief 四个独立图标的位掩码。
   *
   * 多个图标可以使用按位或运算组合，例如：
   * `APP_DISPLAY_AIP1620_ICON_1 | APP_DISPLAY_AIP1620_ICON_3`。
   */
  typedef enum
  {
    APP_DISPLAY_AIP1620_ICON_NONE = 0U,     /**< 不选择任何图标。 */
    APP_DISPLAY_AIP1620_ICON_1 = (1U << 0), /**< 图标 1。 */
    APP_DISPLAY_AIP1620_ICON_2 = (1U << 1), /**< 图标 2。 */
    APP_DISPLAY_AIP1620_ICON_3 = (1U << 2), /**< 图标 3。 */
    APP_DISPLAY_AIP1620_ICON_4 = (1U << 3), /**< 图标 4。 */
    APP_DISPLAY_AIP1620_ICON_ALL = 0x0FU,   /**< 全部四个图标。 */
  } app_display_aip1620_icon_t;

  /**
   * @brief 可参与闪烁的显示内容位掩码。
   *
   * 数字位、冒号和图标可以使用按位或运算组合，以便同时闪烁。
   */
  typedef enum
  {
    APP_DISPLAY_AIP1620_BLINK_NONE = 0U,           /**< 不选择任何内容。 */
    APP_DISPLAY_AIP1620_BLINK_DIGIT_1 = (1U << 0), /**< 第 1 个数字位。 */
    APP_DISPLAY_AIP1620_BLINK_DIGIT_2 = (1U << 1), /**< 第 2 个数字位。 */
    APP_DISPLAY_AIP1620_BLINK_DIGIT_3 = (1U << 2), /**< 第 3 个数字位。 */
    APP_DISPLAY_AIP1620_BLINK_DIGIT_4 = (1U << 3), /**< 第 4 个数字位。 */
    APP_DISPLAY_AIP1620_BLINK_COLON = (1U << 4),   /**< 中间冒号。 */
    APP_DISPLAY_AIP1620_BLINK_ICON_1 = (1U << 5),  /**< 图标 1。 */
    APP_DISPLAY_AIP1620_BLINK_ICON_2 = (1U << 6),  /**< 图标 2。 */
    APP_DISPLAY_AIP1620_BLINK_ICON_3 = (1U << 7),  /**< 图标 3。 */
    APP_DISPLAY_AIP1620_BLINK_ICON_4 = (1U << 8),  /**< 图标 4。 */
    APP_DISPLAY_AIP1620_BLINK_ALL = 0x01FFU,       /**< 全部显示内容。 */
  } app_display_aip1620_blink_t;

  /**
   * @brief 初始化 AiP1620 显示服务。
   *
   * 初始化底层驱动，将默认亮度设为第 3 档，并创建显示命令队列和唯一的
   * 显示任务。初始化成功后，其他显示接口会把命令发送到该队列，由显示
   * 任务串行操作硬件。
   *
   * @retval ESP_OK 初始化成功。
   * @retval ESP_ERR_INVALID_STATE 显示服务已经初始化。
   * @retval ESP_ERR_NO_MEM 创建消息队列或显示任务失败。
   * @return 其他值表示底层 AiP1620 驱动初始化或亮度设置失败。
   */
  esp_err_t App_DisplayAIP1620_Init(void);

  /**
   * @brief 停止 AiP1620 显示服务并释放相关资源。
   *
   * 向显示任务发送关闭命令，等待显示任务关闭硬件、释放 GPIO，然后删除
   * 消息队列。重复调用未初始化的显示服务会直接返回成功。
   *
   * @note 不能在显示任务自身的上下文中调用本函数。
   *
   * @retval ESP_OK 停止成功，或显示服务尚未初始化。
   * @retval ESP_ERR_INVALID_STATE 在显示任务自身上下文中调用。
   * @retval ESP_ERR_NO_MEM 创建关闭响应队列失败。
   * @retval ESP_ERR_TIMEOUT 关闭命令未能在规定时间内进入消息队列。
   * @return 其他值表示底层驱动关闭失败。
   */
  esp_err_t App_DisplayAIP1620_Deinit(void);

  /**
   * @brief 清空数字、冒号和全部图标。
   *
   * 清空当前逻辑显示帧，但不改变显示开关状态和亮度设置。命令由显示任务
   * 异步执行。
   *
   * @retval ESP_OK 清屏命令已成功进入消息队列。
   * @retval ESP_ERR_INVALID_STATE 显示服务尚未初始化。
   * @retval ESP_ERR_TIMEOUT 命令未能在规定时间内进入消息队列。
   */
  esp_err_t App_DisplayAIP1620_Clear(void);

  /**
   * @brief 开启或关闭显示输出。
   *
   * 关闭显示时保留当前显示 RAM 内容和亮度；再次开启后恢复显示这些内容。
   * 命令由显示任务异步执行。
   *
   * @param[in] enabled `true` 开启显示，`false` 关闭显示。
   *
   * @retval ESP_OK 设置命令已成功进入消息队列。
   * @retval ESP_ERR_INVALID_STATE 显示服务尚未初始化。
   * @retval ESP_ERR_TIMEOUT 命令未能在规定时间内进入消息队列。
   */
  esp_err_t App_DisplayAIP1620_SetEnabled(bool enabled);

  /**
   * @brief 设置显示亮度。
   *
   * 亮度范围为第 1 档至第 5 档。该操作不改变数字、冒号、图标和显示开关
   * 状态。命令由显示任务异步执行。
   *
   * @param[in] brightness 目标亮度档位。
   *
   * @retval ESP_OK 设置命令已成功进入消息队列。
   * @retval ESP_ERR_INVALID_ARG `brightness` 不是有效亮度档位。
   * @retval ESP_ERR_INVALID_STATE 显示服务尚未初始化。
   * @retval ESP_ERR_TIMEOUT 命令未能在规定时间内进入消息队列。
   */
  esp_err_t App_DisplayAIP1620_SetBrightness(
      app_display_aip1620_brightness_t brightness);

  /**
   * @brief 以四位数字格式显示时间。
   *
   * 将时间显示为 `HHMM`，并根据 `colon_enabled` 控制中间冒号。该操作保留
   * 当前图标、亮度和显示开关状态。命令由显示任务异步执行。
   *
   * @param[in] hour 小时，取值范围为 0~23。
   * @param[in] minute 分钟，取值范围为 0~59。
   * @param[in] colon_enabled `true` 点亮冒号，`false` 熄灭冒号。
   *
   * @retval ESP_OK 显示命令已成功进入消息队列。
   * @retval ESP_ERR_INVALID_ARG 小时或分钟超出有效范围。
   * @retval ESP_ERR_INVALID_STATE 显示服务尚未初始化。
   * @retval ESP_ERR_TIMEOUT 命令未能在规定时间内进入消息队列。
   */
  esp_err_t App_DisplayAIP1620_ShowTime(uint8_t hour, uint8_t minute,
                                        bool colon_enabled);

  /**
   * @brief 显示一个非负整数。
   *
   * 数值小于四位时，可选择是否显示前导零；数值 `0` 始终至少显示最右侧
   * 的一个零。大于 `9999` 的输入按 `9999` 显示。该操作保留当前冒号、
   * 图标、亮度和显示开关状态。命令由显示任务异步执行。
   *
   * @param[in] number 要显示的数值。
   * @param[in] leading_zero `true` 补齐前导零，`false` 隐藏前导零。
   *
   * @retval ESP_OK 显示命令已成功进入消息队列。
   * @retval ESP_ERR_INVALID_STATE 显示服务尚未初始化。
   * @retval ESP_ERR_TIMEOUT 命令未能在规定时间内进入消息队列。
   */
  esp_err_t App_DisplayAIP1620_ShowNumber(uint16_t number, bool leading_zero);

  /**
   * @brief 单独设置中间冒号的显示状态。
   *
   * 该操作保留当前数字、图标、亮度和显示开关状态。命令由显示任务异步
   * 执行。
   *
   * @param[in] enabled `true` 点亮冒号，`false` 熄灭冒号。
   *
   * @retval ESP_OK 设置命令已成功进入消息队列。
   * @retval ESP_ERR_INVALID_STATE 显示服务尚未初始化。
   * @retval ESP_ERR_TIMEOUT 命令未能在规定时间内进入消息队列。
   */
  esp_err_t App_DisplayAIP1620_SetColon(bool enabled);

  /**
   * @brief 一次性设置全部四个图标的显示状态。
   *
   * `icon_mask` 中置位的图标点亮，未置位的图标熄灭；有效范围之外的高位
   * 会被忽略。传入 `APP_DISPLAY_AIP1620_ICON_NONE` 可熄灭全部图标。该操作
   * 保留当前数字、冒号、亮度和显示开关状态。
   *
   * @param[in] icon_mask 由 @ref app_display_aip1620_icon_t
   * 组合而成的图标掩码。
   *
   * @retval ESP_OK 设置命令已成功进入消息队列。
   * @retval ESP_ERR_INVALID_STATE 显示服务尚未初始化。
   * @retval ESP_ERR_TIMEOUT 命令未能在规定时间内进入消息队列。
   */
  esp_err_t App_DisplayAIP1620_SetIcons(uint8_t icon_mask);

  /**
   * @brief 开启或关闭一个或多个指定图标。
   *
   * 只修改 `icon_mask` 选中的图标，其他图标保持不变。可以通过按位或组合
   * 多个图标。该操作保留当前数字、冒号、亮度和显示开关状态。
   *
   * @param[in] icon_mask 要修改的图标掩码，必须至少包含一个有效图标。
   * @param[in] enabled `true` 点亮选中图标，`false` 熄灭选中图标。
   *
   * @retval ESP_OK 设置命令已成功进入消息队列。
   * @retval ESP_ERR_INVALID_ARG 掩码为空或包含无效图标位。
   * @retval ESP_ERR_INVALID_STATE 显示服务尚未初始化。
   * @retval ESP_ERR_TIMEOUT 命令未能在规定时间内进入消息队列。
   */
  esp_err_t App_DisplayAIP1620_SetIcon(uint8_t icon_mask, bool enabled);

  /**
   * @brief 启动或更新指定显示内容的闪烁策略。
   *
   * `content_mask` 中选中的数字位、冒号或图标会按指定间隔在显示和隐藏
   * 之间切换。`interval_ms` 是每次亮灭状态切换的间隔，因此一个完整闪烁
   * 周期约为其两倍。闪烁期间仍可正常更新显示内容，新内容会继续采用当前
   * 闪烁策略。
   *
   * 再次调用本函数会使用新的掩码和间隔替换原有闪烁策略。
   *
   * @param[in] content_mask 由 @ref app_display_aip1620_blink_t 组合而成的
   *                         闪烁内容掩码，不能为 `BLINK_NONE`。
   * @param[in] interval_ms 每次亮灭状态切换的时间间隔，单位为毫秒，不能小于
   *                        @ref APP_DISPLAY_AIP1620_BLINK_MIN_INTERVAL_MS。
   *
   * @retval ESP_OK 闪烁命令已成功进入消息队列。
   * @retval ESP_ERR_INVALID_ARG 掩码为空、包含无效位或切换间隔过小。
   * @retval ESP_ERR_INVALID_STATE 显示服务尚未初始化。
   * @retval ESP_ERR_TIMEOUT 命令未能在规定时间内进入消息队列。
   */
  esp_err_t App_DisplayAIP1620_StartBlink(uint16_t content_mask,
                                          uint32_t interval_ms);

  /**
   * @brief 停止闪烁并恢复完整显示内容。
   *
   * 清除当前闪烁策略，并重新显示闪烁期间保存和更新的完整逻辑显示帧。
   * 命令由显示任务异步执行。
   *
   * @retval ESP_OK 停止命令已成功进入消息队列。
   * @retval ESP_ERR_INVALID_STATE 显示服务尚未初始化。
   * @retval ESP_ERR_TIMEOUT 命令未能在规定时间内进入消息队列。
   */
  esp_err_t App_DisplayAIP1620_StopBlink(void);

#ifdef __cplusplus
}
#endif

#endif /* PROJECT_NAME_APP_DISPLAY_AIP1620_H */
