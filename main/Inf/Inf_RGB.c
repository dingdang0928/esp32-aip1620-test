#include "inf_rgb.h"


#define RGB_GPIO 38
#define RGB_BREATH_PERIOD_MS 2000
#define RGB_UPDATE_PERIOD_MS 20
#define RGB_MAX_BRIGHTNESS 100
#define RGB_TASK_STACK_SIZE 2048
#define RGB_TASK_PRIORITY 5
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
  TickType_t last_wake_time = xTaskGetTickCount();

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

    vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(RGB_UPDATE_PERIOD_MS));
  }
}

esp_err_t inf_rgb_init(void)
{
  led_strip_handle_t led_strip = NULL;
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

  esp_err_t err = led_strip_new_rmt_device(&strip_config,
                                           &rmt_config,
                                           &led_strip);
  if (err != ESP_OK)
  {
    return err;
  }

  if (xTaskCreate(rgb_breathing_task,
                  "rgb_breath",
                  RGB_TASK_STACK_SIZE,
                  (void *)led_strip,
                  RGB_TASK_PRIORITY,
                  NULL) != pdPASS)
  {
    led_strip_del(led_strip);
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}
