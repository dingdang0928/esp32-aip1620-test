#include "App_ClockDisplay.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CLOCK_DISPLAY_TASK_STACK_SIZE 3072U
#define CLOCK_DISPLAY_TASK_PRIORITY 4U
#define CLOCK_DISPLAY_REFRESH_MS 500U

typedef struct
{
  bool enabled;
  bool low_battery;
  uint8_t icon_mask;
  inf_aip1620_brightness_t brightness;
} app_clock_display_state_t;

static app_clock_display_state_t s_state = {
    .enabled = true,
    .low_battery = false,
    .icon_mask = INF_AIP1620_ICON_NONE,
    .brightness = INF_AIP1620_BRIGHTNESS_3,
};
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_display_task;

static app_clock_display_state_t App_ClockDisplay_GetState(void)
{
  app_clock_display_state_t state;

  portENTER_CRITICAL(&s_state_lock);
  state = s_state;
  portEXIT_CRITICAL(&s_state_lock);

  return state;
}

static void App_ClockDisplay_Task(void *argument)
{
  (void)argument;

  bool blink_on = true;
  bool previous_enabled = false;
  inf_aip1620_brightness_t previous_brightness =
      INF_AIP1620_BRIGHTNESS_COUNT;
  TickType_t last_wake = xTaskGetTickCount();

  while (true)
  {
    app_clock_display_state_t state = App_ClockDisplay_GetState();

    if (state.enabled != previous_enabled)
    {
      if (state.enabled)
      {
        Inf_AIP_1620_Display_Enable();
      }
      else
      {
        Inf_AIP_1620_Display_Disable();
      }
      previous_enabled = state.enabled;
    }

    if (state.brightness != previous_brightness)
    {
      (void)Inf_AIP_1620_Set_Brightness_Level(state.brightness);
      previous_brightness = state.brightness;
    }

    if (state.enabled)
    {
      inf_rtc_time_t time = {0};
      if (Inf_RTC_IsTimeValid() && (Inf_RTC_GetTime(&time) == ESP_OK))
      {
        (void)Inf_AIP_1620_Display_Time(time.hour, time.minute, true);
      }
      else
      {
        (void)Inf_AIP_1620_Display_Time(0U, 0U, true);
      }

      uint8_t icons = state.icon_mask & INF_AIP1620_ICON_ALL;
      if (state.low_battery && blink_on)
      {
        icons |= INF_AIP1620_ICON_4;
      }
      Inf_AIP_1620_Display_Icons(icons);
    }

    blink_on = !blink_on;
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(CLOCK_DISPLAY_REFRESH_MS));
  }
}

esp_err_t App_ClockDisplay_Init(void)
{
  if (s_display_task != NULL)
  {
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t ret = Inf_RTC_Init();
  if (ret != ESP_OK)
  {
    return ret;
  }

  ret = Inf_AIP_1620_Init();
  if (ret != ESP_OK)
  {
    return ret;
  }

  ret = Inf_AIP_1620_Set_Brightness_Level(s_state.brightness);
  if (ret != ESP_OK)
  {
    (void)Inf_AIP_1620_Deinit();
    return ret;
  }

  BaseType_t result =
      xTaskCreate(App_ClockDisplay_Task, "clock_display",
                  CLOCK_DISPLAY_TASK_STACK_SIZE, NULL,
                  CLOCK_DISPLAY_TASK_PRIORITY, &s_display_task);
  if (result != pdPASS)
  {
    s_display_task = NULL;
    (void)Inf_AIP_1620_Deinit();
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}

esp_err_t App_ClockDisplay_SetTime(const inf_rtc_time_t *time)
{
  return Inf_RTC_SetTime(time);
}

void App_ClockDisplay_SetEnabled(bool enabled)
{
  portENTER_CRITICAL(&s_state_lock);
  s_state.enabled = enabled;
  portEXIT_CRITICAL(&s_state_lock);
}

esp_err_t App_ClockDisplay_SetBrightness(
    inf_aip1620_brightness_t brightness)
{
  if ((uint8_t)brightness >= INF_AIP1620_BRIGHTNESS_COUNT)
  {
    return ESP_ERR_INVALID_ARG;
  }

  portENTER_CRITICAL(&s_state_lock);
  s_state.brightness = brightness;
  portEXIT_CRITICAL(&s_state_lock);
  return ESP_OK;
}

void App_ClockDisplay_SetIcons(uint8_t icon_mask)
{
  portENTER_CRITICAL(&s_state_lock);
  s_state.icon_mask = icon_mask & INF_AIP1620_ICON_ALL;
  portEXIT_CRITICAL(&s_state_lock);
}

void App_ClockDisplay_SetLowBattery(bool low_battery)
{
  portENTER_CRITICAL(&s_state_lock);
  s_state.low_battery = low_battery;
  portEXIT_CRITICAL(&s_state_lock);
}
