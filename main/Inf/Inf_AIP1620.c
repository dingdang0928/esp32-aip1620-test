#include "Inf_AIP1620.h"

#include <string.h>

#include "Common/Com_Debug.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"

#define AIP1620_CLK_GPIO GPIO_NUM_36
#define AIP1620_DIN_GPIO GPIO_NUM_39
#define AIP1620_STB_GPIO GPIO_NUM_40

#define AIP1620_CMD_MODE_6_GRID_8_SEG 0x02U
#define AIP1620_CMD_DATA_AUTO_ADDR 0x40U
#define AIP1620_CMD_ADDR_BASE 0xC0U
#define AIP1620_CMD_DISPLAY_BASE 0x80U
#define AIP1620_CMD_DISPLAY_ON_BIT 0x08U

#define AIP1620_GRID_COUNT 6U
#define AIP1620_DEFAULT_BRIGHTNESS_RAW 0U
#define AIP1620_HALF_CLOCK_US 1U

#define AIP1620_SEG1 (1U << 0)
#define AIP1620_SEG2 (1U << 1)
#define AIP1620_SEG3 (1U << 2)
#define AIP1620_SEG4 (1U << 3)

#define AIP1620_COLON_SEGMENTS (AIP1620_SEG1 | AIP1620_SEG2)
#define AIP1620_ICON_SEGMENTS \
  (AIP1620_SEG1 | AIP1620_SEG2 | AIP1620_SEG3 | AIP1620_SEG4)

typedef enum
{
  AIP1620_GRID_DIGIT_1 = 0,
  AIP1620_GRID_DIGIT_2,
  AIP1620_GRID_DIGIT_3,
  AIP1620_GRID_DIGIT_4,
  AIP1620_GRID_COLON,
  AIP1620_GRID_ICONS,
} aip1620_grid_t;

static const uint8_t s_digit_segments[10] = {
    0x77U, 0x24U, 0x5DU, 0x6DU, 0x2EU, 0x6BU, 0x7BU, 0x25U, 0x7FU, 0x6FU,
};

static const uint8_t s_brightness_raw[INF_AIP1620_BRIGHTNESS_COUNT] = {
    0U, 1U, 2U, 3U, 7U,
};

static uint8_t s_grid_data[AIP1620_GRID_COUNT];
static uint8_t s_brightness = AIP1620_DEFAULT_BRIGHTNESS_RAW;
static bool s_display_enabled;
static bool s_initialized;

static esp_err_t AIP1620_GpioInit(void)
{
  gpio_set_level(AIP1620_CLK_GPIO, 1);
  gpio_set_level(AIP1620_DIN_GPIO, 0);
  gpio_set_level(AIP1620_STB_GPIO, 1);

  const gpio_config_t config = {
      .pin_bit_mask = (1ULL << AIP1620_CLK_GPIO) | (1ULL << AIP1620_DIN_GPIO) |
                      (1ULL << AIP1620_STB_GPIO),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };

  esp_err_t ret = gpio_config(&config);
  if (ret == ESP_OK)
  {
    gpio_set_level(AIP1620_CLK_GPIO, 1);
    gpio_set_level(AIP1620_DIN_GPIO, 0);
    gpio_set_level(AIP1620_STB_GPIO, 1);
    esp_rom_delay_us(AIP1620_HALF_CLOCK_US);
  }
  return ret;
}

static esp_err_t AIP1620_GpioRelease(void)
{
  const gpio_config_t config = {
      .pin_bit_mask = (1ULL << AIP1620_CLK_GPIO) | (1ULL << AIP1620_DIN_GPIO) |
                      (1ULL << AIP1620_STB_GPIO),
      .mode = GPIO_MODE_DISABLE,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  return gpio_config(&config);
}

static bool AIP1620_BusBegin(void)
{
  esp_err_t ret = AIP1620_GpioInit();
  if (ret != ESP_OK)
  {
    MY_LOGE("AiP1620 总线启用失败：%s", esp_err_to_name(ret));
    return false;
  }
  return true;
}

static void AIP1620_BusEnd(void)
{
  esp_err_t ret = AIP1620_GpioRelease();
  if (ret != ESP_OK)
  {
    MY_LOGE("AiP1620 总线释放失败：%s", esp_err_to_name(ret));
  }
}

static void AIP1620_WriteByte(uint8_t value)
{
  for (uint8_t bit = 0U; bit < 8U; ++bit)
  {
    gpio_set_level(AIP1620_CLK_GPIO, 0);
    gpio_set_level(AIP1620_DIN_GPIO, (value >> bit) & 0x01U);
    esp_rom_delay_us(AIP1620_HALF_CLOCK_US);
    gpio_set_level(AIP1620_CLK_GPIO, 1);
    esp_rom_delay_us(AIP1620_HALF_CLOCK_US);
  }
}

static esp_err_t AIP1620_WriteCommand(uint8_t command)
{
  if (!AIP1620_BusBegin())
  {
    return ESP_FAIL;
  }

  gpio_set_level(AIP1620_STB_GPIO, 0);
  esp_rom_delay_us(AIP1620_HALF_CLOCK_US);
  AIP1620_WriteByte(command);
  gpio_set_level(AIP1620_STB_GPIO, 1);
  esp_rom_delay_us(AIP1620_HALF_CLOCK_US);
  AIP1620_BusEnd();
  return ESP_OK;
}

static esp_err_t AIP1620_UpdateRam(void)
{
  esp_err_t ret = AIP1620_WriteCommand(AIP1620_CMD_DATA_AUTO_ADDR);
  if (ret != ESP_OK)
  {
    return ret;
  }
  if (!AIP1620_BusBegin())
  {
    return ESP_FAIL;
  }

  gpio_set_level(AIP1620_STB_GPIO, 0);
  esp_rom_delay_us(AIP1620_HALF_CLOCK_US);
  AIP1620_WriteByte(AIP1620_CMD_ADDR_BASE);
  for (uint8_t grid = 0U; grid < AIP1620_GRID_COUNT; ++grid)
  {
    AIP1620_WriteByte(s_grid_data[grid]);
    AIP1620_WriteByte(0x00U);
  }
  gpio_set_level(AIP1620_STB_GPIO, 1);
  esp_rom_delay_us(AIP1620_HALF_CLOCK_US);
  AIP1620_BusEnd();
  return ESP_OK;
}

static esp_err_t AIP1620_UpdateDisplayControl(void)
{
  uint8_t command = AIP1620_CMD_DISPLAY_BASE | s_brightness;
  if (s_display_enabled)
  {
    command |= AIP1620_CMD_DISPLAY_ON_BIT;
  }
  return AIP1620_WriteCommand(command);
}

esp_err_t Inf_AIP1620_Init(void)
{
  if (s_initialized)
  {
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t ret = AIP1620_GpioInit();
  if (ret != ESP_OK)
  {
    return ret;
  }

  memset(s_grid_data, 0, sizeof(s_grid_data));
  s_brightness = AIP1620_DEFAULT_BRIGHTNESS_RAW;
  s_display_enabled = true;

  ret = AIP1620_WriteCommand(AIP1620_CMD_MODE_6_GRID_8_SEG);
  if (ret == ESP_OK)
  {
    ret = AIP1620_UpdateRam();
  }
  if (ret == ESP_OK)
  {
    ret = AIP1620_UpdateDisplayControl();
  }
  if (ret != ESP_OK)
  {
    (void)AIP1620_GpioRelease();
    return ret;
  }

  s_initialized = true;
  return ESP_OK;
}

esp_err_t Inf_AIP1620_Deinit(void)
{
  if (!s_initialized)
  {
    return ESP_OK;
  }

  s_display_enabled = false;
  esp_err_t ret = AIP1620_UpdateDisplayControl();
  esp_err_t release_ret = AIP1620_GpioRelease();

  memset(s_grid_data, 0, sizeof(s_grid_data));
  s_brightness = AIP1620_DEFAULT_BRIGHTNESS_RAW;
  s_initialized = false;

  return (ret != ESP_OK) ? ret : release_ret;
}

esp_err_t Inf_AIP1620_WriteFrame(const inf_aip1620_frame_t *frame)
{
  if (frame == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }
  if (!s_initialized)
  {
    return ESP_ERR_INVALID_STATE;
  }

  for (uint8_t index = 0U; index < INF_AIP1620_DIGIT_COUNT; ++index)
  {
    uint8_t digit = frame->digits[index];
    if ((digit > 9U) && (digit != INF_AIP1620_DIGIT_BLANK))
    {
      return ESP_ERR_INVALID_ARG;
    }
    s_grid_data[index] =
        (digit == INF_AIP1620_DIGIT_BLANK) ? 0U : s_digit_segments[digit];
  }

  s_grid_data[AIP1620_GRID_COLON] =
      frame->colon_enabled ? AIP1620_COLON_SEGMENTS : 0U;
  s_grid_data[AIP1620_GRID_ICONS] = frame->icon_mask & AIP1620_ICON_SEGMENTS;
  return AIP1620_UpdateRam();
}

esp_err_t Inf_AIP1620_Clear(void)
{
  if (!s_initialized)
  {
    return ESP_ERR_INVALID_STATE;
  }
  memset(s_grid_data, 0, sizeof(s_grid_data));
  return AIP1620_UpdateRam();
}

esp_err_t Inf_AIP1620_SetEnabled(bool enabled)
{
  if (!s_initialized)
  {
    return ESP_ERR_INVALID_STATE;
  }
  s_display_enabled = enabled;
  return AIP1620_UpdateDisplayControl();
}

esp_err_t Inf_AIP1620_SetBrightness(inf_aip1620_brightness_t brightness)
{
  if ((uint8_t)brightness >= INF_AIP1620_BRIGHTNESS_COUNT)
  {
    return ESP_ERR_INVALID_ARG;
  }
  if (!s_initialized)
  {
    return ESP_ERR_INVALID_STATE;
  }

  s_brightness = s_brightness_raw[brightness];
  return AIP1620_UpdateDisplayControl();
}
