/**
 * @file Inf_RTC.h
 * @brief 云端校时和本地时间接口。
 *
 * 云端须同时下发 UTC Unix 毫秒时间戳和 POSIX TZ 字符串。设备不使用 NTP，
 * 未成功校时前，时间读取接口返回 ESP_ERR_INVALID_STATE。
 */

#ifndef PROJECT_NAME_INF_RTC_H
#define PROJECT_NAME_INF_RTC_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** POSIX TZ 字符串最大长度，不包含结尾的 '\0'。 */
#define INF_RTC_TIMEZONE_MAX_LENGTH 96U

  /**
   * @brief 初始化时间模块，默认使用 UTC0，并进入等待云端校时状态。
   *
   * 重复调用不会修改已经设置的时间和时区。
   */
  esp_err_t Inf_RTC_Init(void);

  /**
   * @brief 应用云端下发的 UTC 时间和 POSIX 时区。
   *
   * @param[in] utc_time_ms UTC Unix 时间戳，单位毫秒。
   * @param[in] timezone POSIX TZ 字符串，例如 `CST-8`；有夏令时的地区应由
   *                     云端下发包含夏令时规则的完整字符串。
   */
  esp_err_t Inf_RTC_SyncFromCloud(int64_t utc_time_ms, const char *timezone);

  /** @brief 判断设备是否已经成功完成云端校时。 */
  bool Inf_RTC_IsTimeValid(void);

  /**
   * @brief 获取当前 UTC Unix 时间戳，单位秒。
   * @retval ESP_ERR_INVALID_STATE 尚未初始化或尚未完成云端校时。
   */
  esp_err_t Inf_RTC_GetUtcTimestamp(int64_t *utc_time_s);

  /**
   * @brief 获取当前时区下的本地日历时间。
   * @retval ESP_ERR_INVALID_STATE 尚未初始化或尚未完成云端校时。
   */
  esp_err_t Inf_RTC_GetLocalTime(struct tm *local_time);

  /**
   * @brief 将 UTC Unix 秒时间戳转换为当前时区的本地日历时间。
   * @retval ESP_ERR_INVALID_STATE 尚未初始化或尚未完成云端校时。
   */
  esp_err_t Inf_RTC_ConvertUtcToLocal(int64_t utc_time_s,
                                      struct tm *local_time);

#ifdef __cplusplus
}
#endif

#endif /* PROJECT_NAME_INF_RTC_H */
