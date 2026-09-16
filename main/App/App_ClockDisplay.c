#include "App_ClockDisplay.h"

#include "Common/Com_Debug.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define CLOCK_TASK_STACK_SIZE 3072U
#define CLOCK_TASK_PRIORITY 4U
#define CLOCK_REFRESH_MS 1000U
#define CLOCK_LOW_BATTERY_BLINK_MS 500U

typedef struct
{
  QueueHandle_t response_queue;
} clock_command_t;

static QueueHandle_t s_clock_queue;
static TaskHandle_t s_clock_task;

void App_ClockDisplay_Refresh(void)
{
  struct tm local_time = {0};
  if (Inf_RTC_GetLocalTime(&local_time) != ESP_OK)
  {
    (void)App_DisplayAIP1620_ShowTime(0U, 0U, true);
    return;
  }
  esp_err_t ret = App_DisplayAIP1620_ShowTime((uint8_t)local_time.tm_hour,
                                              (uint8_t)local_time.tm_min, true);
  // 通过串口打印进行显示当前时间用于测试,后期可以注释掉这行代码
  MY_LOGI("当前时间 %04d-%02d-%02d %02d:%02d:%02d", local_time.tm_year + 1900,
          local_time.tm_mon + 1, local_time.tm_mday, local_time.tm_hour,
          local_time.tm_min, local_time.tm_sec);
  if (ret != ESP_OK)
  {
    MY_LOGE("时钟显示消息发送失败：%s", esp_err_to_name(ret));
  }
}

static void App_ClockDisplay_Task(void *argument)
{
  (void)argument;

  const TickType_t refresh_ticks = pdMS_TO_TICKS(CLOCK_REFRESH_MS);
  TickType_t next_refresh_tick = xTaskGetTickCount();
  clock_command_t command;
  while (true)
  {
    App_ClockDisplay_Refresh();

    /*
     * 使用固定刷新时间点计算队列等待时间，避免把本轮执行耗时累加到
     * 1 秒周期中。队列仍可在等待期间立即接收退出命令。
     */
    next_refresh_tick += refresh_ticks;
    TickType_t now = xTaskGetTickCount();
    int32_t remaining_ticks = (int32_t)(next_refresh_tick - now);
    TickType_t wait_ticks =
        (remaining_ticks > 0) ? (TickType_t)remaining_ticks : 0U;

    if (xQueueReceive(s_clock_queue, &command, wait_ticks) == pdPASS)
    {
      const esp_err_t result = ESP_OK;
      s_clock_task = NULL;
      if (command.response_queue != NULL)
      {
        (void)xQueueSend(command.response_queue, &result, portMAX_DELAY);
      }
      vTaskDelete(NULL);
    }
  }
}

esp_err_t App_ClockDisplay_Init(void)
{
  if ((s_clock_queue != NULL) || (s_clock_task != NULL))
  {
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t ret = Inf_RTC_Init();
  if (ret != ESP_OK)
  {
    return ret;
  }

  ret = App_DisplayAIP1620_Init();
  if (ret != ESP_OK)
  {
    return ret;
  }

  s_clock_queue = xQueueCreate(1U, sizeof(clock_command_t));
  if (s_clock_queue == NULL)
  {
    (void)App_DisplayAIP1620_Deinit();
    return ESP_ERR_NO_MEM;
  }

  BaseType_t result =
      xTaskCreate(App_ClockDisplay_Task, "clock_display", CLOCK_TASK_STACK_SIZE,
                  NULL, CLOCK_TASK_PRIORITY, &s_clock_task);
  if (result != pdPASS)
  {
    vQueueDelete(s_clock_queue);
    s_clock_queue = NULL;
    (void)App_DisplayAIP1620_Deinit();
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

esp_err_t App_ClockDisplay_Deinit(void)
{
  if ((s_clock_queue == NULL) || (s_clock_task == NULL))
  {
    return App_DisplayAIP1620_Deinit();
  }
  if (s_clock_task == xTaskGetCurrentTaskHandle())
  {
    return ESP_ERR_INVALID_STATE;
  }

  QueueHandle_t response_queue = xQueueCreate(1U, sizeof(esp_err_t));
  if (response_queue == NULL)
  {
    return ESP_ERR_NO_MEM;
  }
  const clock_command_t command = {
      .response_queue = response_queue,
  };
  if (xQueueSend(s_clock_queue, &command, pdMS_TO_TICKS(50U)) != pdPASS)
  {
    vQueueDelete(response_queue);
    return ESP_ERR_TIMEOUT;
  }

  esp_err_t result = ESP_FAIL;
  (void)xQueueReceive(response_queue, &result, portMAX_DELAY);
  vQueueDelete(response_queue);
  vQueueDelete(s_clock_queue);
  s_clock_queue = NULL;
  if (result != ESP_OK)
  {
    return result;
  }
  return App_DisplayAIP1620_Deinit();
}

esp_err_t App_ClockDisplay_SetEnabled(bool enabled)
{
  return App_DisplayAIP1620_SetEnabled(enabled);
}

esp_err_t App_ClockDisplay_SetBrightness(
    app_display_aip1620_brightness_t brightness)
{
  return App_DisplayAIP1620_SetBrightness(brightness);
}

esp_err_t App_ClockDisplay_SetIcons(uint8_t icon_mask)
{
  return App_DisplayAIP1620_SetIcons(icon_mask);
}

esp_err_t App_ClockDisplay_SetLowBattery(bool low_battery)
{
  esp_err_t ret;
  if (low_battery)
  {
    ret = App_DisplayAIP1620_SetIcon(APP_DISPLAY_AIP1620_ICON_4, true);
    if (ret == ESP_OK)
    {
      ret = App_DisplayAIP1620_StartBlink(APP_DISPLAY_AIP1620_BLINK_ICON_4,
                                          CLOCK_LOW_BATTERY_BLINK_MS);
    }
  }
  else
  {
    ret = App_DisplayAIP1620_StopBlink();
    if (ret == ESP_OK)
    {
      ret = App_DisplayAIP1620_SetIcon(APP_DISPLAY_AIP1620_ICON_4, false);
    }
  }
  return ret;
}
