/**
 * @file Dri_RTC.c
 * @brief ESP 系统 UTC 时钟的最小驱动实现。
 */

#include "Dri_RTC.h"

#include <errno.h>
#include <sys/time.h>
#include <time.h>

#include "esp_log.h"

static const char *TAG = "Dri_RTC";

esp_err_t Dri_RTC_SetUtcTimeMs(int64_t utc_time_ms)
{
  if (utc_time_ms < 0)
  {
    return ESP_ERR_INVALID_ARG;
  }

  int64_t seconds_value = utc_time_ms / INT64_C(1000);
  time_t seconds = (time_t)seconds_value;
  if ((int64_t)seconds != seconds_value)
  {
    return ESP_ERR_INVALID_SIZE;
  }

  const struct timeval value = {
      .tv_sec = seconds,
      .tv_usec = (suseconds_t)((utc_time_ms % INT64_C(1000)) * INT64_C(1000)),
  };
  if (settimeofday(&value, NULL) != 0)
  {
    ESP_LOGE(TAG, "设置系统 UTC 时间失败：errno=%d", errno);
    return ESP_FAIL;
  }

  return ESP_OK;
}

esp_err_t Dri_RTC_GetUtcTimeMs(int64_t *utc_time_ms)
{
  if (utc_time_ms == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }
  *utc_time_ms = 0;

  struct timeval value = {0};
  if ((gettimeofday(&value, NULL) != 0) || (value.tv_sec < 0) ||
      (value.tv_usec < 0) || (value.tv_usec >= 1000000))
  {
    return ESP_FAIL;
  }

  if ((int64_t)value.tv_sec > (INT64_MAX / INT64_C(1000)))
  {
    return ESP_ERR_INVALID_SIZE;
  }

  *utc_time_ms = ((int64_t)value.tv_sec * INT64_C(1000)) +
                 ((int64_t)value.tv_usec / INT64_C(1000));
  return ESP_OK;
}
