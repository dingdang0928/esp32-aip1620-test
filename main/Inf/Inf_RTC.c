#include "Inf_RTC.h"

#include <stdlib.h>
#include <sys/time.h>
#include <time.h>


/**
 * @brief 判断是否为闰年
 *
 * @param year 年份
 *
 * @return true 闰年
 * @return false 平年
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

    if ((year % 4U) == 0U)
    {
        return true;
    }

    return false;
}


/**
 * @brief 获取指定月份的天数
 *
 * @param year 年份
 * @param month 月份
 *
 * @return 月份对应的天数
 */
static uint8_t Inf_RTC_GetMonthDays(uint16_t year, uint8_t month)
{
    static const uint8_t month_days[12] =
    {
        31U, 28U, 31U, 30U,
        31U, 30U, 31U, 31U,
        30U, 31U, 30U, 31U
    };

    if ((month < 1U) || (month > 12U))
    {
        return 0U;
    }

    if ((month == 2U) && Inf_RTC_IsLeapYear(year))
    {
        return 29U;
    }

    return month_days[month - 1U];
}


/**
 * @brief 初始化 RTC
 */
esp_err_t Inf_RTC_Init(void)
{
    /*
     * 设置时区为 UTC+8。
     *
     * POSIX TZ 规则中：
     * CST-8 表示 UTC+8。
     */
    if (setenv("TZ", "CST-8", 1) != 0)
    {
        return ESP_FAIL;
    }

    tzset();

    return ESP_OK;
}


/**
 * @brief 检查 RTC 时间是否合法
 */
bool Inf_RTC_IsTimeValid(const inf_rtc_time_t *rtc_time)
{
    uint8_t max_day;

    if (rtc_time == NULL)
    {
        return false;
    }

    /* 检查年份 */
    if ((rtc_time->year < 2020U) ||
        (rtc_time->year > 2099U))
    {
        return false;
    }

    /* 检查月份 */
    if ((rtc_time->month < 1U) ||
        (rtc_time->month > 12U))
    {
        return false;
    }

    /* 获取当前月份最大天数 */
    max_day = Inf_RTC_GetMonthDays(
        rtc_time->year,
        rtc_time->month);

    /* 检查日期 */
    if ((rtc_time->day < 1U) ||
        (rtc_time->day > max_day))
    {
        return false;
    }

    /* 检查小时 */
    if (rtc_time->hour > 23U)
    {
        return false;
    }

    /* 检查分钟 */
    if (rtc_time->minute > 59U)
    {
        return false;
    }

    /* 检查秒 */
    if (rtc_time->second > 59U)
    {
        return false;
    }

    return true;
}


/**
 * @brief 设置 RTC 时间
 */
esp_err_t Inf_RTC_SetTime(const inf_rtc_time_t *rtc_time)
{
    struct tm tm_info = {0};
    struct timeval tv_info = {0};

    time_t timestamp;

    if (rtc_time == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!Inf_RTC_IsTimeValid(rtc_time))
    {
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * struct tm：
     *
     * tm_year 从 1900 年开始计算。
     * tm_mon  范围为 0~11。
     */
    tm_info.tm_year = (int)rtc_time->year - 1900;
    tm_info.tm_mon  = (int)rtc_time->month - 1;
    tm_info.tm_mday = (int)rtc_time->day;

    tm_info.tm_hour = (int)rtc_time->hour;
    tm_info.tm_min  = (int)rtc_time->minute;
    tm_info.tm_sec  = (int)rtc_time->second;

    tm_info.tm_isdst = -1;

    /*
     * 将本地日期时间转换成 Unix 时间戳。
     */
    timestamp = mktime(&tm_info);

    if (timestamp == (time_t)-1)
    {
        return ESP_FAIL;
    }

    tv_info.tv_sec = timestamp;
    tv_info.tv_usec = 0;

    /*
     * 设置 ESP32 系统时间。
     */
    if (settimeofday(&tv_info, NULL) != 0)
    {
        return ESP_FAIL;
    }

    return ESP_OK;
}


/**
 * @brief 获取 RTC 时间
 */
esp_err_t Inf_RTC_GetTime(inf_rtc_time_t *rtc_time)
{
    time_t timestamp;
    struct tm tm_info;

    if (rtc_time == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * 获取当前 Unix 时间戳。
     *
     * 注意：
     * 这里调用的是 <time.h> 中的 time() 函数。
     */
    timestamp = time(NULL);

    if (timestamp == (time_t)-1)
    {
        return ESP_FAIL;
    }

    /*
     * 将 Unix 时间戳转换成本地时间。
     */
    if (localtime_r(&timestamp, &tm_info) == NULL)
    {
        return ESP_FAIL;
    }

    rtc_time->year =
        (uint16_t)(tm_info.tm_year + 1900);

    rtc_time->month =
        (uint8_t)(tm_info.tm_mon + 1);

    rtc_time->day =
        (uint8_t)tm_info.tm_mday;

    rtc_time->hour =
        (uint8_t)tm_info.tm_hour;

    rtc_time->minute =
        (uint8_t)tm_info.tm_min;

    rtc_time->second =
        (uint8_t)tm_info.tm_sec;

    rtc_time->week =
        (uint8_t)tm_info.tm_wday;

    return ESP_OK;
}