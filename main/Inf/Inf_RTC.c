#include "Inf_RTC.h"

#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

#include "esp_log.h"

static const char *TAG = "Inf_RTC";

/* 认为 2026 年之后的时间才是有效时间 */
#define INF_RTC_VALID_YEAR_MIN 2026

/**
 * @brief 判断是否为闰年
 */
static bool Inf_RTC_IsLeapYear(uint16_t year)
{
  if ((year % 400U) == 0U)
  {
    return true;
  }

  if ((year % 100U) == 0U)
  {
    return false;
  }

  return ((year % 4U) == 0U);
}

/**
 * @brief 获取指定月份的天数
 */
static uint8_t Inf_RTC_GetDaysInMonth(uint16_t year, uint8_t month)
{
  static const uint8_t days_table[12] = {31, 28, 31, 30, 31, 30,
                                         31, 31, 30, 31, 30, 31};

  if ((month < 1U) || (month > 12U))
  {
    return 0U;
  }

  if ((month == 2U) && Inf_RTC_IsLeapYear(year))
  {
    return 29U;
  }

  return days_table[month - 1U];
}

/**
 * @brief 检查时间参数是否合法
 */
static bool Inf_RTC_CheckTime(const inf_rtc_time_t *rtc_time)
{
  if (rtc_time == NULL)
  {
    return false;
  }

  if ((rtc_time->year < 1970U) || (rtc_time->year > 2099U))
  {
    return false;
  }

  if ((rtc_time->month < 1U) || (rtc_time->month > 12U))
  {
    return false;
  }

  uint8_t days = Inf_RTC_GetDaysInMonth(rtc_time->year, rtc_time->month);

  if ((rtc_time->day < 1U) || (rtc_time->day > days))
  {
    return false;
  }

  if (rtc_time->hour > 23U)
  {
    return false;
  }

  if (rtc_time->minute > 59U)
  {
    return false;
  }

  if (rtc_time->second > 59U)
  {
    return false;
  }

  return true;
}

esp_err_t Inf_RTC_Init(void)
{
  /*
   * 设置中国标准时间。
   *
   * POSIX TZ 格式里面：
   * CST-8 表示 UTC + 8。
   */
  if (setenv("TZ", "CST-8", 1) != 0)
  {
    ESP_LOGE(TAG, "设置时区失败");
    return ESP_FAIL;
  }

  tzset();

  ESP_LOGI(TAG, "RTC 初始化完成，时区 UTC+8");

  return ESP_OK;
}

esp_err_t Inf_RTC_SetTime(const inf_rtc_time_t *rtc_time)
{
  if (!Inf_RTC_CheckTime(rtc_time))
  {
    ESP_LOGE(TAG, "RTC 时间参数非法");
    return ESP_ERR_INVALID_ARG;
  }

  struct tm time_info = {0};

  /*
   * struct tm:
   *
   * tm_year 从 1900 开始
   * tm_mon  从 0 开始
   */
  time_info.tm_year = (int)rtc_time->year - 1900;
  time_info.tm_mon = (int)rtc_time->month - 1;
  time_info.tm_mday = rtc_time->day;

  time_info.tm_hour = rtc_time->hour;
  time_info.tm_min = rtc_time->minute;
  time_info.tm_sec = rtc_time->second;

  /*
   * tm_isdst = -1
   * 让 C 库自己判断夏令时。
   *
   * 中国当前没有夏令时，但这样写更加通用。
   */
  time_info.tm_isdst = -1;

  time_t timestamp = mktime(&time_info);

  if (timestamp == (time_t)-1)
  {
    ESP_LOGE(TAG, "mktime 转换失败");
    return ESP_FAIL;
  }

  struct timeval tv = {.tv_sec = timestamp, .tv_usec = 0};

  if (settimeofday(&tv, NULL) != 0)
  {
    ESP_LOGE(TAG, "settimeofday 设置失败");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "RTC 设置成功：%04u-%02u-%02u %02u:%02u:%02u", rtc_time->year,
           rtc_time->month, rtc_time->day, rtc_time->hour, rtc_time->minute,
           rtc_time->second);
  
  return ESP_OK;
}

esp_err_t Inf_RTC_GetTime(inf_rtc_time_t *rtc_time)
{
  if (rtc_time == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  time_t now = time(NULL);

  if (now == (time_t)-1)
  {
    ESP_LOGE(TAG, "获取系统时间失败");
    return ESP_FAIL;
  }

  struct tm time_info = {0};

  if (localtime_r(&now, &time_info) == NULL)
  {
    ESP_LOGE(TAG, "localtime_r 转换失败");
    return ESP_FAIL;
  }

  rtc_time->year = (uint16_t)(time_info.tm_year + 1900);
  rtc_time->month = (uint8_t)(time_info.tm_mon + 1);
  rtc_time->day = (uint8_t)time_info.tm_mday;

  rtc_time->hour = (uint8_t)time_info.tm_hour;
  rtc_time->minute = (uint8_t)time_info.tm_min;
  rtc_time->second = (uint8_t)time_info.tm_sec;

  rtc_time->weekday = (uint8_t)time_info.tm_wday;

  return ESP_OK;
}

int64_t Inf_RTC_GetTimestamp(void)
{
  return (int64_t)time(NULL);
}

bool Inf_RTC_IsTimeValid(void)
{
  inf_rtc_time_t rtc_time = {0};

  if (Inf_RTC_GetTime(&rtc_time) != ESP_OK)
  {
    return false;
  }

  return (rtc_time.year >= INF_RTC_VALID_YEAR_MIN);
}

void Inf_RTC_PrintTime(void)
{
  inf_rtc_time_t rtc_time = {0};

  if (Inf_RTC_GetTime(&rtc_time) != ESP_OK)
  {
    ESP_LOGE(TAG, "读取 RTC 时间失败");
    return;
  }

  static const char *weekday_string[7] = {
      "星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"};

  ESP_LOGI(TAG, "%04u-%02u-%02u %02u:%02u:%02u %s", rtc_time.year,
           rtc_time.month, rtc_time.day, rtc_time.hour, rtc_time.minute,
           rtc_time.second, weekday_string[rtc_time.weekday]);
}
