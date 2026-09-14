#include "APP_DisplayAIP1620.h"

#include <stddef.h>

#include "Common/Com_Debug.h"
#include "Inf_AIP1620.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define DISPLAY_TASK_STACK_SIZE 3072U
#define DISPLAY_TASK_PRIORITY 4U
#define DISPLAY_QUEUE_LENGTH 16U
#define DISPLAY_QUEUE_SEND_TIMEOUT_MS 50U

typedef enum
{
  DISPLAY_COMMAND_CLEAR = 0,
  DISPLAY_COMMAND_SET_ENABLED,
  DISPLAY_COMMAND_SET_BRIGHTNESS,
  DISPLAY_COMMAND_SHOW_TIME,
  DISPLAY_COMMAND_SHOW_NUMBER,
  DISPLAY_COMMAND_SET_COLON,
  DISPLAY_COMMAND_SET_ICONS,
  DISPLAY_COMMAND_SET_ICON,
  DISPLAY_COMMAND_START_BLINK,
  DISPLAY_COMMAND_STOP_BLINK,
  DISPLAY_COMMAND_SHUTDOWN,
} display_command_id_t;

typedef struct
{
  display_command_id_t id;
  union
  {
    bool enabled;
    app_display_aip1620_brightness_t brightness;
    struct
    {
      uint8_t hour;
      uint8_t minute;
      bool colon_enabled;
    } time;
    struct
    {
      uint16_t number;
      bool leading_zero;
    } number;
    uint8_t icon_mask;
    struct
    {
      uint8_t icon_mask;
      bool enabled;
    } icon;
    struct
    {
      uint16_t content_mask;
      uint32_t interval_ms;
    } blink;
    QueueHandle_t response_queue;
  } data;
} display_command_t;

typedef struct
{
  inf_aip1620_frame_t frame;
  uint16_t blink_mask;
  TickType_t blink_interval;
  TickType_t next_blink_tick;
  bool blink_visible;
} display_context_t;

static QueueHandle_t s_command_queue;
static TaskHandle_t s_display_task;

static esp_err_t App_DisplayAIP1620_Send(const display_command_t *command)
{
  QueueHandle_t queue = s_command_queue;
  if ((command == NULL) || (queue == NULL))
  {
    return ESP_ERR_INVALID_STATE;
  }

  return (xQueueSend(queue, command,
                     pdMS_TO_TICKS(DISPLAY_QUEUE_SEND_TIMEOUT_MS)) == pdPASS)
             ? ESP_OK
             : ESP_ERR_TIMEOUT;
}

static void App_DisplayAIP1620_Render(const display_context_t *context)
{
  inf_aip1620_frame_t rendered = context->frame;

  if ((context->blink_mask != APP_DISPLAY_AIP1620_BLINK_NONE) &&
      !context->blink_visible)
  {
    for (uint8_t index = 0U; index < INF_AIP1620_DIGIT_COUNT; ++index)
    {
      if ((context->blink_mask & (1U << index)) != 0U)
      {
        rendered.digits[index] = INF_AIP1620_DIGIT_BLANK;
      }
    }
    if ((context->blink_mask & APP_DISPLAY_AIP1620_BLINK_COLON) != 0U)
    {
      rendered.colon_enabled = false;
    }
    rendered.icon_mask &=
        (uint8_t)~((context->blink_mask >> 5U) & APP_DISPLAY_AIP1620_ICON_ALL);
  }

  esp_err_t ret = Inf_AIP1620_WriteFrame(&rendered);
  if (ret != ESP_OK)
  {
    MY_LOGE("AiP1620 刷新失败：%s", esp_err_to_name(ret));
  }
}

static void App_DisplayAIP1620_SetNumber(inf_aip1620_frame_t *frame,
                                         uint16_t number, bool leading_zero)
{
  if (number > 9999U)
  {
    number = 9999U;
  }

  for (int index = (int)INF_AIP1620_DIGIT_COUNT - 1; index >= 0; --index)
  {
    uint8_t digit = (uint8_t)(number % 10U);
    bool visible = leading_zero || (number > 0U) ||
                   (index == (int)INF_AIP1620_DIGIT_COUNT - 1);
    frame->digits[index] = visible ? digit : INF_AIP1620_DIGIT_BLANK;
    number /= 10U;
  }
}

static bool App_DisplayAIP1620_ApplyCommand(display_context_t *context,
                                            const display_command_t *command)
{
  bool render_required = false;

  switch (command->id)
  {
    case DISPLAY_COMMAND_CLEAR:
      for (uint8_t index = 0U; index < INF_AIP1620_DIGIT_COUNT; ++index)
      {
        context->frame.digits[index] = INF_AIP1620_DIGIT_BLANK;
      }
      context->frame.colon_enabled = false;
      context->frame.icon_mask = APP_DISPLAY_AIP1620_ICON_NONE;
      render_required = true;
      break;

    case DISPLAY_COMMAND_SET_ENABLED:
      (void)Inf_AIP1620_SetEnabled(command->data.enabled);
      break;

    case DISPLAY_COMMAND_SET_BRIGHTNESS:
      (void)Inf_AIP1620_SetBrightness(
          (inf_aip1620_brightness_t)command->data.brightness);
      break;

    case DISPLAY_COMMAND_SHOW_TIME:
      context->frame.digits[0] = command->data.time.hour / 10U;
      context->frame.digits[1] = command->data.time.hour % 10U;
      context->frame.digits[2] = command->data.time.minute / 10U;
      context->frame.digits[3] = command->data.time.minute % 10U;
      context->frame.colon_enabled = command->data.time.colon_enabled;
      render_required = true;
      break;

    case DISPLAY_COMMAND_SHOW_NUMBER:
      App_DisplayAIP1620_SetNumber(&context->frame, command->data.number.number,
                                   command->data.number.leading_zero);
      render_required = true;
      break;

    case DISPLAY_COMMAND_SET_COLON:
      context->frame.colon_enabled = command->data.enabled;
      render_required = true;
      break;

    case DISPLAY_COMMAND_SET_ICONS:
      context->frame.icon_mask =
          command->data.icon_mask & APP_DISPLAY_AIP1620_ICON_ALL;
      render_required = true;
      break;

    case DISPLAY_COMMAND_SET_ICON:
      if (command->data.icon.enabled)
      {
        context->frame.icon_mask |= command->data.icon.icon_mask;
      }
      else
      {
        context->frame.icon_mask &= (uint8_t)~command->data.icon.icon_mask;
      }
      context->frame.icon_mask &= APP_DISPLAY_AIP1620_ICON_ALL;
      render_required = true;
      break;

    case DISPLAY_COMMAND_START_BLINK:
      context->blink_mask = command->data.blink.content_mask;
      context->blink_interval = pdMS_TO_TICKS(command->data.blink.interval_ms);
      if (context->blink_interval == 0U)
      {
        context->blink_interval = 1U;
      }
      context->blink_visible = true;
      context->next_blink_tick = xTaskGetTickCount() + context->blink_interval;
      render_required = true;
      break;

    case DISPLAY_COMMAND_STOP_BLINK:
      context->blink_mask = APP_DISPLAY_AIP1620_BLINK_NONE;
      context->blink_visible = true;
      render_required = true;
      break;

    case DISPLAY_COMMAND_SHUTDOWN:
      return false;

    default:
      MY_LOGE("收到未知的 AiP1620 消息：%d", (int)command->id);
      break;
  }

  if (render_required)
  {
    App_DisplayAIP1620_Render(context);
  }
  return true;
}

static TickType_t App_DisplayAIP1620_GetWaitTicks(
    const display_context_t *context)
{
  if (context->blink_mask == APP_DISPLAY_AIP1620_BLINK_NONE)
  {
    return portMAX_DELAY;
  }

  TickType_t now = xTaskGetTickCount();
  int32_t remaining = (int32_t)(context->next_blink_tick - now);
  return (remaining > 0) ? (TickType_t)remaining : 0U;
}

static void App_DisplayAIP1620_Task(void *argument)
{
  (void)argument;

  display_context_t context = {
      .frame =
          {
              .digits = {INF_AIP1620_DIGIT_BLANK, INF_AIP1620_DIGIT_BLANK,
                         INF_AIP1620_DIGIT_BLANK, INF_AIP1620_DIGIT_BLANK},
              .colon_enabled = false,
              .icon_mask = APP_DISPLAY_AIP1620_ICON_NONE,
          },
      .blink_mask = APP_DISPLAY_AIP1620_BLINK_NONE,
      .blink_visible = true,
  };

  App_DisplayAIP1620_Render(&context);

  display_command_t command;
  QueueHandle_t shutdown_response_queue = NULL;
  bool running = true;
  while (running)
  {
    TickType_t wait_ticks = App_DisplayAIP1620_GetWaitTicks(&context);
    if (xQueueReceive(s_command_queue, &command, wait_ticks) == pdPASS)
    {
      if (command.id == DISPLAY_COMMAND_SHUTDOWN)
      {
        shutdown_response_queue = command.data.response_queue;
      }
      running = App_DisplayAIP1620_ApplyCommand(&context, &command);
      continue;
    }

    context.blink_visible = !context.blink_visible;
    context.next_blink_tick = xTaskGetTickCount() + context.blink_interval;
    App_DisplayAIP1620_Render(&context);
  }

  esp_err_t shutdown_result = Inf_AIP1620_Deinit();
  s_display_task = NULL;
  if (shutdown_response_queue != NULL)
  {
    (void)xQueueSend(shutdown_response_queue, &shutdown_result, portMAX_DELAY);
  }
  vTaskDelete(NULL);
}

esp_err_t App_DisplayAIP1620_Init(void)
{
  if ((s_command_queue != NULL) || (s_display_task != NULL))
  {
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t ret = Inf_AIP1620_Init();
  if (ret != ESP_OK)
  {
    return ret;
  }

  ret = Inf_AIP1620_SetBrightness(INF_AIP1620_BRIGHTNESS_3);
  if (ret != ESP_OK)
  {
    (void)Inf_AIP1620_Deinit();
    return ret;
  }

  s_command_queue =
      xQueueCreate(DISPLAY_QUEUE_LENGTH, sizeof(display_command_t));
  if (s_command_queue == NULL)
  {
    (void)Inf_AIP1620_Deinit();
    return ESP_ERR_NO_MEM;
  }

  BaseType_t result = xTaskCreate(App_DisplayAIP1620_Task, "aip1620_display",
                                  DISPLAY_TASK_STACK_SIZE, NULL,
                                  DISPLAY_TASK_PRIORITY, &s_display_task);
  if (result != pdPASS)
  {
    vQueueDelete(s_command_queue);
    s_command_queue = NULL;
    (void)Inf_AIP1620_Deinit();
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

esp_err_t App_DisplayAIP1620_Deinit(void)
{
  if ((s_command_queue == NULL) || (s_display_task == NULL))
  {
    return ESP_OK;
  }
  if (s_display_task == xTaskGetCurrentTaskHandle())
  {
    return ESP_ERR_INVALID_STATE;
  }

  QueueHandle_t response_queue = xQueueCreate(1U, sizeof(esp_err_t));
  if (response_queue == NULL)
  {
    return ESP_ERR_NO_MEM;
  }
  display_command_t command = {
      .id = DISPLAY_COMMAND_SHUTDOWN,
      .data.response_queue = response_queue,
  };
  esp_err_t ret = App_DisplayAIP1620_Send(&command);
  if (ret != ESP_OK)
  {
    vQueueDelete(response_queue);
    return ret;
  }

  esp_err_t shutdown_result = ESP_FAIL;
  (void)xQueueReceive(response_queue, &shutdown_result, portMAX_DELAY);
  vQueueDelete(response_queue);
  vQueueDelete(s_command_queue);
  s_command_queue = NULL;
  return shutdown_result;
}

esp_err_t App_DisplayAIP1620_Clear(void)
{
  const display_command_t command = {.id = DISPLAY_COMMAND_CLEAR};
  return App_DisplayAIP1620_Send(&command);
}

esp_err_t App_DisplayAIP1620_SetEnabled(bool enabled)
{
  const display_command_t command = {
      .id = DISPLAY_COMMAND_SET_ENABLED,
      .data.enabled = enabled,
  };
  return App_DisplayAIP1620_Send(&command);
}

esp_err_t App_DisplayAIP1620_SetBrightness(
    app_display_aip1620_brightness_t brightness)
{
  if ((uint8_t)brightness >= APP_DISPLAY_AIP1620_BRIGHTNESS_COUNT)
  {
    return ESP_ERR_INVALID_ARG;
  }
  const display_command_t command = {
      .id = DISPLAY_COMMAND_SET_BRIGHTNESS,
      .data.brightness = brightness,
  };
  return App_DisplayAIP1620_Send(&command);
}

esp_err_t App_DisplayAIP1620_ShowTime(uint8_t hour, uint8_t minute,
                                      bool colon_enabled)
{
  if ((hour > 23U) || (minute > 59U))
  {
    return ESP_ERR_INVALID_ARG;
  }
  const display_command_t command = {
      .id = DISPLAY_COMMAND_SHOW_TIME,
      .data.time =
          {
              .hour = hour,
              .minute = minute,
              .colon_enabled = colon_enabled,
          },
  };
  return App_DisplayAIP1620_Send(&command);
}

esp_err_t App_DisplayAIP1620_ShowNumber(uint16_t number, bool leading_zero)
{
  const display_command_t command = {
      .id = DISPLAY_COMMAND_SHOW_NUMBER,
      .data.number =
          {
              .number = number,
              .leading_zero = leading_zero,
          },
  };
  return App_DisplayAIP1620_Send(&command);
}

esp_err_t App_DisplayAIP1620_SetColon(bool enabled)
{
  const display_command_t command = {
      .id = DISPLAY_COMMAND_SET_COLON,
      .data.enabled = enabled,
  };
  return App_DisplayAIP1620_Send(&command);
}

esp_err_t App_DisplayAIP1620_SetIcons(uint8_t icon_mask)
{
  const display_command_t command = {
      .id = DISPLAY_COMMAND_SET_ICONS,
      .data.icon_mask = icon_mask & APP_DISPLAY_AIP1620_ICON_ALL,
  };
  return App_DisplayAIP1620_Send(&command);
}

esp_err_t App_DisplayAIP1620_SetIcon(uint8_t icon_mask, bool enabled)
{
  if ((icon_mask == APP_DISPLAY_AIP1620_ICON_NONE) ||
      ((icon_mask & (uint8_t)~APP_DISPLAY_AIP1620_ICON_ALL) != 0U))
  {
    return ESP_ERR_INVALID_ARG;
  }
  const display_command_t command = {
      .id = DISPLAY_COMMAND_SET_ICON,
      .data.icon =
          {
              .icon_mask = icon_mask,
              .enabled = enabled,
          },
  };
  return App_DisplayAIP1620_Send(&command);
}

esp_err_t App_DisplayAIP1620_StartBlink(uint16_t content_mask,
                                        uint32_t interval_ms)
{
  if ((content_mask == APP_DISPLAY_AIP1620_BLINK_NONE) ||
      ((content_mask & (uint16_t)~APP_DISPLAY_AIP1620_BLINK_ALL) != 0U) ||
      (interval_ms < APP_DISPLAY_AIP1620_BLINK_MIN_INTERVAL_MS))
  {
    return ESP_ERR_INVALID_ARG;
  }
  const display_command_t command = {
      .id = DISPLAY_COMMAND_START_BLINK,
      .data.blink =
          {
              .content_mask = content_mask,
              .interval_ms = interval_ms,
          },
  };
  return App_DisplayAIP1620_Send(&command);
}

esp_err_t App_DisplayAIP1620_StopBlink(void)
{
  const display_command_t command = {.id = DISPLAY_COMMAND_STOP_BLINK};
  return App_DisplayAIP1620_Send(&command);
}
