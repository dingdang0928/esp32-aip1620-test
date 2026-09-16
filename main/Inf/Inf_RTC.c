#include "Inf_RTC.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "Dri_RTC.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "Inf_RTC";

/* 云端时间只接受 2024-01-01（含）至 2100-01-01（不含）。 */
#define INF_RTC_MIN_UTC_TIME_MS INT64_C(1704067200000)
#define INF_RTC_MAX_UTC_TIME_MS INT64_C(4102444800000)

static SemaphoreHandle_t s_mutex;
static StaticSemaphore_t s_mutex_storage;
static bool s_initialized;
static bool s_time_valid;

static bool Inf_RTC_IsTimezoneSafe(const char *timezone)
{
  if (timezone == NULL)
  {
    return false;
  }

  size_t length = strnlen(timezone, INF_RTC_TIMEZONE_MAX_LENGTH + 1U);
  if ((length == 0U) || (length > INF_RTC_TIMEZONE_MAX_LENGTH))
  {
    return false;
  }

  for (size_t index = 0U; index < length; ++index)
  {
    unsigned char character = (unsigned char)timezone[index];
    if (!isprint(character) || isspace(character))
    {
      return false;
    }
  }
  return true;
}

esp_err_t Inf_RTC_Init(void)
{
  if (s_mutex == NULL)
  {
    s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_storage);
    if (s_mutex == NULL)
    {
      return ESP_ERR_NO_MEM;
    }
  }

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (s_initialized)
  {
    xSemaphoreGive(s_mutex);
    return ESP_OK;
  }

  if (setenv("TZ", "UTC0", 1) != 0)
  {
    ESP_LOGE(TAG, "初始化时区失败：errno=%d", errno);
    xSemaphoreGive(s_mutex);
    return ESP_FAIL;
  }
  tzset();

  s_time_valid = false;
  s_initialized = true;
  xSemaphoreGive(s_mutex);

  ESP_LOGI(TAG, "时间模块初始化完成，等待云端校时");
  return ESP_OK;
}

esp_err_t Inf_RTC_SyncFromCloud(int64_t utc_time_ms, const char *timezone)
{
  if ((utc_time_ms < INF_RTC_MIN_UTC_TIME_MS) ||
      (utc_time_ms >= INF_RTC_MAX_UTC_TIME_MS) ||
      !Inf_RTC_IsTimezoneSafe(timezone))
  {
    return ESP_ERR_INVALID_ARG;
  }
  if (s_mutex == NULL)
  {
    return ESP_ERR_INVALID_STATE;
  }

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (!s_initialized)
  {
    xSemaphoreGive(s_mutex);
    return ESP_ERR_INVALID_STATE;
  }

  s_time_valid = false;
  esp_err_t err = Dri_RTC_SetUtcTimeMs(utc_time_ms);
  if (err == ESP_OK)
  {
    if (setenv("TZ", timezone, 1) != 0)
    {
      ESP_LOGE(TAG, "设置时区失败：errno=%d", errno);
      err = ESP_FAIL;
    }
    else
    {
      tzset();
      s_time_valid = true;
    }
  }
  xSemaphoreGive(s_mutex);

  if (err == ESP_OK)
  {
    ESP_LOGI(TAG, "云端校时成功：utc_ms=%" PRId64 "，timezone=%s",
             utc_time_ms, timezone);
  }
  return err;
}

bool Inf_RTC_IsTimeValid(void)
{
  if (s_mutex == NULL)
  {
    return false;
  }

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  bool valid = s_initialized && s_time_valid;
  xSemaphoreGive(s_mutex);
  return valid;
}

esp_err_t Inf_RTC_GetUtcTimestamp(int64_t *utc_time_s)
{
  if (utc_time_s == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }
  *utc_time_s = 0;

  if (s_mutex == NULL)
  {
    return ESP_ERR_INVALID_STATE;
  }

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (!s_initialized || !s_time_valid)
  {
    xSemaphoreGive(s_mutex);
    return ESP_ERR_INVALID_STATE;
  }

  int64_t utc_time_ms = 0;
  esp_err_t err = Dri_RTC_GetUtcTimeMs(&utc_time_ms);
  xSemaphoreGive(s_mutex);

  if (err == ESP_OK)
  {
    *utc_time_s = utc_time_ms / INT64_C(1000);
  }
  return err;
}

esp_err_t Inf_RTC_ConvertUtcToLocal(int64_t utc_time_s,
                                    struct tm *local_time)
{
  if (local_time == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }
  memset(local_time, 0, sizeof(*local_time));

  time_t raw_time = (time_t)utc_time_s;
  if ((int64_t)raw_time != utc_time_s)
  {
    return ESP_ERR_INVALID_SIZE;
  }
  if (s_mutex == NULL)
  {
    return ESP_ERR_INVALID_STATE;
  }

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (!s_initialized || !s_time_valid)
  {
    xSemaphoreGive(s_mutex);
    return ESP_ERR_INVALID_STATE;
  }
  struct tm *result = localtime_r(&raw_time, local_time);
  xSemaphoreGive(s_mutex);

  return (result != NULL) ? ESP_OK : ESP_FAIL;
}

esp_err_t Inf_RTC_GetLocalTime(struct tm *local_time)
{
  if (local_time == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }
  memset(local_time, 0, sizeof(*local_time));

  if (s_mutex == NULL)
  {
    return ESP_ERR_INVALID_STATE;
  }

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (!s_initialized || !s_time_valid)
  {
    xSemaphoreGive(s_mutex);
    return ESP_ERR_INVALID_STATE;
  }

  int64_t utc_time_ms = 0;
  esp_err_t err = Dri_RTC_GetUtcTimeMs(&utc_time_ms);
  if (err == ESP_OK)
  {
    time_t raw_time = (time_t)(utc_time_ms / INT64_C(1000));
    err = (localtime_r(&raw_time, local_time) != NULL) ? ESP_OK : ESP_FAIL;
  }
  xSemaphoreGive(s_mutex);
  return err;
}
