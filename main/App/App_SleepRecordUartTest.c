#include "App_SleepRecordUartTest.h"

#include "sdkconfig.h"

#if CONFIG_SLEEP_RECORD_UART_TEST

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "App_SleepRecord.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SLEEP_UART_TASK_STACK_SIZE 5120U
#define SLEEP_UART_TASK_PRIORITY 5U
#define SLEEP_UART_RX_BUFFER_SIZE 512U
#define SLEEP_UART_RETRY_MS 20U
#define SLEEP_UART_LINE_SIZE 96U

static const char *TAG = "SleepUartTest";
static TaskHandle_t s_uart_task;

static esp_err_t App_SleepRecordUartTest_UartInit(void)
{
  const uart_port_t uart_port = (uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM;
  if (!uart_is_driver_installed(uart_port))
  {
    esp_err_t err = uart_driver_install(uart_port, SLEEP_UART_RX_BUFFER_SIZE, 0,
                                        0, NULL, 0);
    if (err != ESP_OK)
    {
      return err;
    }
  }
  uart_vfs_dev_use_driver(uart_port);
  return ESP_OK;
}

static void App_SleepRecordUartTest_PrintHelp(void)
{
  ESP_LOGI(TAG, "================ 睡眠记录串口测试菜单 ================");
  ESP_LOGI(TAG, "help");
  ESP_LOGI(
      TAG,
      "bind 0|1  设置设备绑定状态：0=未绑定，1=已绑定；未绑定时不能开始记录");
  ESP_LOGI(TAG,
           "time YYYY-MM-DD HH:MM:SS   校准设备 RTC，例如：time 2026-09-15 "
           "10:00:00。");
  ESP_LOGI(TAG, "start  开始睡眠记录；执行前必须已经 bind 1 且 RTC 时间有效。");
  ESP_LOGI(TAG,
           "stop  正常结束记录；满 10 分钟才写入 NVS，不足 10 分钟直接丢弃。");
  ESP_LOGI(TAG,
           "cancel  取消当前记录；不会写入 NVS，也不会占用 50 条记录配额。");
  ESP_LOGI(TAG, "status  打印绑定、校时、记录中、已记录时长和 NVS 记录数量。");
  ESP_LOGI(TAG, "list  从最早到最新读取并打印 NVS 中的全部睡眠记录。");
  ESP_LOGI(TAG,
           "read INDEX  按逻辑索引读取一条记录，INDEX=0 表示当前最早记录。");
  ESP_LOGI(TAG,
           "advance MINUTES  测试加速：模拟已经记录指定分钟，不需要真实等待。 "
           "示例：advance 10、advance 720、advance 1440。");
  ESP_LOGI(TAG, "clear yes  清空 NVS 中全部睡眠记录；必须带 yes，防止误操作。");
  ESP_LOGI(TAG, "说明：命令前可选加 sleep 前缀，例如 sleep start。");
  ESP_LOGI(
      TAG,
      "推荐流程：bind 1 -> time ... -> start -> advance 10 -> stop -> list");
  ESP_LOGI(TAG, "========================================================");
}

static char *App_SleepRecordUartTest_Trim(char *text)
{
  while (isspace((unsigned char)*text))
  {
    ++text;
  }

  size_t length = strlen(text);
  while ((length > 0U) && isspace((unsigned char)text[length - 1U]))
  {
    text[--length] = '\0';
  }
  return text;
}

static bool App_SleepRecordUartTest_ReadLine(char *line, size_t capacity)
{
  size_t length = 0U;
  bool overflow = false;

  while (true)
  {
    int ch = getchar();
    if (ch == EOF)
    {
      vTaskDelay(pdMS_TO_TICKS(SLEEP_UART_RETRY_MS));
      continue;
    }
    if ((ch == '\r') || (ch == '\n'))
    {
      if ((length == 0U) && !overflow)
      {
        continue;
      }
      line[length] = '\0';
      return !overflow;
    }
    if (length < (capacity - 1U))
    {
      line[length++] = (char)ch;
    }
    else
    {
      overflow = true;
    }
  }
}

static void App_SleepRecordUartTest_FormatTime(uint32_t timestamp, char *buffer,
                                               size_t buffer_size)
{
  time_t raw_time = (time_t)timestamp;
  struct tm local_time = {0};
  if (localtime_r(&raw_time, &local_time) == NULL)
  {
    snprintf(buffer, buffer_size, "invalid");
    return;
  }
  (void)strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S", &local_time);
}

static esp_err_t App_SleepRecordUartTest_PrintRecord(uint8_t index)
{
  app_sleep_record_t record = {0};
  esp_err_t err = App_SleepRecord_ReadByIndex(index, &record);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "读取失败：index=%u，错误=%s", index, esp_err_to_name(err));
    return err;
  }

  char start_time[24];
  char end_time[24];
  App_SleepRecordUartTest_FormatTime(record.start_timestamp, start_time,
                                     sizeof(start_time));
  App_SleepRecordUartTest_FormatTime(record.end_timestamp, end_time,
                                     sizeof(end_time));
  uint32_t duration_minutes =
      (record.end_timestamp - record.start_timestamp) / 60U;
  ESP_LOGI(TAG,
           "读取成功：index=%u，id=%" PRIu32 "，开始=%s (%" PRIu32
           ")，结束=%s (%" PRIu32 ")，时长=%" PRIu32 " 分钟",
           index, record.record_id, start_time, record.start_timestamp,
           end_time, record.end_timestamp, duration_minutes);
  return ESP_OK;
}

static esp_err_t App_SleepRecordUartTest_PrintStatus(void)
{
  app_sleep_record_status_t status = {0};
  esp_err_t err = App_SleepRecord_GetStatus(&status);
  if (err != ESP_OK)
  {
    return err;
  }

  ESP_LOGI(TAG,
           "状态：已绑定=%u，时间有效=%u，记录中=%u，已发12小时提醒=%u，"
           "已记录=%" PRIu32 " 秒，NVS记录数=%u，开始时间戳=%" PRIu32,
           status.bound, status.time_valid, status.recording,
           status.reminder_12h_sent, status.elapsed_seconds,
           status.stored_count, status.start_timestamp);
  return ESP_OK;
}

static esp_err_t App_SleepRecordUartTest_List(void)
{
  uint8_t count = 0U;
  esp_err_t err = App_SleepRecord_GetCount(&count);
  if (err != ESP_OK)
  {
    return err;
  }

  ESP_LOGI(TAG, "记录列表：共 %u 条（从最早到最新）", count);
  for (uint8_t index = 0U; index < count; ++index)
  {
    err = App_SleepRecordUartTest_PrintRecord(index);
    if (err != ESP_OK)
    {
      return err;
    }
  }
  if (count == 0U)
  {
    ESP_LOGI(TAG, "记录列表为空，NVS 中没有睡眠记录");
  }
  return ESP_OK;
}

static esp_err_t App_SleepRecordUartTest_SetTime(const char *arguments)
{
  unsigned int year;
  unsigned int month;
  unsigned int day;
  unsigned int hour;
  unsigned int minute;
  unsigned int second;
  char tail;

  int fields = sscanf(arguments, "%u-%u-%u %u:%u:%u %c", &year, &month, &day,
                      &hour, &minute, &second, &tail);
  if (fields != 6)
  {
    return ESP_ERR_INVALID_ARG;
  }

  inf_rtc_time_t rtc_time = {
      .year = (uint16_t)year,
      .month = (uint8_t)month,
      .day = (uint8_t)day,
      .hour = (uint8_t)hour,
      .minute = (uint8_t)minute,
      .second = (uint8_t)second,
      .weekday = 0U,
  };
  if ((year > UINT16_MAX) || (month > UINT8_MAX) || (day > UINT8_MAX) ||
      (hour > UINT8_MAX) || (minute > UINT8_MAX) || (second > UINT8_MAX))
  {
    return ESP_ERR_INVALID_ARG;
  }
  return App_SleepRecord_SetTime(&rtc_time);
}

static esp_err_t App_SleepRecordUartTest_ParseU32(const char *arguments,
                                                  uint32_t *value)
{
  if ((arguments == NULL) || (value == NULL) || (*arguments == '\0'))
  {
    return ESP_ERR_INVALID_ARG;
  }

  char *end = NULL;
  unsigned long parsed = strtoul(arguments, &end, 10);
  while ((end != NULL) && isspace((unsigned char)*end))
  {
    ++end;
  }
  if ((end == arguments) || (end == NULL) || (*end != '\0') ||
      (parsed > UINT32_MAX))
  {
    return ESP_ERR_INVALID_ARG;
  }
  *value = (uint32_t)parsed;
  return ESP_OK;
}

static esp_err_t App_SleepRecordUartTest_Execute(char *line)
{
  char *command = App_SleepRecordUartTest_Trim(line);
  if (strncmp(command, "sleep ", 6U) == 0)
  {
    command = App_SleepRecordUartTest_Trim(command + 6U);
  }

  char *arguments = strchr(command, ' ');
  if (arguments != NULL)
  {
    *arguments++ = '\0';
    arguments = App_SleepRecordUartTest_Trim(arguments);
  }
  else
  {
    arguments = command + strlen(command);
  }

  if (strcmp(command, "help") == 0)
  {
    App_SleepRecordUartTest_PrintHelp();
    return ESP_OK;
  }
  if (strcmp(command, "bind") == 0)
  {
    if ((strcmp(arguments, "0") != 0) && (strcmp(arguments, "1") != 0))
    {
      return ESP_ERR_INVALID_ARG;
    }
    return App_SleepRecord_SetBound(arguments[0] == '1');
  }
  if (strcmp(command, "time") == 0)
  {
    return App_SleepRecordUartTest_SetTime(arguments);
  }
  if (strcmp(command, "start") == 0)
  {
    return App_SleepRecord_Start();
  }
  if (strcmp(command, "stop") == 0)
  {
    uint32_t record_id = INF_SLEEP_STORAGE_INVALID_RECORD_ID;
    esp_err_t err = App_SleepRecord_Stop(&record_id);
    if (err == ESP_OK)
    {
      if (record_id == INF_SLEEP_STORAGE_INVALID_RECORD_ID)
      {
        ESP_LOGI(TAG, "结束成功：记录不足 10 分钟，本次没有写入 NVS");
      }
      else
      {
        ESP_LOGI(TAG, "结束成功：记录已写入 NVS，record_id=%" PRIu32,
                 record_id);
      }
    }
    return err;
  }
  if (strcmp(command, "cancel") == 0)
  {
    return App_SleepRecord_Cancel();
  }
  if (strcmp(command, "status") == 0)
  {
    return App_SleepRecordUartTest_PrintStatus();
  }
  if (strcmp(command, "list") == 0)
  {
    return App_SleepRecordUartTest_List();
  }
  if (strcmp(command, "read") == 0)
  {
    uint32_t index;
    esp_err_t err = App_SleepRecordUartTest_ParseU32(arguments, &index);
    if ((err != ESP_OK) || (index > UINT8_MAX))
    {
      return ESP_ERR_INVALID_ARG;
    }
    return App_SleepRecordUartTest_PrintRecord((uint8_t)index);
  }
  if (strcmp(command, "advance") == 0)
  {
    uint32_t minutes;
    esp_err_t err = App_SleepRecordUartTest_ParseU32(arguments, &minutes);
    return (err == ESP_OK) ? App_SleepRecord_TestSetElapsedMinutes(minutes)
                           : err;
  }
  if (strcmp(command, "clear") == 0)
  {
    return (strcmp(arguments, "yes") == 0) ? App_SleepRecord_ClearAll()
                                           : ESP_ERR_INVALID_ARG;
  }
  return ESP_ERR_NOT_SUPPORTED;
}

static void App_SleepRecordUartTest_Task(void *argument)
{
  (void)argument;
  App_SleepRecordUartTest_PrintHelp();

  char line[SLEEP_UART_LINE_SIZE];
  while (true)
  {
    ESP_LOGI(TAG, "sleep> ");
    if (!App_SleepRecordUartTest_ReadLine(line, sizeof(line)))
    {
      ESP_LOGE(TAG, "命令过长，请重新输入");
      continue;
    }

    esp_err_t err = App_SleepRecordUartTest_Execute(line);
    if (err == ESP_OK)
    {
      ESP_LOGI(TAG, "命令执行成功");
    }
    else if (err == ESP_ERR_NOT_SUPPORTED)
    {
      ESP_LOGE(TAG, "未知命令，请输入 help 查看中文菜单");
    }
    else
    {
      ESP_LOGE(TAG, "命令执行失败：%s (0x%x)，请检查前置条件和参数",
               esp_err_to_name(err), err);
    }
  }
}

esp_err_t App_SleepRecordUartTest_Start(void)
{
  if (s_uart_task != NULL)
  {
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t err = App_SleepRecordUartTest_UartInit();
  if (err != ESP_OK)
  {
    return err;
  }

  BaseType_t result = xTaskCreate(App_SleepRecordUartTest_Task,
                                  "sleep_uart_test", SLEEP_UART_TASK_STACK_SIZE,
                                  NULL, SLEEP_UART_TASK_PRIORITY, &s_uart_task);
  return (result == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

#else

esp_err_t App_SleepRecordUartTest_Start(void)
{
  return ESP_ERR_NOT_SUPPORTED;
}

#endif /* CONFIG_SLEEP_RECORD_UART_TEST */
