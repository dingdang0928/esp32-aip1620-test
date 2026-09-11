#ifndef INF_RTC_H
#define INF_RTC_H

#include <stdint.h>
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

  /**
   * @brief RTC 时间结构体
   */
  typedef struct
  {
    uint16_t year;   /**< 年，例如 2026 */
    uint8_t month;   /**< 月，1~12 */
    uint8_t day;     /**< 日，1~31 */
    uint8_t hour;    /**< 时，0~23 */
    uint8_t minute;  /**< 分，0~59 */
    uint8_t second;  /**< 秒，0~59 */
    uint8_t weekday; /**< 星期，0=星期日，1=星期一 ... 6=星期六 */
  } inf_rtc_time_t;

  /**
   * @brief 初始化内部 RTC
   *
   * 默认设置为中国标准时间 UTC+8。
   *
   * @return ESP_OK 成功
   */
  esp_err_t Inf_RTC_Init(void);

  /**
   * @brief 设置 RTC 时间
   *
   * @param rtc_time 要设置的时间
   *
   * @return
   *      - ESP_OK 成功
   *      - ESP_ERR_INVALID_ARG 参数错误
   */
  esp_err_t Inf_RTC_SetTime(const inf_rtc_time_t *rtc_time);

  /**
   * @brief 获取当前 RTC 时间
   *
   * @param rtc_time 用于保存当前时间
   *
   * @return
   *      - ESP_OK 成功
   *      - ESP_ERR_INVALID_ARG 参数错误
   */
  esp_err_t Inf_RTC_GetTime(inf_rtc_time_t *rtc_time);

  /**
   * @brief 获取 Unix 时间戳
   *
   * @return 当前 Unix 时间戳，单位秒
   */
  int64_t Inf_RTC_GetTimestamp(void);

  /**
   * @brief 判断当前 RTC 时间是否有效
   *
   * @return true 有效
   * @return false 无效
   */
  bool Inf_RTC_IsTimeValid(void);

  /**
   * @brief 打印当前 RTC 时间
   */
  void Inf_RTC_PrintTime(void);

#ifdef __cplusplus
}
#endif

#endif /* INF_RTC_H */
