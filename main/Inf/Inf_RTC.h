#ifndef __INF_RTC_H__
#define __INF_RTC_H__

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief RTC 时间结构体
 */
typedef struct
{
    uint16_t year;   /**< 年：2020~2099 */
    uint8_t month;   /**< 月：1~12 */
    uint8_t day;     /**< 日：1~31 */

    uint8_t hour;    /**< 时：0~23 */
    uint8_t minute;  /**< 分：0~59 */
    uint8_t second;  /**< 秒：0~59 */

    uint8_t week;    /**< 星期：0=星期日，1=星期一 ... 6=星期六 */
} inf_rtc_time_t;

/**
 * @brief 初始化 RTC
 *
 * @return ESP_OK 成功
 * @return ESP_FAIL 失败
 */
esp_err_t Inf_RTC_Init(void);

/**
 * @brief 设置 RTC 时间
 *
 * @param rtc_time RTC 时间
 *
 * @return ESP_OK 成功
 * @return ESP_ERR_INVALID_ARG 参数错误
 * @return ESP_FAIL 设置失败
 */
esp_err_t Inf_RTC_SetTime(const inf_rtc_time_t *rtc_time);

/**
 * @brief 获取 RTC 时间
 *
 * @param rtc_time 用于保存当前 RTC 时间
 *
 * @return ESP_OK 成功
 * @return ESP_ERR_INVALID_ARG 参数错误
 * @return ESP_FAIL 获取失败
 */
esp_err_t Inf_RTC_GetTime(inf_rtc_time_t *rtc_time);

/**
 * @brief 检查 RTC 时间是否合法
 *
 * @param rtc_time RTC 时间
 *
 * @return true 合法
 * @return false 非法
 */
bool Inf_RTC_IsTimeValid(const inf_rtc_time_t *rtc_time);

#ifdef __cplusplus
}
#endif

#endif /* __INF_RTC_H__ */