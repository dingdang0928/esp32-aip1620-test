#include "inf_rgb.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define RGB_GPIO 38
#define RGB_COLOR_CYCLE_MS 7000
#define RGB_UPDATE_PERIOD_MS 20
#define RGB_MAX_BRIGHTNESS 48U
#define RGB_TASK_STACK_SIZE 2048
#define RGB_TASK_PRIORITY 5
#define RGB_TASK_STOP_NOTIFICATION (1UL << 0)

typedef struct
{
  uint8_t red;
  uint8_t green;
  uint8_t blue;
} rgb_color_t;

static const rgb_color_t s_colors[] = {
    {.red = 255U, .green = 0U, .blue = 0U},     /* 红 */
    {.red = 255U, .green = 127U, .blue = 0U},   /* 橙 */
    {.red = 255U, .green = 255U, .blue = 0U},   /* 黄 */
    {.red = 0U, .green = 255U, .blue = 0U},     /* 绿 */
    {.red = 0U, .green = 255U, .blue = 255U},   /* 青 */
    {.red = 0U, .green = 0U, .blue = 255U},     /* 蓝 */
    {.red = 255U, .green = 0U, .blue = 255U},   /* 紫 */
};

static led_strip_handle_t s_led_strip = NULL;
static TaskHandle_t s_rgb_task_handle = NULL;
static TaskHandle_t s_rgb_deinit_waiter = NULL;

static uint8_t rgb_interpolate(uint8_t start,
                               uint8_t end,
                               uint32_t step,
                               uint32_t total_steps)
{
  uint32_t value = ((uint32_t)start * (total_steps - step)) +
                   ((uint32_t)end * step);
  return (uint8_t)(value / total_steps);
}

static void rgb_set_frame(led_strip_handle_t led_strip, uint32_t color_step)
{
  const uint32_t color_count = sizeof(s_colors) / sizeof(s_colors[0]);
  const uint32_t steps_per_color =
      (RGB_COLOR_CYCLE_MS / color_count) / RGB_UPDATE_PERIOD_MS;
  const uint32_t color_index = color_step / steps_per_color;
  const uint32_t next_color_index = (color_index + 1U) % color_count;
  const uint32_t transition_step = color_step % steps_per_color;
  const rgb_color_t *current = &s_colors[color_index];
  const rgb_color_t *next = &s_colors[next_color_index];

  uint8_t red = rgb_interpolate(current->red, next->red,
                                transition_step, steps_per_color);
  uint8_t green = rgb_interpolate(current->green, next->green,
                                  transition_step, steps_per_color);
  uint8_t blue = rgb_interpolate(current->blue, next->blue,
                                 transition_step, steps_per_color);

  red = (uint8_t)(((uint16_t)red * RGB_MAX_BRIGHTNESS) / 255U);
  green = (uint8_t)(((uint16_t)green * RGB_MAX_BRIGHTNESS) / 255U);
  blue = (uint8_t)(((uint16_t)blue * RGB_MAX_BRIGHTNESS) / 255U);

  ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0,
                                      red,
                                      green,
                                      blue));
  ESP_ERROR_CHECK(led_strip_refresh(led_strip));
}

static void rgb_color_cycle_task(void *arg)
{
  led_strip_handle_t led_strip = (led_strip_handle_t)arg;
  const uint32_t color_steps = RGB_COLOR_CYCLE_MS / RGB_UPDATE_PERIOD_MS;
  uint32_t color_step = 0;

  while (1)
  {
    rgb_set_frame(led_strip, color_step);

    color_step++;
    if (color_step >= color_steps)
    {
      color_step = 0;
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

  if (xTaskCreate(rgb_color_cycle_task,
                  "rgb_cycle",
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
