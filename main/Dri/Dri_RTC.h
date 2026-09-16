/**
 * @file Dri_RTC.h
 * @brief ESP 系统 UTC 时钟的最小驱动封装。
 *
 * DRI 层只负责读写系统 UTC 时间，不处理时区、云端协议或“是否已校时”状态。
 */

#ifndef PROJECT_NAME_DRI_RTC_H
#define PROJECT_NAME_DRI_RTC_H

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

  /**
   * @brief 设置系统 UTC Unix 时间戳。
   *
   * @param[in] utc_time_ms UTC Unix 时间戳，单位毫秒，必须大于或等于 0。
   *
   * @retval ESP_OK 设置成功。
   * @retval ESP_ERR_INVALID_ARG 时间戳为负数。
   * @retval ESP_ERR_INVALID_SIZE 时间戳超出平台 time_t 可表示范围。
   * @retval ESP_FAIL settimeofday() 调用失败。
   */
  esp_err_t Dri_RTC_SetUtcTimeMs(int64_t utc_time_ms);

  /**
   * @brief 获取当前系统 UTC Unix 时间戳。
   *
   * @param[out] utc_time_ms 接收 UTC Unix 时间戳，单位毫秒。
   *
   * @retval ESP_OK 读取成功。
   * @retval ESP_ERR_INVALID_ARG utc_time_ms 为 NULL。
   * @retval ESP_ERR_INVALID_SIZE 时间无法安全转换为 int64_t 毫秒值。
   * @retval ESP_FAIL gettimeofday() 调用失败或返回无效数据。
   */
  esp_err_t Dri_RTC_GetUtcTimeMs(int64_t *utc_time_ms);

#ifdef __cplusplus
}
#endif

#endif /* PROJECT_NAME_DRI_RTC_H */
