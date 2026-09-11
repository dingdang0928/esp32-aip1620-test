#include "inf_rgb.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define RGB_GPIO 38
#define RGB_BREATH_PERIOD_MS 2000
#define RGB_UPDATE_PERIOD_MS 20
#define RGB_MAX_BRIGHTNESS 100
#define RGB_TASK_STACK_SIZE 2048
#define RGB_TASK_PRIORITY 5
#define RGB_TASK_STOP_NOTIFICATION (1UL << 0)
#define PI_F 3.14159265358979323846f

typedef struct
{
  uint8_t red;
  uint8_t green;
  uint8_t blue;
} rgb_color_t;

static const rgb_color_t s_colors[] = {
    {.red = 1, .green = 1, .blue = 0},
    {.red = 0, .green = 1, .blue = 1},
    {.red = 1, .green = 0, .blue = 1},
};

static led_strip_handle_t s_led_strip = NULL;
static TaskHandle_t s_rgb_task_handle = NULL;
static TaskHandle_t s_rgb_deinit_waiter = NULL;

static void rgb_set_frame(led_strip_handle_t led_strip,
                          uint32_t color_index,
                          uint32_t step)
{
  const uint32_t steps = RGB_BREATH_PERIOD_MS / RGB_UPDATE_PERIOD_MS;
  const float progress = (float)step / (float)steps;
  const uint8_t brightness = (uint8_t)(
      (1.0f - cosf(2.0f * PI_F * progress)) *
          (RGB_MAX_BRIGHTNESS / 2.0f) +
      0.5f);
  const rgb_color_t *color = &s_colors[color_index];

  ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0,
                                      color->red * brightness,
                                      color->green * brightness,
                                      color->blue * brightness));
  ESP_ERROR_CHECK(led_strip_refresh(led_strip));
}

static void rgb_breathing_task(void *arg)
{
  led_strip_handle_t led_strip = (led_strip_handle_t)arg;
  const uint32_t steps = RGB_BREATH_PERIOD_MS / RGB_UPDATE_PERIOD_MS;
  uint32_t color_index = 0;
  uint32_t step = 0;

  while (1)
  {
    rgb_set_frame(led_strip, color_index, step);

    step++;
    if (step >= steps)
    {
      step = 0;
      color_index = (color_index + 1) %
                    (sizeof(s_colors) / sizeof(s_colors[0]));
    }

    if (xTaskNotifyWait(0,
                        RGB_TASK_STOP_NOTIFICATION,
                        NULL,
                        pdMS_TO_TICKS(RGB_UPDATE_PERIOD_MS)) == pdTRUE)
    {
      break;
    }
  }

  TaskHandle_t waiter = s_rgb_deinit_waiter;
  s_rgb_task_handle = NULL;
  s_rgb_deinit_waiter = NULL;

  if (waiter != NULL)
  {
    xTaskNotifyGive(waiter);
  }

  vTaskDelete(NULL);
}

esp_err_t inf_rgb_init(void)
{
  const led_strip_config_t strip_config = {
      .strip_gpio_num = RGB_GPIO,
      .max_leds = 1,
      .led_model = LED_MODEL_WS2812,
      .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
      .flags.invert_out = false,
  };
  const led_strip_rmt_config_t rmt_config = {
      .resolution_hz = 10 * 1000 * 1000,
  };

  if (s_led_strip != NULL || s_rgb_task_handle != NULL)
  {
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t err = led_strip_new_rmt_device(&strip_config,
                                           &rmt_config,
                                           &s_led_strip);
  if (err != ESP_OK)
  {
    s_led_strip = NULL;
    return err;
  }

  if (xTaskCreate(rgb_breathing_task,
                  "rgb_breath",
                  RGB_TASK_STACK_SIZE,
                  (void *)s_led_strip,
                  RGB_TASK_PRIORITY,
                  &s_rgb_task_handle) != pdPASS)
  {
    led_strip_del(s_led_strip);
    s_led_strip = NULL;
    s_rgb_task_handle = NULL;
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}

esp_err_t inf_rgb_deinit(void)
{
  TaskHandle_t task = s_rgb_task_handle;
  led_strip_handle_t led_strip = s_led_strip;
  esp_err_t err = ESP_OK;

  if (task == NULL && led_strip == NULL)
  {
    return ESP_OK;
  }

  if (task != NULL)
  {
    if (task == xTaskGetCurrentTaskHandle())
    {
      return ESP_ERR_INVALID_STATE;
    }

    /* Clear a possible stale notification before waiting for task exit. */
    (void)ulTaskNotifyTake(pdTRUE, 0);
    s_rgb_deinit_waiter = xTaskGetCurrentTaskHandle();
    (void)xTaskNotify(task,
                      RGB_TASK_STOP_NOTIFICATION,
                      eSetBits);
    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  }

  led_strip = s_led_strip;
  if (led_strip == NULL)
  {
    return ESP_OK;
  }

  err = led_strip_set_pixel(led_strip, 0, 0, 0, 0);
  if (err == ESP_OK)
  {
    err = led_strip_refresh(led_strip);
  }

  esp_err_t del_err = led_strip_del(led_strip);
  s_led_strip = NULL;

  if (err != ESP_OK)
  {
    return err;
  }

  return del_err;
}
