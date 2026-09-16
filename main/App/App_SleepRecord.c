#include "App_SleepRecord.h"

#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "Inf_RTC.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define APP_SLEEP_RECORD_PAYLOAD_SIZE 8U
#define APP_SLEEP_RECORD_MONITOR_PERIOD_MS 1000U
#define APP_SLEEP_RECORD_TASK_STACK_SIZE 3072U
#define APP_SLEEP_RECORD_TASK_PRIORITY 4U
#define APP_SLEEP_RECORD_SECONDS_PER_MINUTE UINT32_C(60)
#define APP_SLEEP_RECORD_US_PER_SECOND INT64_C(1000000)

typedef struct
{
  bool bound;
  bool recording;
  bool reminder_12h_sent;
  uint32_t start_timestamp;
  int64_t start_monotonic_us;
} app_sleep_record_context_t;

static const char *TAG = "App_SleepRecord";
static SemaphoreHandle_t s_mutex;
static TaskHandle_t s_monitor_task;
static bool s_storage_initialized;
static bool s_initialized;
static app_sleep_record_context_t s_context;

static void App_SleepRecord_PutU32(uint8_t *destination, uint32_t value)
{
  destination[0] = (uint8_t)value;
  destination[1] = (uint8_t)(value >> 8U);
  destination[2] = (uint8_t)(value >> 16U);
  destination[3] = (uint8_t)(value >> 24U);
}

static uint32_t App_SleepRecord_GetU32(const uint8_t *source)
{
  return ((uint32_t)source[0]) | ((uint32_t)source[1] << 8U) |
         ((uint32_t)source[2] << 16U) | ((uint32_t)source[3] << 24U);
}

static uint32_t App_SleepRecord_RoundDownToMinute(uint32_t timestamp)
{
  return timestamp - (timestamp % APP_SLEEP_RECORD_SECONDS_PER_MINUTE);
}

static uint32_t App_SleepRecord_GetElapsedSecondsLocked(void)
{
  if (!s_context.recording)
  {
    return 0U;
  }

  int64_t elapsed_us = esp_timer_get_time() - s_context.start_monotonic_us;
  if (elapsed_us <= 0)
  {
    return 0U;
  }

  uint64_t elapsed_seconds =
      (uint64_t)elapsed_us / (uint64_t)APP_SLEEP_RECORD_US_PER_SECOND;
  return (elapsed_seconds > UINT32_MAX) ? UINT32_MAX
                                        : (uint32_t)elapsed_seconds;
}

static void App_SleepRecord_ResetRecordingLocked(void)
{
  s_context.recording = false;
  s_context.reminder_12h_sent = false;
  s_context.start_timestamp = 0U;
  s_context.start_monotonic_us = 0;
}

static esp_err_t App_SleepRecord_CheckReady(void)
{
  return (s_initialized && (s_mutex != NULL)) ? ESP_OK
                                               : ESP_ERR_INVALID_STATE;
}

static esp_err_t App_SleepRecord_Decode(
    const inf_sleep_storage_record_info_t *info, const uint8_t *payload,
    size_t payload_length, app_sleep_record_t *record)
{
  if ((info == NULL) || (payload == NULL) || (record == NULL))
  {
    return ESP_ERR_INVALID_ARG;
  }
  if ((info->schema_version != APP_SLEEP_RECORD_SCHEMA_VERSION) ||
      (payload_length != APP_SLEEP_RECORD_PAYLOAD_SIZE))
  {
    return ESP_ERR_INVALID_VERSION;
  }

  record->record_id = info->record_id;
  record->start_timestamp = App_SleepRecord_GetU32(&payload[0]);
  record->end_timestamp = App_SleepRecord_GetU32(&payload[4]);

  if (record->end_timestamp < record->start_timestamp)
  {
    memset(record, 0, sizeof(*record));
    return ESP_ERR_INVALID_RESPONSE;
  }
  return ESP_OK;
}

static void App_SleepRecord_MonitorTask(void *argument)
{
  (void)argument;

  while (true)
  {
    vTaskDelay(pdMS_TO_TICKS(APP_SLEEP_RECORD_MONITOR_PERIOD_MS));

    app_sleep_record_event_t event = APP_SLEEP_RECORD_EVENT_STARTED;
    uint32_t elapsed_minutes = 0U;
    bool notify = false;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_context.recording)
    {
      uint32_t elapsed_seconds = App_SleepRecord_GetElapsedSecondsLocked();
      elapsed_minutes = elapsed_seconds / APP_SLEEP_RECORD_SECONDS_PER_MINUTE;

      if (elapsed_minutes >= APP_SLEEP_RECORD_AUTO_CANCEL_MINUTES)
      {
        App_SleepRecord_ResetRecordingLocked();
        event = APP_SLEEP_RECORD_EVENT_AUTO_CANCELLED_24H;
        notify = true;
      }
      else if (!s_context.reminder_12h_sent &&
               (elapsed_minutes >= APP_SLEEP_RECORD_REMINDER_MINUTES))
      {
        s_context.reminder_12h_sent = true;
        event = APP_SLEEP_RECORD_EVENT_REMINDER_12H;
        notify = true;
      }
    }
    xSemaphoreGive(s_mutex);

    if (notify)
    {
      App_SleepRecord_OnEvent(event, INF_SLEEP_STORAGE_INVALID_RECORD_ID,
                              elapsed_minutes);
    }
  }
}

esp_err_t App_SleepRecord_Init(void)
{
  if (s_initialized || (s_monitor_task != NULL))
  {
    return ESP_ERR_INVALID_STATE;
  }

  if (s_mutex == NULL)
  {
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL)
    {
      return ESP_ERR_NO_MEM;
    }
  }

  esp_err_t err = Inf_RTC_Init();
  if (err != ESP_OK)
  {
    vSemaphoreDelete(s_mutex);
    s_mutex = NULL;
    return err;
  }

  if (!s_storage_initialized)
  {
    err = Inf_SleepStorage_Init();
    if (err != ESP_OK)
    {
      vSemaphoreDelete(s_mutex);
      s_mutex = NULL;
      return err;
    }
    s_storage_initialized = true;
  }

  memset(&s_context, 0, sizeof(s_context));
  BaseType_t task_result =
      xTaskCreate(App_SleepRecord_MonitorTask, "sleep_monitor",
                  APP_SLEEP_RECORD_TASK_STACK_SIZE, NULL,
                  APP_SLEEP_RECORD_TASK_PRIORITY, &s_monitor_task);
  if (task_result != pdPASS)
  {
    vSemaphoreDelete(s_mutex);
    s_mutex = NULL;
    s_monitor_task = NULL;
    return ESP_ERR_NO_MEM;
  }

  s_initialized = true;
  ESP_LOGI(TAG, "sleep record service initialized");
  return ESP_OK;
}

esp_err_t App_SleepRecord_SetBound(bool bound)
{
  esp_err_t err = App_SleepRecord_CheckReady();
  if (err != ESP_OK)
  {
    return err;
  }

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_context.bound = bound;
  xSemaphoreGive(s_mutex);
  ESP_LOGI(TAG, "binding state: %s", bound ? "bound" : "unbound");
  return ESP_OK;
}

esp_err_t App_SleepRecord_Start(void)
{
  esp_err_t err = App_SleepRecord_CheckReady();
  if (err != ESP_OK)
  {
    return err;
  }

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (s_context.recording)
  {
    xSemaphoreGive(s_mutex);
    return ESP_ERR_INVALID_STATE;
  }
  if (!s_context.bound || !Inf_RTC_IsTimeValid())
  {
    xSemaphoreGive(s_mutex);
    return ESP_ERR_INVALID_STATE;
  }

  int64_t timestamp = 0;
  err = Inf_RTC_GetUtcTimestamp(&timestamp);
  if (err != ESP_OK)
  {
    xSemaphoreGive(s_mutex);
    return err;
  }
  if ((timestamp < 0) || ((uint64_t)timestamp > UINT32_MAX))
  {
    xSemaphoreGive(s_mutex);
    return ESP_ERR_INVALID_RESPONSE;
  }

  s_context.start_timestamp = App_SleepRecord_RoundDownToMinute(
      (uint32_t)timestamp);
  s_context.start_monotonic_us = esp_timer_get_time();
  s_context.recording = true;
  s_context.reminder_12h_sent = false;
  xSemaphoreGive(s_mutex);

  App_SleepRecord_OnEvent(APP_SLEEP_RECORD_EVENT_STARTED,
                          INF_SLEEP_STORAGE_INVALID_RECORD_ID, 0U);
  return ESP_OK;
}

esp_err_t App_SleepRecord_Stop(uint32_t *new_record_id)
{
  if (new_record_id != NULL)
  {
    *new_record_id = INF_SLEEP_STORAGE_INVALID_RECORD_ID;
  }

  esp_err_t err = App_SleepRecord_CheckReady();
  if (err != ESP_OK)
  {
    return err;
  }

  app_sleep_record_event_t event;
  uint32_t event_record_id = INF_SLEEP_STORAGE_INVALID_RECORD_ID;
  uint32_t elapsed_minutes;

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (!s_context.recording)
  {
    xSemaphoreGive(s_mutex);
    return ESP_ERR_INVALID_STATE;
  }

  uint32_t elapsed_seconds = App_SleepRecord_GetElapsedSecondsLocked();
  elapsed_minutes = elapsed_seconds / APP_SLEEP_RECORD_SECONDS_PER_MINUTE;

  if (elapsed_minutes >= APP_SLEEP_RECORD_AUTO_CANCEL_MINUTES)
  {
    App_SleepRecord_ResetRecordingLocked();
    xSemaphoreGive(s_mutex);
    App_SleepRecord_OnEvent(APP_SLEEP_RECORD_EVENT_AUTO_CANCELLED_24H,
                            INF_SLEEP_STORAGE_INVALID_RECORD_ID,
                            elapsed_minutes);
    return ESP_ERR_INVALID_STATE;
  }

  if (elapsed_seconds <
      (APP_SLEEP_RECORD_MIN_DURATION_MINUTES *
       APP_SLEEP_RECORD_SECONDS_PER_MINUTE))
  {
    App_SleepRecord_ResetRecordingLocked();
    event = APP_SLEEP_RECORD_EVENT_DISCARDED_TOO_SHORT;
  }
  else
  {
    uint64_t end_timestamp_value =
        (uint64_t)s_context.start_timestamp + (uint64_t)elapsed_seconds;
    if (end_timestamp_value > UINT32_MAX)
    {
      xSemaphoreGive(s_mutex);
      return ESP_ERR_INVALID_RESPONSE;
    }

    uint32_t end_timestamp =
        App_SleepRecord_RoundDownToMinute((uint32_t)end_timestamp_value);
    if (end_timestamp < s_context.start_timestamp)
    {
      xSemaphoreGive(s_mutex);
      return ESP_ERR_INVALID_STATE;
    }

    uint8_t payload[APP_SLEEP_RECORD_PAYLOAD_SIZE];
    App_SleepRecord_PutU32(&payload[0], s_context.start_timestamp);
    App_SleepRecord_PutU32(&payload[4], end_timestamp);

    uint32_t overwritten_record_id = INF_SLEEP_STORAGE_INVALID_RECORD_ID;
    err = Inf_SleepStorage_Append(APP_SLEEP_RECORD_SCHEMA_VERSION, payload,
                                  sizeof(payload), &event_record_id,
                                  &overwritten_record_id);
    if (err != ESP_OK)
    {
      xSemaphoreGive(s_mutex);
      return err;
    }

    if (overwritten_record_id != INF_SLEEP_STORAGE_INVALID_RECORD_ID)
    {
      ESP_LOGW(TAG, "capacity reached; overwritten record id=%" PRIu32,
               overwritten_record_id);
    }
    App_SleepRecord_ResetRecordingLocked();
    event = APP_SLEEP_RECORD_EVENT_SAVED;
    if (new_record_id != NULL)
    {
      *new_record_id = event_record_id;
    }
  }
  xSemaphoreGive(s_mutex);

  App_SleepRecord_OnEvent(event, event_record_id, elapsed_minutes);
  return ESP_OK;
}

esp_err_t App_SleepRecord_Cancel(void)
{
  esp_err_t err = App_SleepRecord_CheckReady();
  if (err != ESP_OK)
  {
    return err;
  }

  uint32_t elapsed_minutes;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (!s_context.recording)
  {
    xSemaphoreGive(s_mutex);
    return ESP_ERR_INVALID_STATE;
  }
  elapsed_minutes = App_SleepRecord_GetElapsedSecondsLocked() /
                    APP_SLEEP_RECORD_SECONDS_PER_MINUTE;
  App_SleepRecord_ResetRecordingLocked();
  xSemaphoreGive(s_mutex);

  App_SleepRecord_OnEvent(APP_SLEEP_RECORD_EVENT_CANCELLED,
                          INF_SLEEP_STORAGE_INVALID_RECORD_ID,
                          elapsed_minutes);
  return ESP_OK;
}

esp_err_t App_SleepRecord_GetStatus(app_sleep_record_status_t *status)
{
  if (status == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }
  esp_err_t err = App_SleepRecord_CheckReady();
  if (err != ESP_OK)
  {
    return err;
  }

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  memset(status, 0, sizeof(*status));
  status->bound = s_context.bound;
  status->time_valid = Inf_RTC_IsTimeValid();
  status->recording = s_context.recording;
  status->reminder_12h_sent = s_context.reminder_12h_sent;
  status->start_timestamp = s_context.start_timestamp;
  status->elapsed_seconds = App_SleepRecord_GetElapsedSecondsLocked();
  err = Inf_SleepStorage_GetCount(&status->stored_count);
  xSemaphoreGive(s_mutex);
  return err;
}

esp_err_t App_SleepRecord_GetCount(uint8_t *count)
{
  if (count == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }
  esp_err_t err = App_SleepRecord_CheckReady();
  if (err != ESP_OK)
  {
    return err;
  }

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  err = Inf_SleepStorage_GetCount(count);
  xSemaphoreGive(s_mutex);
  return err;
}

static esp_err_t App_SleepRecord_ReadLocked(bool by_id, uint32_t value,
                                             app_sleep_record_t *record)
{
  /* 使用 INF 上限接收，便于识别后续 schema，而不是先被小缓冲区挡住。 */
  uint8_t payload[INF_SLEEP_STORAGE_MAX_PAYLOAD_SIZE];
  size_t actual_length = 0U;
  inf_sleep_storage_record_info_t info = {0};
  esp_err_t err;

  if (by_id)
  {
    err = Inf_SleepStorage_ReadById(value, &info, payload, sizeof(payload),
                                    &actual_length);
  }
  else
  {
    if (value > UINT8_MAX)
    {
      return ESP_ERR_INVALID_ARG;
    }
    err = Inf_SleepStorage_ReadByIndex((uint8_t)value, &info, payload,
                                       sizeof(payload), &actual_length);
  }

  return (err == ESP_OK)
             ? App_SleepRecord_Decode(&info, payload, actual_length, record)
             : err;
}

esp_err_t App_SleepRecord_ReadByIndex(uint8_t index,
                                      app_sleep_record_t *record)
{
  if (record == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }
  esp_err_t err = App_SleepRecord_CheckReady();
  if (err != ESP_OK)
  {
    return err;
  }

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  err = App_SleepRecord_ReadLocked(false, index, record);
  xSemaphoreGive(s_mutex);
  return err;
}

esp_err_t App_SleepRecord_ReadById(uint32_t record_id,
                                   app_sleep_record_t *record)
{
  if ((record == NULL) ||
      (record_id == INF_SLEEP_STORAGE_INVALID_RECORD_ID))
  {
    return ESP_ERR_INVALID_ARG;
  }
  esp_err_t err = App_SleepRecord_CheckReady();
  if (err != ESP_OK)
  {
    return err;
  }

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  err = App_SleepRecord_ReadLocked(true, record_id, record);
  xSemaphoreGive(s_mutex);
  return err;
}

esp_err_t App_SleepRecord_ClearAll(void)
{
  esp_err_t err = App_SleepRecord_CheckReady();
  if (err != ESP_OK)
  {
    return err;
  }

  bool was_recording;
  uint32_t elapsed_minutes = 0U;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  was_recording = s_context.recording;
  if (was_recording)
  {
    elapsed_minutes = App_SleepRecord_GetElapsedSecondsLocked() /
                      APP_SLEEP_RECORD_SECONDS_PER_MINUTE;
  }
  err = Inf_SleepStorage_ClearAll();
  if (err == ESP_OK)
  {
    App_SleepRecord_ResetRecordingLocked();
  }
  xSemaphoreGive(s_mutex);

  if ((err == ESP_OK) && was_recording)
  {
    App_SleepRecord_OnEvent(APP_SLEEP_RECORD_EVENT_CANCELLED,
                            INF_SLEEP_STORAGE_INVALID_RECORD_ID,
                            elapsed_minutes);
  }
  return err;
}

void __attribute__((weak)) App_SleepRecord_OnEvent(
    app_sleep_record_event_t event, uint32_t record_id,
    uint32_t elapsed_minutes)
{
  switch (event)
  {
    case APP_SLEEP_RECORD_EVENT_STARTED:
      ESP_LOGI(TAG, "sleep record started");
      break;
    case APP_SLEEP_RECORD_EVENT_SAVED:
      ESP_LOGI(TAG, "sleep record saved: id=%" PRIu32 ", duration=%" PRIu32
                    " min",
               record_id, elapsed_minutes);
      break;
    case APP_SLEEP_RECORD_EVENT_DISCARDED_TOO_SHORT:
      ESP_LOGI(TAG, "sleep record discarded: duration=%" PRIu32 " min",
               elapsed_minutes);
      break;
    case APP_SLEEP_RECORD_EVENT_CANCELLED:
      ESP_LOGI(TAG, "sleep record cancelled: duration=%" PRIu32 " min",
               elapsed_minutes);
      break;
    case APP_SLEEP_RECORD_EVENT_REMINDER_12H:
      ESP_LOGW(TAG, "sleep record reached 12-hour reminder");
      break;
    case APP_SLEEP_RECORD_EVENT_AUTO_CANCELLED_24H:
      ESP_LOGW(TAG, "sleep record automatically cancelled at 24 hours");
      break;
    default:
      break;
  }
}

#if CONFIG_SLEEP_RECORD_UART_TEST
esp_err_t App_SleepRecord_TestSetElapsedMinutes(uint32_t elapsed_minutes)
{
  esp_err_t err = App_SleepRecord_CheckReady();
  if (err != ESP_OK)
  {
    return err;
  }
  if (elapsed_minutes > APP_SLEEP_RECORD_AUTO_CANCEL_MINUTES)
  {
    return ESP_ERR_INVALID_ARG;
  }

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (!s_context.recording)
  {
    xSemaphoreGive(s_mutex);
    return ESP_ERR_INVALID_STATE;
  }

  int64_t delta_us = (int64_t)elapsed_minutes *
                     APP_SLEEP_RECORD_SECONDS_PER_MINUTE *
                     APP_SLEEP_RECORD_US_PER_SECOND;
  s_context.start_monotonic_us = esp_timer_get_time() - delta_us;

  int64_t now = 0;
  err = Inf_RTC_GetUtcTimestamp(&now);
  if (err != ESP_OK)
  {
    xSemaphoreGive(s_mutex);
    return err;
  }
  int64_t test_start = now -
                       ((int64_t)elapsed_minutes *
                        APP_SLEEP_RECORD_SECONDS_PER_MINUTE);
  if ((test_start < 0) || ((uint64_t)test_start > UINT32_MAX))
  {
    xSemaphoreGive(s_mutex);
    return ESP_ERR_INVALID_RESPONSE;
  }
  s_context.start_timestamp =
      App_SleepRecord_RoundDownToMinute((uint32_t)test_start);
  /* 由监控任务产生边界事件，便于串口真实验证 12 h 提醒。 */
  s_context.reminder_12h_sent = false;
  xSemaphoreGive(s_mutex);
  return ESP_OK;
}
#endif
