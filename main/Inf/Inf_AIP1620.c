#include "Inf_AIP1620.h"

#include <stdint.h>
#include <string.h>

#include "Common/Com_Debug.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#ifdef CONFIG_AIP1620_POWER_TEST
#include "Inf_AIP1620_Test.h"

#include <stdio.h>
#include <stdlib.h>

#include "driver/uart.h"
#include "driver/uart_vfs.h"
#endif

/* 硬件引脚：CLK=36,DIN=39,STB=40；灯板由外部电源供电。 */
#define AIP1620_CLK_GPIO GPIO_NUM_36
#define AIP1620_DIN_GPIO GPIO_NUM_39
#define AIP1620_STB_GPIO GPIO_NUM_40

/* 灯板使用 6 位 8 段：4 个数字位、1 个冒号位和 1 个图标位。 */
#define AIP1620_CMD_MODE_6_GRID_8_SEG 0x02U
#define AIP1620_CMD_DATA_AUTO_ADDR 0x40U
#define AIP1620_CMD_DATA_FIXED_ADDR 0x44U
#define AIP1620_CMD_ADDR_BASE 0xC0U
#define AIP1620_CMD_DISPLAY_BASE 0x80U
#define AIP1620_CMD_DISPLAY_ON_BIT 0x08U

#define AIP1620_GRID_COUNT 6U
#define AIP1620_DEFAULT_BRIGHTNESS 0U
#define AIP1620_RAW_BRIGHTNESS_MAX 8U
#define AIP1620_HALF_CLOCK_US 1U
#define AIP1620_BLINK_INTERVAL_MIN_MS 50U
#define AIP1620_BLINK_TASK_STACK_SIZE 3072U
#define AIP1620_BLINK_TASK_PRIORITY 4U
#define AIP1620_BLINK_EVENT_UPDATE (1UL << 0)
#define AIP1620_BLINK_EVENT_STOP (1UL << 1)

#ifdef CONFIG_AIP1620_POWER_TEST
#define AIP1620_POWER_TEST_TASK_STACK_SIZE 9182U
#define AIP1620_POWER_TEST_TASK_PRIORITY 5U
#define AIP1620_POWER_TEST_UART_RX_BUFFER_SIZE 512U
#define AIP1620_POWER_TEST_UART_RETRY_MS 20U
#define AIP1620_POWER_TEST_CLEAR_PREPARE_MS 1000U
#define AIP1620_POWER_TEST_ICON_BLINK_INTERVAL_MS 500U
#define AIP1620_POWER_TEST_ICON_BLINK_TASK_STACK_SIZE 4096U
#define AIP1620_LOW_BATTERY_EVENT_STOP (1UL << 0)
#define AIP1620_LOW_BATTERY_EVENT_BRIGHTNESS_UP (1UL << 1)
#define AIP1620_LOW_BATTERY_EVENT_BRIGHTNESS_DOWN (1UL << 2)
#endif

/* AiP1620 显示 RAM 的 bit0~bit7 分别对应 SEG1~SEG8。 */
#define AIP1620_SEG1 (1U << 0)
#define AIP1620_SEG2 (1U << 1)
#define AIP1620_SEG3 (1U << 2)
#define AIP1620_SEG4 (1U << 3)
#define AIP1620_SEG5 (1U << 4)
#define AIP1620_SEG6 (1U << 5)
#define AIP1620_SEG7 (1U << 6)

#define AIP1620_DIGIT_SEGMENTS                                                \
  (AIP1620_SEG1 | AIP1620_SEG2 | AIP1620_SEG3 | AIP1620_SEG4 | AIP1620_SEG5 | \
   AIP1620_SEG6 | AIP1620_SEG7)
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

#ifdef CONFIG_AIP1620_POWER_TEST
// 客户需求：亮度使用五个档位 0、1、2、3、7。
typedef enum
{
  BRIGHTNESS_LEVEL_1 = 0,
  BRIGHTNESS_LEVEL_2 = 1,
  BRIGHTNESS_LEVEL_3 = 2,
  BRIGHTNESS_LEVEL_4 = 3,
  BRIGHTNESS_LEVEL_5 = 7
} aip_1620_BrightnessLevel_t;

typedef enum
{
  AIP1620_ICON_STATE_IDLE = 0,
  AIP1620_ICON_STATE_ICON4_BLINK_ON,
  AIP1620_ICON_STATE_ICON4_BLINK_OFF,
  AIP1620_ICON_STATE_ICON4_STEADY,
  AIP1620_ICON_STATE_ALL_BLINK_ON,
  AIP1620_ICON_STATE_ALL_BLINK_OFF,
  AIP1620_ICON_STATE_ALL_STEADY,
  AIP1620_ICON_STATE_STOPPING,
} aip1620_icon_state_t;

typedef enum
{
  AIP1620_POWER_TEST_CMD_BRIGHTNESS_UP = 11,
  AIP1620_POWER_TEST_CMD_BRIGHTNESS_DOWN = 12,
  AIP1620_POWER_TEST_CMD_DISPLAY_ENABLE = 13,
  AIP1620_POWER_TEST_CMD_DISPLAY_DISABLE = 14,
  AIP1620_POWER_TEST_CMD_ICON4_BLINK = 15,
  AIP1620_POWER_TEST_CMD_ICON4_STEADY = 16,
  AIP1620_POWER_TEST_CMD_ALL_ICONS_BLINK = 17,
  AIP1620_POWER_TEST_CMD_ALL_ICONS_STEADY = 18,
  AIP1620_POWER_TEST_CMD_CONTENT_BLINK_ENABLE = 19,
  AIP1620_POWER_TEST_CMD_CONTENT_BLINK_DISABLE = 20,
} aip1620_power_test_command_t;
#endif

/* 灯板实际段位：SEG1~SEG7 = a、f、b、g、e、c、d。 */
static const uint8_t s_digit_segments[10] = {
    0x77U, 0x24U, 0x5DU, 0x6DU, 0x2EU, 0x6BU, 0x7BU, 0x25U, 0x7FU, 0x6FU,
};

/* 实测 RAM 下标 0～3 对应从左到右四个数字位。 */
static const aip1620_grid_t s_digit_grids[INF_AIP1620_DIGIT_COUNT] = {
    AIP1620_GRID_DIGIT_1,
    AIP1620_GRID_DIGIT_2,
    AIP1620_GRID_DIGIT_3,
    AIP1620_GRID_DIGIT_4,
};

/* 每个 GRID 只允许点亮灯板上实际连接的 SEG,屏蔽悬空段。 */
static const uint8_t s_grid_segment_masks[AIP1620_GRID_COUNT] = {
    AIP1620_DIGIT_SEGMENTS, AIP1620_DIGIT_SEGMENTS, AIP1620_DIGIT_SEGMENTS,
    AIP1620_DIGIT_SEGMENTS, AIP1620_COLON_SEGMENTS, AIP1620_ICON_SEGMENTS,
};
static uint8_t s_grid_data[AIP1620_GRID_COUNT];
static uint8_t s_blink_segments[AIP1620_GRID_COUNT];
static uint8_t s_brightness = AIP1620_DEFAULT_BRIGHTNESS;
static bool s_display_enabled;
static bool s_initialized;
static bool s_blink_visible = true;
static uint32_t s_blink_interval_ms;
static SemaphoreHandle_t s_driver_lock;
static TaskHandle_t s_blink_task_handle;
static TaskHandle_t s_blink_stop_waiter;

#ifdef CONFIG_AIP1620_POWER_TEST
static TaskHandle_t s_power_test_task_handle;
static TaskHandle_t s_icon_state_task_handle;
static TaskHandle_t s_icon_state_stop_waiter;
static volatile aip1620_icon_state_t s_icon_state = AIP1620_ICON_STATE_IDLE;
#endif

static bool AIP_1620_Lock(void)
{
  return (s_driver_lock != NULL) &&
         (xSemaphoreTakeRecursive(s_driver_lock, portMAX_DELAY) == pdTRUE);
}

static void AIP_1620_Unlock(void)
{
  (void)xSemaphoreGiveRecursive(s_driver_lock);
}

static uint8_t AIP_1620_GetRenderedGrid(aip1620_grid_t grid)
{
  uint8_t segments = s_grid_data[grid];
  if (!s_blink_visible)
  {
    segments &= (uint8_t)~s_blink_segments[grid];
  }
  return segments;
}

static void AIP_1620_SetGridSegments(aip1620_grid_t grid, uint8_t segments)
{
  s_grid_data[grid] = segments & s_grid_segment_masks[grid];
}

/** 初始化通信 GPIO。 */
static esp_err_t AIP_1620_GPIO_Init(void)
{
  /* 先准备输出锁存值,避免切换为输出时产生错误脉冲。 */
  gpio_set_level(AIP1620_CLK_GPIO, 1);
  gpio_set_level(AIP1620_DIN_GPIO, 0);
  gpio_set_level(AIP1620_STB_GPIO, 1);

  const gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << AIP1620_CLK_GPIO) | (1ULL << AIP1620_DIN_GPIO) |
                      (1ULL << AIP1620_STB_GPIO),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };

  esp_err_t ret = gpio_config(&io_conf);
  if (ret != ESP_OK)
  {
    return ret;
  }

  gpio_set_level(AIP1620_CLK_GPIO, 1);  // CLK 高电平为空闲状态
  gpio_set_level(AIP1620_DIN_GPIO, 0);
  gpio_set_level(AIP1620_STB_GPIO, 1);  // STB 高电平为空闲状态
  esp_rom_delay_us(AIP1620_HALF_CLOCK_US);
  return ESP_OK;
}

/** Disconnect the ESP32 digital input/output paths while AiP1620 keeps
 * displaying. */
static esp_err_t AIP_1620_GPIO_Release(void)
{
  const gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << AIP1620_CLK_GPIO) | (1ULL << AIP1620_DIN_GPIO) |
                      (1ULL << AIP1620_STB_GPIO),
      .mode = GPIO_MODE_DISABLE,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };

  return gpio_config(&io_conf);
}

static bool AIP_1620_BusBegin(void)
{
  esp_err_t ret = AIP_1620_GPIO_Init();
  if (ret != ESP_OK)
  {
    MY_LOGE("AIP1620总线启用失败：%s", esp_err_to_name(ret));
    return false;
  }
  return true;
}

static void AIP_1620_BusEnd(void)
{
  esp_err_t ret = AIP_1620_GPIO_Release();
  if (ret != ESP_OK)
  {
    MY_LOGE("AIP1620总线释放失败：%s", esp_err_to_name(ret));
  }
}

static void AIP_1620_Delay(void)
{
  esp_rom_delay_us(AIP1620_HALF_CLOCK_US);
}

/** 按 AiP1620 要求,以最低位优先发送一个字节。 */
// 采样要求:在STB是低电平的情况下,CLK是低电平,DIN输入想要的数据,拉高CLK,采样完成
// 当采样结束的时候,需要拉高STB
static void AIP_1620_WriteByte(uint8_t value)
{
  for (uint8_t bit = 0; bit < 8U; ++bit)
  {
    gpio_set_level(AIP1620_CLK_GPIO, 0);
    gpio_set_level(AIP1620_DIN_GPIO, (value >> bit) & 0x01U);
    AIP_1620_Delay();
    gpio_set_level(AIP1620_CLK_GPIO, 1);
    AIP_1620_Delay();
  }
}

/** 发送一条独立命令。 */
static void AIP_1620_WriteCommand(uint8_t command)
{
  if (!AIP_1620_BusBegin())
  {
    return;
  }

  gpio_set_level(AIP1620_STB_GPIO, 0);
  AIP_1620_Delay();
  AIP_1620_WriteByte(command);
  gpio_set_level(AIP1620_STB_GPIO, 1);
  AIP_1620_Delay();
  AIP_1620_BusEnd();
}

/** 将 6 个 GRID 的缓存一次写入显示 RAM。 */
static void AIP_1620_UpdateRam(void)
{
  AIP_1620_WriteCommand(AIP1620_CMD_DATA_AUTO_ADDR);

  if (!AIP_1620_BusBegin())
  {
    return;
  }

  gpio_set_level(AIP1620_STB_GPIO, 0);
  AIP_1620_Delay();
  AIP_1620_WriteByte(AIP1620_CMD_ADDR_BASE);
  for (uint8_t grid = 0; grid < AIP1620_GRID_COUNT; ++grid)
  {
    AIP_1620_WriteByte(AIP_1620_GetRenderedGrid((aip1620_grid_t)grid));
    AIP_1620_WriteByte(0x00U);  // 奇数地址仅用于 SEG13/SEG14,本灯板未使用
  }
  gpio_set_level(AIP1620_STB_GPIO, 1);
  AIP_1620_Delay();
  AIP_1620_BusEnd();
}

/** Write only one GRID byte without refreshing the other display RAM bytes. */
static void AIP_1620_UpdateGrid(aip1620_grid_t grid)
{
  if ((uint8_t)grid >= AIP1620_GRID_COUNT)
  {
    return;
  }

  AIP_1620_WriteCommand(AIP1620_CMD_DATA_FIXED_ADDR);

  if (!AIP_1620_BusBegin())
  {
    return;
  }

  gpio_set_level(AIP1620_STB_GPIO, 0);
  AIP_1620_Delay();
  AIP_1620_WriteByte(AIP1620_CMD_ADDR_BASE + ((uint8_t)grid * 2U));
  AIP_1620_WriteByte(AIP_1620_GetRenderedGrid(grid));
  gpio_set_level(AIP1620_STB_GPIO, 1);
  AIP_1620_Delay();
  AIP_1620_BusEnd();
}

/** 按当前开关状态和亮度刷新显示控制命令。 */
static void AIP_1620_UpdateDisplayControl(void)
{
  uint8_t command = AIP1620_CMD_DISPLAY_BASE | s_brightness;
  if (s_display_enabled)
  {
    command |= AIP1620_CMD_DISPLAY_ON_BIT;
  }
  AIP_1620_WriteCommand(command);
}

esp_err_t Inf_AIP_1620_Init(void)
{
  if (s_driver_lock == NULL)
  {
    s_driver_lock = xSemaphoreCreateRecursiveMutex();
    if (s_driver_lock == NULL)
    {
      return ESP_ERR_NO_MEM;
    }
  }

  if (!AIP_1620_Lock())
  {
    return ESP_FAIL;
  }

  if (s_initialized)
  {
    AIP_1620_Unlock();
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t ret = AIP_1620_GPIO_Init();
  if (ret != ESP_OK)
  {
    AIP_1620_Unlock();
    return ret;
  }

  memset(s_grid_data, 0, sizeof(s_grid_data));
  memset(s_blink_segments, 0, sizeof(s_blink_segments));
  s_brightness = AIP1620_DEFAULT_BRIGHTNESS;
  s_display_enabled = true;
  s_blink_visible = true;
  s_blink_interval_ms = 0U;

  /* 严格按照手册顺序：模式、数据方式、地址及 RAM、显示控制。 */
  AIP_1620_WriteCommand(AIP1620_CMD_MODE_6_GRID_8_SEG);
  AIP_1620_UpdateRam();  // 上电后先清 RAM,避免开启时出现乱码
  AIP_1620_UpdateDisplayControl();
  s_initialized = true;
  AIP_1620_Unlock();
  return ESP_OK;
}

esp_err_t Inf_AIP_1620_Deinit(void)
{
  esp_err_t blink_ret = Inf_AIP_1620_Blink_Stop();
  if (blink_ret != ESP_OK)
  {
    return blink_ret;
  }

#ifdef CONFIG_AIP1620_POWER_TEST
  if (s_icon_state != AIP1620_ICON_STATE_IDLE)
  {
    Inf_AIP_1620_Low_Battery_Warning_Disable();
  }
#endif

  if (!AIP_1620_Lock())
  {
    return s_initialized ? ESP_FAIL : ESP_OK;
  }

  if (!s_initialized)
  {
    AIP_1620_Unlock();
    return ESP_OK;
  }

  /* 先关闭显示输出,再释放通信引脚,避免 GPIO 失效时屏幕保持点亮。 */
  s_display_enabled = false;
  AIP_1620_UpdateDisplayControl();

  /* 软件缓存复位；下次 Init 会重新清空芯片显示 RAM。 */
  memset(s_grid_data, 0, sizeof(s_grid_data));
  memset(s_blink_segments, 0, sizeof(s_blink_segments));
  s_brightness = AIP1620_DEFAULT_BRIGHTNESS;
  s_blink_visible = true;
  s_blink_interval_ms = 0U;

  /* 灯板由外部电源供电,GPIO 设为高阻且关闭上下拉,避免额外漏电。 */
  esp_err_t ret = AIP_1620_GPIO_Release();
  if (ret != ESP_OK)
  {
    AIP_1620_Unlock();
    return ret;
  }

  s_initialized = false;
  AIP_1620_Unlock();
  return ESP_OK;
}

//清空RAM,不更改显示开关
void Inf_AIP_1620_ClearAll(void)
{
  if (!AIP_1620_Lock())
  {
    return;
  }
  memset(s_grid_data, 0, sizeof(s_grid_data));
  AIP_1620_UpdateRam();
  AIP_1620_Unlock();
}

#ifdef CONFIG_AIP1620_POWER_TEST
void Inf_AIP_1620_Display_All(void)
{
  if (!AIP_1620_Lock())
  {
    return;
  }
  memcpy(s_grid_data, s_grid_segment_masks, sizeof(s_grid_data));
  AIP_1620_UpdateRam();
  AIP_1620_Unlock();
}
#endif

void Inf_AIP_1620_Display_Enable(void)
{
  if (!AIP_1620_Lock())
  {
    return;
  }
  s_display_enabled = true;
  AIP_1620_UpdateDisplayControl();
  AIP_1620_Unlock();
}

void Inf_AIP_1620_Display_Disable(void)
{
  if (!AIP_1620_Lock())
  {
    return;
  }
  s_display_enabled = false;
  AIP_1620_UpdateDisplayControl();
  AIP_1620_Unlock();
}

static void AIP_1620_SetRawBrightness(uint8_t brightness)
{
  if (brightness >= AIP1620_RAW_BRIGHTNESS_MAX)
  {
    brightness = AIP1620_RAW_BRIGHTNESS_MAX - 1U;
  }
  s_brightness = brightness;
  AIP_1620_UpdateDisplayControl();
}

#ifdef CONFIG_AIP1620_POWER_TEST
void Inf_AIP_1620_Set_Brightness(uint8_t raw_brightness)
{
  if (!AIP_1620_Lock())
  {
    return;
  }
  AIP_1620_SetRawBrightness(raw_brightness);
  AIP_1620_Unlock();
}
#endif

esp_err_t Inf_AIP_1620_Set_Brightness_Level(inf_aip1620_brightness_t brightness)
{
  static const uint8_t raw_brightness[INF_AIP1620_BRIGHTNESS_COUNT] = {
      0U, 1U, 2U, 3U, 7U,
  };

  if ((uint8_t)brightness >= INF_AIP1620_BRIGHTNESS_COUNT)
  {
    return ESP_ERR_INVALID_ARG;
  }

  if (!AIP_1620_Lock())
  {
    return ESP_ERR_INVALID_STATE;
  }
  AIP_1620_SetRawBrightness(raw_brightness[brightness]);
  AIP_1620_Unlock();
  return ESP_OK;
}

#ifdef CONFIG_AIP1620_POWER_TEST
void Inf_AIP_1620_Display_Digit(uint8_t position, uint8_t digit)
{
  if ((position >= INF_AIP1620_DIGIT_COUNT) || (digit > 9U))
  {
    return;
  }

  if (!AIP_1620_Lock())
  {
    return;
  }
  AIP_1620_SetGridSegments(s_digit_grids[position], s_digit_segments[digit]);
  AIP_1620_UpdateRam();
  AIP_1620_Unlock();
}
#endif

void Inf_AIP_1620_Display_Number(uint16_t number, bool leading_zero)
{
  if (!AIP_1620_Lock())
  {
    return;
  }

  if (number > 9999U)
  {
    number = 9999U;
  }

  /* 从右向左拆分,最高位可按需留空。 */
  for (int position = (int)INF_AIP1620_DIGIT_COUNT - 1; position >= 0;
       --position)
  {
    uint8_t digit = number % 10U;
    bool visible = leading_zero || (number > 0U) ||
                   (position == (int)INF_AIP1620_DIGIT_COUNT - 1);
    AIP_1620_SetGridSegments(s_digit_grids[position],
                             visible ? s_digit_segments[digit] : 0x00U);
    number /= 10U;
  }
  AIP_1620_UpdateRam();
  AIP_1620_Unlock();
}

esp_err_t Inf_AIP_1620_Display_Time(uint8_t hour, uint8_t minute,
                                    bool colon_enable)
{
  if ((hour > 23U) || (minute > 59U))
  {
    return ESP_ERR_INVALID_ARG;
  }

  if (!AIP_1620_Lock())
  {
    return ESP_ERR_INVALID_STATE;
  }
  AIP_1620_SetGridSegments(s_digit_grids[0], s_digit_segments[hour / 10U]);
  AIP_1620_SetGridSegments(s_digit_grids[1], s_digit_segments[hour % 10U]);
  AIP_1620_SetGridSegments(s_digit_grids[2], s_digit_segments[minute / 10U]);
  AIP_1620_SetGridSegments(s_digit_grids[3], s_digit_segments[minute % 10U]);
  AIP_1620_SetGridSegments(AIP1620_GRID_COLON,
                           colon_enable ? AIP1620_COLON_SEGMENTS : 0x00U);
  AIP_1620_UpdateRam();
  AIP_1620_Unlock();
  return ESP_OK;
}

void Inf_AIP_1620_Display_Mid_Dot(bool enable)
{
  if (!AIP_1620_Lock())
  {
    return;
  }
  AIP_1620_SetGridSegments(AIP1620_GRID_COLON,
                           enable ? AIP1620_COLON_SEGMENTS : 0x00U);
  AIP_1620_UpdateRam();
  AIP_1620_Unlock();
}

void Inf_AIP_1620_Display_Icons(uint8_t icon_mask)
{
  if (!AIP_1620_Lock())
  {
    return;
  }
  uint8_t new_icon_data = icon_mask & s_grid_segment_masks[AIP1620_GRID_ICONS];
  if (s_grid_data[AIP1620_GRID_ICONS] == new_icon_data)
  {
    AIP_1620_Unlock();
    return;
  }

  s_grid_data[AIP1620_GRID_ICONS] = new_icon_data;
  AIP_1620_UpdateGrid(AIP1620_GRID_ICONS);
  AIP_1620_Unlock();
}

static void AIP_1620_ConfigureBlinkSegments(uint16_t content_mask)
{
  memset(s_blink_segments, 0, sizeof(s_blink_segments));

  for (uint8_t position = 0U; position < INF_AIP1620_DIGIT_COUNT; ++position)
  {
    if ((content_mask & (1U << position)) != 0U)
    {
      s_blink_segments[s_digit_grids[position]] = AIP1620_DIGIT_SEGMENTS;
    }
  }

  if ((content_mask & INF_AIP1620_BLINK_COLON) != 0U)
  {
    s_blink_segments[AIP1620_GRID_COLON] = AIP1620_COLON_SEGMENTS;
  }

  s_blink_segments[AIP1620_GRID_ICONS] =
      (uint8_t)((content_mask >> 5U) & AIP1620_ICON_SEGMENTS);
}

static void AIP_1620_BlinkTask(void *argument)
{
  (void)argument;

  while (true)
  {
    uint32_t interval_ms = AIP1620_BLINK_INTERVAL_MIN_MS;
    if (AIP_1620_Lock())
    {
      interval_ms = s_blink_interval_ms;
      AIP_1620_Unlock();
    }

    TickType_t wait_ticks = pdMS_TO_TICKS(interval_ms);
    if (wait_ticks == 0U)
    {
      wait_ticks = 1U;
    }

    uint32_t events = 0U;
    BaseType_t notified =
        xTaskNotifyWait(0U, UINT32_MAX, &events, wait_ticks);
    if ((notified == pdTRUE) &&
        ((events & AIP1620_BLINK_EVENT_STOP) != 0U))
    {
      break;
    }

    if (!AIP_1620_Lock())
    {
      break;
    }

    if ((notified == pdTRUE) &&
        ((events & AIP1620_BLINK_EVENT_UPDATE) != 0U))
    {
      s_blink_visible = true;
    }
    else
    {
      s_blink_visible = !s_blink_visible;
    }

    if (s_initialized)
    {
      AIP_1620_UpdateRam();
    }
    AIP_1620_Unlock();
  }

  TaskHandle_t stop_waiter = NULL;
  if (AIP_1620_Lock())
  {
    memset(s_blink_segments, 0, sizeof(s_blink_segments));
    s_blink_visible = true;
    s_blink_interval_ms = 0U;
    if (s_initialized)
    {
      AIP_1620_UpdateRam();
    }

    stop_waiter = s_blink_stop_waiter;
    s_blink_stop_waiter = NULL;
    s_blink_task_handle = NULL;
    AIP_1620_Unlock();
  }

  if (stop_waiter != NULL)
  {
    xTaskNotifyGive(stop_waiter);
  }
  vTaskDelete(NULL);
}

esp_err_t Inf_AIP_1620_Blink_Start(uint16_t content_mask,
                                    uint32_t interval_ms)
{
  if ((content_mask == INF_AIP1620_BLINK_NONE) ||
      ((content_mask & (uint16_t)~INF_AIP1620_BLINK_ALL) != 0U) ||
      (interval_ms < AIP1620_BLINK_INTERVAL_MIN_MS))
  {
    return ESP_ERR_INVALID_ARG;
  }

  if (!AIP_1620_Lock())
  {
    return ESP_ERR_INVALID_STATE;
  }
  if (!s_initialized)
  {
    AIP_1620_Unlock();
    return ESP_ERR_INVALID_STATE;
  }

  AIP_1620_ConfigureBlinkSegments(content_mask);
  s_blink_interval_ms = interval_ms;
  s_blink_visible = true;
  AIP_1620_UpdateRam();

  TaskHandle_t blink_task = s_blink_task_handle;
  if (blink_task != NULL)
  {
    AIP_1620_Unlock();
    return (xTaskNotify(blink_task, AIP1620_BLINK_EVENT_UPDATE, eSetBits) ==
            pdPASS)
               ? ESP_OK
               : ESP_FAIL;
  }

  BaseType_t result =
      xTaskCreate(AIP_1620_BlinkTask, "aip1620_blink",
                  AIP1620_BLINK_TASK_STACK_SIZE, NULL,
                  AIP1620_BLINK_TASK_PRIORITY, &s_blink_task_handle);
  if (result != pdPASS)
  {
    s_blink_task_handle = NULL;
    memset(s_blink_segments, 0, sizeof(s_blink_segments));
    s_blink_interval_ms = 0U;
    AIP_1620_Unlock();
    return ESP_ERR_NO_MEM;
  }

  AIP_1620_Unlock();
  return ESP_OK;
}

esp_err_t Inf_AIP_1620_Blink_Stop(void)
{
  if (s_driver_lock == NULL)
  {
    return ESP_OK;
  }
  if (!AIP_1620_Lock())
  {
    return ESP_FAIL;
  }

  TaskHandle_t blink_task = s_blink_task_handle;
  if (blink_task == NULL)
  {
    memset(s_blink_segments, 0, sizeof(s_blink_segments));
    s_blink_visible = true;
    s_blink_interval_ms = 0U;
    AIP_1620_Unlock();
    return ESP_OK;
  }
  if ((blink_task == xTaskGetCurrentTaskHandle()) ||
      (s_blink_stop_waiter != NULL))
  {
    AIP_1620_Unlock();
    return ESP_ERR_INVALID_STATE;
  }

  s_blink_stop_waiter = xTaskGetCurrentTaskHandle();
  AIP_1620_Unlock();

  if (xTaskNotify(blink_task, AIP1620_BLINK_EVENT_STOP, eSetBits) != pdPASS)
  {
    if (AIP_1620_Lock())
    {
      s_blink_stop_waiter = NULL;
      AIP_1620_Unlock();
    }
    return ESP_FAIL;
  }

  (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  return ESP_OK;
}

#ifdef CONFIG_AIP1620_POWER_TEST
static const aip_1620_BrightnessLevel_t s_power_test_brightness[] = {
    BRIGHTNESS_LEVEL_1, BRIGHTNESS_LEVEL_2, BRIGHTNESS_LEVEL_3,
    BRIGHTNESS_LEVEL_4, BRIGHTNESS_LEVEL_5,
};

/** 将系统控制台UART切换为驱动模式,使getchar在无数据时真正阻塞。 */
static esp_err_t AIP_1620_PowerTest_UART_Init(void)
{
  const uart_port_t uart_port = (uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM;

  if (!uart_is_driver_installed(uart_port))
  {
    esp_err_t ret = uart_driver_install(
        uart_port, AIP1620_POWER_TEST_UART_RX_BUFFER_SIZE, 0, 0, NULL, 0);
    if (ret != ESP_OK)
    {
      return ret;
    }
  }

  uart_vfs_dev_use_driver(uart_port);
  return ESP_OK;
}

static void AIP_1620_PowerTest_PrintMenu(void)
{
  MY_LOGI(" ========== AIP1620 POWER TEST ========== ");
  MY_LOGI("0: P0 ESP32断电基线(AIP1620外部电源保持供电) ");
  MY_LOGI("1: P1 上电但未初始化 ");
  MY_LOGI("2: P2 初始化后,RAM全0,显示开启 ");
  MY_LOGI("3: P3 全亮最高档 -> ClearAll瞬态测试 ");
  MY_LOGI("4: P4 RAM全0,显示关闭 ");
  MY_LOGI("5: P5 全亮页面,最低亮度 ");
  MY_LOGI("6: P6 全亮页面,最高亮度 ");
  MY_LOGI("7: P7 RAM全亮,显示关闭 ");
  MY_LOGI("8: P8 Deinit,GPIO释放 ");
  MY_LOGI("9: P9 先进入最低功耗,再手动断开ESP32供电 ");
  MY_LOGI("10: P10 软件可控最低功耗(RAM全0、显示关闭、GPIO高阻) ");
  MY_LOGI("11: 增加一档亮度 ");
  MY_LOGI("12: 降低一档亮度 ");
  MY_LOGI("13: 开启显示 ");
  MY_LOGI("14: 关闭显示(保留当前RAM和亮度) ");
  MY_LOGI("15: 最下方ICON4持续闪烁(低电量提醒) ");
  MY_LOGI("16: 最下方ICON4常亮 ");
  MY_LOGI("17: 全部ICON持续闪烁 ");
  MY_LOGI("18: 全部ICON常亮 ");
  MY_LOGI("19: 当前全部显示内容开始闪烁(亮/灭各500ms) ");
  MY_LOGI("20: 停止内容闪烁并恢复完整显示 ");
  MY_LOGI("21~30: 预留 ");
  MY_LOGI("31~35: 典型页面,亮度1~5 ");
  MY_LOGI("41~45: 全亮页面,亮度1~5 ");
  MY_LOGI("51~55: 空白页面,亮度1~5 ");
  MY_LOGI("可使用11/12调节亮度(支持ICON状态下) ");
  MY_LOGI("请输入编号并回车： ");
}

static bool AIP_1620_PowerTest_ReadCommand(int *command)
{
  char input[8];
  size_t length = 0U;
  bool invalid_input = false;

  while (true)
  {
    int ch = getchar();

    if (ch == EOF)
    {
      /* 异常情况下避免EOF导致任务空转和日志刷屏。 */
      vTaskDelay(pdMS_TO_TICKS(AIP1620_POWER_TEST_UART_RETRY_MS));
      continue;
    }

    if ((ch == '\r') || (ch == '\n'))
    {
      if ((length == 0U) && !invalid_input)
      {
        continue;
      }
      input[length] = '\0';
      break;
    }

    bool is_digit = (ch >= '0') && (ch <= '9');
    if (is_digit && (length < (sizeof(input) - 1U)))
    {
      input[length++] = (char)ch;
    }
    else
    {
      invalid_input = true;
    }
  }

  if (invalid_input)
  {
    return false;
  }

  char *end = NULL;
  long value = strtol(input, &end, 10);
  if ((end == input) || (*end != '\0') || (value < 0L) || (value > 55L))
  {
    return false;
  }

  *command = (int)value;

  MY_LOGI("收到的编号是 %d ", *command);
  return true;
}

static void AIP_1620_PowerTest_AdjustBrightness(bool increase)
{
  size_t current_index = 0U;
  const size_t level_count =
      sizeof(s_power_test_brightness) / sizeof(s_power_test_brightness[0]);

  while ((current_index < level_count) &&
         ((uint8_t)s_power_test_brightness[current_index] != s_brightness))
  {
    ++current_index;
  }

  if (current_index >= level_count)
  {
    current_index = 0U;
  }

  if (increase)
  {
    if ((current_index + 1U) >= level_count)
    {
      MY_LOGI("已经是最高亮度(第5档) ");
      return;
    }
    ++current_index;
  }
  else
  {
    if (current_index == 0U)
    {
      MY_LOGI("已经是最低亮度(第1档) ");
      return;
    }
    --current_index;
  }

  Inf_AIP_1620_Set_Brightness((uint8_t)s_power_test_brightness[current_index]);
  MY_LOGI("当前亮度：第%u档,底层亮度值=%u ", (unsigned int)(current_index + 1U),
          (unsigned int)s_power_test_brightness[current_index]);
}

static esp_err_t AIP_1620_PowerTest_EnsureInit(void)
{
  if (s_initialized)
  {
    return ESP_OK;
  }

  MY_LOGI("POWER_TEST I0: INIT_START,请观察初始化期间的峰值电流 ");
  esp_err_t ret = Inf_AIP_1620_Init();
  if (ret == ESP_OK)
  {
    MY_LOGI("POWER_TEST I0: INIT_DONE ");
  }
  return ret;
}

static void AIP_1620_PowerTest_SetTypicalPage(uint8_t brightness)
{
  Inf_AIP_1620_Display_Number(1234U, true);
  Inf_AIP_1620_Display_Mid_Dot(true);
  Inf_AIP_1620_Display_Icons(INF_AIP1620_ICON_ALL);
  Inf_AIP_1620_Set_Brightness(brightness);
  Inf_AIP_1620_Display_Enable();
}

static void AIP_1620_PowerTest_SetAllPage(uint8_t brightness)
{
  Inf_AIP_1620_Display_All();
  Inf_AIP_1620_Set_Brightness(brightness);
  Inf_AIP_1620_Display_Enable();
}

static void AIP_1620_PowerTest_SetBlankPage(uint8_t brightness)
{
  Inf_AIP_1620_ClearAll();
  Inf_AIP_1620_Set_Brightness(brightness);
  Inf_AIP_1620_Display_Enable();
}

static void AIP_1620_IconStateTask(void *argument)
{
  (void)argument;

  while (s_icon_state != AIP1620_ICON_STATE_STOPPING)
  {
    TickType_t wait_ticks = portMAX_DELAY;

    switch (s_icon_state)
    {
      case AIP1620_ICON_STATE_ICON4_BLINK_ON:
        Inf_AIP_1620_Display_Icons(INF_AIP1620_ICON_4);
        s_icon_state = AIP1620_ICON_STATE_ICON4_BLINK_OFF;
        wait_ticks = pdMS_TO_TICKS(AIP1620_POWER_TEST_ICON_BLINK_INTERVAL_MS);
        break;

      case AIP1620_ICON_STATE_ICON4_BLINK_OFF:
        Inf_AIP_1620_Display_Icons(0U);
        s_icon_state = AIP1620_ICON_STATE_ICON4_BLINK_ON;
        wait_ticks = pdMS_TO_TICKS(AIP1620_POWER_TEST_ICON_BLINK_INTERVAL_MS);
        break;

      case AIP1620_ICON_STATE_ICON4_STEADY:
        Inf_AIP_1620_Display_Icons(INF_AIP1620_ICON_4);
        break;

      case AIP1620_ICON_STATE_ALL_BLINK_ON:
        Inf_AIP_1620_Display_Icons(INF_AIP1620_ICON_ALL);
        s_icon_state = AIP1620_ICON_STATE_ALL_BLINK_OFF;
        wait_ticks = pdMS_TO_TICKS(AIP1620_POWER_TEST_ICON_BLINK_INTERVAL_MS);
        break;

      case AIP1620_ICON_STATE_ALL_BLINK_OFF:
        Inf_AIP_1620_Display_Icons(0U);
        s_icon_state = AIP1620_ICON_STATE_ALL_BLINK_ON;
        wait_ticks = pdMS_TO_TICKS(AIP1620_POWER_TEST_ICON_BLINK_INTERVAL_MS);
        break;

      case AIP1620_ICON_STATE_ALL_STEADY:
        Inf_AIP_1620_Display_Icons(INF_AIP1620_ICON_ALL);
        break;

      default:
        s_icon_state = AIP1620_ICON_STATE_STOPPING;
        break;
    }

    if (s_icon_state == AIP1620_ICON_STATE_STOPPING)
    {
      break;
    }

    uint32_t events = 0U;
    BaseType_t notified = xTaskNotifyWait(0U, UINT32_MAX, &events, wait_ticks);

    if (notified == pdTRUE)
    {
      if ((events & AIP1620_LOW_BATTERY_EVENT_STOP) != 0U)
      {
        s_icon_state = AIP1620_ICON_STATE_STOPPING;
      }
      else
      {
        if ((events & AIP1620_LOW_BATTERY_EVENT_BRIGHTNESS_UP) != 0U)
        {
          AIP_1620_PowerTest_AdjustBrightness(true);
        }
        if ((events & AIP1620_LOW_BATTERY_EVENT_BRIGHTNESS_DOWN) != 0U)
        {
          AIP_1620_PowerTest_AdjustBrightness(false);
        }
      }
    }
  }

  /* 只有退出当前 ICON 状态时才熄灭图标。 */
  Inf_AIP_1620_Display_Icons(0U);

  TaskHandle_t stop_waiter = s_icon_state_stop_waiter;
  s_icon_state_stop_waiter = NULL;
  s_icon_state_task_handle = NULL;
  s_icon_state = AIP1620_ICON_STATE_IDLE;

  if (stop_waiter != NULL)
  {
    xTaskNotifyGive(stop_waiter);
  }
  vTaskDelete(NULL);
}

static esp_err_t AIP_1620_StartIconState(aip1620_icon_state_t initial_state)
{
  if (!s_initialized)
  {
    return ESP_ERR_INVALID_STATE;
  }
  if (s_icon_state != AIP1620_ICON_STATE_IDLE)
  {
    return ESP_ERR_INVALID_STATE;
  }

  Inf_AIP_1620_Display_Enable();
  s_icon_state = initial_state;

  BaseType_t result =
      xTaskCreate(AIP_1620_IconStateTask, "aip1620_icon_state",
                  AIP1620_POWER_TEST_ICON_BLINK_TASK_STACK_SIZE, NULL,
                  AIP1620_POWER_TEST_TASK_PRIORITY, &s_icon_state_task_handle);
  if (result != pdPASS)
  {
    s_icon_state_task_handle = NULL;
    s_icon_state = AIP1620_ICON_STATE_IDLE;
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}

esp_err_t Inf_AIP_1620_Low_Battery_Warning_Enable(void)
{
  if ((s_icon_state == AIP1620_ICON_STATE_ICON4_BLINK_ON) ||
      (s_icon_state == AIP1620_ICON_STATE_ICON4_BLINK_OFF))
  {
    return ESP_OK;
  }
  if (s_icon_state != AIP1620_ICON_STATE_IDLE)
  {
    Inf_AIP_1620_Low_Battery_Warning_Disable();
  }

  return AIP_1620_StartIconState(AIP1620_ICON_STATE_ICON4_BLINK_ON);
}

static bool AIP_1620_IconStateAdjustBrightness(bool increase)
{
  TaskHandle_t icon_task = s_icon_state_task_handle;
  if (icon_task == NULL)
  {
    return false;
  }

  uint32_t event = increase ? AIP1620_LOW_BATTERY_EVENT_BRIGHTNESS_UP
                            : AIP1620_LOW_BATTERY_EVENT_BRIGHTNESS_DOWN;
  return xTaskNotify(icon_task, event, eSetBits) == pdPASS;
}

void Inf_AIP_1620_Low_Battery_Warning_Disable(void)
{
  TaskHandle_t icon_task = s_icon_state_task_handle;
  if (icon_task == NULL)
  {
    s_icon_state = AIP1620_ICON_STATE_IDLE;
    return;
  }

  s_icon_state_stop_waiter = xTaskGetCurrentTaskHandle();
  s_icon_state = AIP1620_ICON_STATE_STOPPING;
  (void)xTaskNotify(icon_task, AIP1620_LOW_BATTERY_EVENT_STOP, eSetBits);
  (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

static int AIP_1620_PowerTest_GetIconModeCommand(void)
{
  switch (s_icon_state)
  {
    case AIP1620_ICON_STATE_ICON4_BLINK_ON:
    case AIP1620_ICON_STATE_ICON4_BLINK_OFF:
      return AIP1620_POWER_TEST_CMD_ICON4_BLINK;
    case AIP1620_ICON_STATE_ICON4_STEADY:
      return AIP1620_POWER_TEST_CMD_ICON4_STEADY;
    case AIP1620_ICON_STATE_ALL_BLINK_ON:
    case AIP1620_ICON_STATE_ALL_BLINK_OFF:
      return AIP1620_POWER_TEST_CMD_ALL_ICONS_BLINK;
    case AIP1620_ICON_STATE_ALL_STEADY:
      return AIP1620_POWER_TEST_CMD_ALL_ICONS_STEADY;
    default:
      return -1;
  }
}

static esp_err_t AIP_1620_PowerTest_EnterIconMode(int command)
{
  if (AIP_1620_PowerTest_GetIconModeCommand() == command)
  {
    MY_LOGI("POWER_TEST ICON_MODE: 当前模式%d保持运行。 ", command);
    return ESP_OK;
  }

  esp_err_t ret = Inf_AIP_1620_Blink_Stop();
  if (ret != ESP_OK)
  {
    return ret;
  }

  if (s_icon_state != AIP1620_ICON_STATE_IDLE)
  {
    Inf_AIP_1620_Low_Battery_Warning_Disable();
  }

  ret = AIP_1620_PowerTest_EnsureInit();
  if (ret != ESP_OK)
  {
    return ret;
  }

  aip1620_icon_state_t initial_state;
  const char *mode_name;
  switch (command)
  {
    case AIP1620_POWER_TEST_CMD_ICON4_BLINK:
      initial_state = AIP1620_ICON_STATE_ICON4_BLINK_ON;
      mode_name = "ICON4_BLINK";
      break;
    case AIP1620_POWER_TEST_CMD_ICON4_STEADY:
      initial_state = AIP1620_ICON_STATE_ICON4_STEADY;
      mode_name = "ICON4_STEADY";
      break;
    case AIP1620_POWER_TEST_CMD_ALL_ICONS_BLINK:
      initial_state = AIP1620_ICON_STATE_ALL_BLINK_ON;
      mode_name = "ALL_ICONS_BLINK";
      break;
    case AIP1620_POWER_TEST_CMD_ALL_ICONS_STEADY:
      initial_state = AIP1620_ICON_STATE_ALL_STEADY;
      mode_name = "ALL_ICONS_STEADY";
      break;
    default:
      return ESP_ERR_INVALID_ARG;
  }

  ret = AIP_1620_StartIconState(initial_state);
  if (ret == ESP_OK)
  {
    MY_LOGI(
        "POWER_TEST ICON_MODE: ENTER %s,RAW_BRIGHTNESS=%u；"
        "闪烁模式周期=1s,当前模式可使用11/12调节亮度。 ",
        mode_name, (unsigned int)s_brightness);
  }
  return ret;
}

static esp_err_t AIP_1620_PowerTest_EnterLowestPower(void)
{
  esp_err_t ret = AIP_1620_PowerTest_EnsureInit();
  if (ret != ESP_OK)
  {
    return ret;
  }

  Inf_AIP_1620_ClearAll();
  return Inf_AIP_1620_Deinit();
}

/** 通过串口逐项执行功耗测试；getchar没有输入时阻塞当前测试任务。 */
static void AIP_1620_PowerTest_Task(void *argument)
{
  (void)argument;

  AIP_1620_PowerTest_PrintMenu();

  while (true)
  {
    int command;
    if (!AIP_1620_PowerTest_ReadCommand(&command))
    {
      MY_LOGI("输入无效,请输入菜单命令并回车。 ");
      continue;
    }

    bool is_icon_mode_command =
        (command >= AIP1620_POWER_TEST_CMD_ICON4_BLINK) &&
        (command <= AIP1620_POWER_TEST_CMD_ALL_ICONS_STEADY);
    bool is_brightness_command =
        (command == AIP1620_POWER_TEST_CMD_BRIGHTNESS_UP) ||
        (command == AIP1620_POWER_TEST_CMD_BRIGHTNESS_DOWN);
    if (!is_icon_mode_command && !is_brightness_command &&
        (s_icon_state != AIP1620_ICON_STATE_IDLE))
    {
      Inf_AIP_1620_Low_Battery_Warning_Disable();
      MY_LOGI("POWER_TEST ICON_MODE: EXIT,已切换到其他状态,全部ICON已熄灭。 ");
    }

    esp_err_t ret = ESP_OK;
    switch (command)
    {
      case AIP1620_POWER_TEST_CMD_BRIGHTNESS_UP:
      case AIP1620_POWER_TEST_CMD_BRIGHTNESS_DOWN:
        if (s_icon_state != AIP1620_ICON_STATE_IDLE)
        {
          if (!AIP_1620_IconStateAdjustBrightness(
                  command == AIP1620_POWER_TEST_CMD_BRIGHTNESS_UP))
          {
            ret = ESP_FAIL;
          }
        }
        else
        {
          ret = AIP_1620_PowerTest_EnsureInit();
          if (ret == ESP_OK)
          {
            AIP_1620_PowerTest_AdjustBrightness(
                command == AIP1620_POWER_TEST_CMD_BRIGHTNESS_UP);
          }
        }
        break;

      case AIP1620_POWER_TEST_CMD_DISPLAY_ENABLE:
      {
        bool was_initialized = s_initialized;
        ret = AIP_1620_PowerTest_EnsureInit();
        if (ret == ESP_OK)
        {
          Inf_AIP_1620_Display_Enable();
          if (was_initialized)
          {
            MY_LOGI(
                "POWER_TEST DISPLAY_ENABLE: DISPLAY=ON , "
                "RAW_BRIGHTNESS=%u,RAM内容保持不变,请记录V/I/P。 ",
                (unsigned int)s_brightness);
          }
          else
          {
            MY_LOGI(
                "POWER_TEST DISPLAY_ENABLE: 已重新初始化,DISPLAY=ON , "
                "RAM=ALL_ZERO , RAW_BRIGHTNESS=%u,请记录V/I/P。 ",
                (unsigned int)s_brightness);
          }
        }
        break;
      }

      case AIP1620_POWER_TEST_CMD_DISPLAY_DISABLE:
        if (s_initialized)
        {
          Inf_AIP_1620_Display_Disable();
          MY_LOGI(
              "POWER_TEST DISPLAY_DISABLE: DISPLAY=OFF , "
              "RAW_BRIGHTNESS=%u,RAM内容保持不变,请记录V/I/P。 ",
              (unsigned int)s_brightness);
        }
        else
        {
          MY_LOGI(
              "POWER_TEST DISPLAY_DISABLE: "
              "当前未初始化,显示已处于关闭状态。 ");
        }
        break;

      case AIP1620_POWER_TEST_CMD_ICON4_BLINK:
      case AIP1620_POWER_TEST_CMD_ICON4_STEADY:
      case AIP1620_POWER_TEST_CMD_ALL_ICONS_BLINK:
      case AIP1620_POWER_TEST_CMD_ALL_ICONS_STEADY:
        ret = AIP_1620_PowerTest_EnterIconMode(command);
        break;

      case AIP1620_POWER_TEST_CMD_CONTENT_BLINK_ENABLE:
        ret = AIP_1620_PowerTest_EnsureInit();
        if (ret == ESP_OK)
        {
          ret = Inf_AIP_1620_Blink_Start(
              INF_AIP1620_BLINK_ALL,
              AIP1620_POWER_TEST_ICON_BLINK_INTERVAL_MS);
        }
        if (ret == ESP_OK)
        {
          MY_LOGI(
              "POWER_TEST CONTENT_BLINK: ENABLED,全部当前显示内容闪烁,"
              "亮/灭各500ms。 ");
        }
        break;

      case AIP1620_POWER_TEST_CMD_CONTENT_BLINK_DISABLE:
        ret = Inf_AIP_1620_Blink_Stop();
        if (ret == ESP_OK)
        {
          MY_LOGI(
              "POWER_TEST CONTENT_BLINK: DISABLED,已恢复完整显示内容。 ");
        }
        break;

      case 0:
        MY_LOGI(
            "POWER_TEST P0: "
            "请保持AIP1620外部电源供电,手动断开ESP32供电,"
            "然后记录AIP1620供电回路的V/I/P；完成后重新给ESP32上电。 ");
        break;

      case 1:
        if (s_initialized)
        {
          MY_LOGI("POWER_TEST P1: 当前已经初始化,请先输入8执行Deinit。 ");
        }
        else
        {
          MY_LOGI("POWER_TEST P1: POWER_ON , NOT_INITIALIZED,请记录V/I/P。 ");
        }
        break;

      case 2:
        ret = AIP_1620_PowerTest_EnsureInit();
        if (ret == ESP_OK)
        {
          Inf_AIP_1620_ClearAll();
          Inf_AIP_1620_Display_Enable();
          MY_LOGI("POWER_TEST P2: RAM=ALL_ZERO , DISPLAY=ON,请记录V/I/P。 ");
        }
        break;

      case 3:
        ret = AIP_1620_PowerTest_EnsureInit();
        if (ret == ESP_OK)
        {
          AIP_1620_PowerTest_SetAllPage(s_power_test_brightness[4]);
          MY_LOGI(
              "POWER_TEST P3: PREPARE , RAM=ALL_SEGMENTS , DISPLAY=ON , "
              "RAW_BRIGHTNESS=%u,1秒后执行ClearAll。 ",
              (unsigned int)s_power_test_brightness[4]);
          vTaskDelay(pdMS_TO_TICKS(AIP1620_POWER_TEST_CLEAR_PREPARE_MS));
          MY_LOGI("POWER_TEST P3: CLEAR_START ");
          Inf_AIP_1620_ClearAll();
          MY_LOGI(
              "POWER_TEST P3: CLEAR_DONE , RAM=ALL_ZERO , DISPLAY=ON , "
              "RAW_BRIGHTNESS=%u,请记录瞬态和稳态V/I/P。 ",
              (unsigned int)s_power_test_brightness[4]);
          Inf_AIP_1620_Display_Disable();
        }
        break;

      case 4:
        ret = AIP_1620_PowerTest_EnsureInit();
        if (ret == ESP_OK)
        {
          Inf_AIP_1620_ClearAll();
          Inf_AIP_1620_Display_Disable();
          MY_LOGI("POWER_TEST P4: RAM=ALL_ZERO , DISPLAY=OFF,请记录V/I/P。 ");
        }
        break;

      case 5:
        ret = AIP_1620_PowerTest_EnsureInit();
        if (ret == ESP_OK)
        {
          AIP_1620_PowerTest_SetAllPage(s_power_test_brightness[0]);
          MY_LOGI(
              "POWER_TEST P5: RAM=ALL_SEGMENTS , DISPLAY=ON , "
              "RAW_BRIGHTNESS=0,请记录V/I/P。 ");
        }
        break;

      case 6:
        ret = AIP_1620_PowerTest_EnsureInit();
        if (ret == ESP_OK)
        {
          AIP_1620_PowerTest_SetAllPage(s_power_test_brightness[4]);
          MY_LOGI(
              "POWER_TEST P6: RAM=ALL_SEGMENTS , DISPLAY=ON , "
              "RAW_BRIGHTNESS=%u,请记录V/I/P。 ",
              (unsigned int)s_power_test_brightness[4]);
        }
        break;

      case 7:
        ret = AIP_1620_PowerTest_EnsureInit();
        if (ret == ESP_OK)
        {
          Inf_AIP_1620_Display_All();
          Inf_AIP_1620_Display_Disable();
          MY_LOGI(
              "POWER_TEST P7: RAM=ALL_SEGMENTS , "
              "DISPLAY=OFF,请记录V/I/P。 ");
        }
        break;

      case 8:
        ret = Inf_AIP_1620_Deinit();
        if (ret == ESP_OK)
        {
          MY_LOGI(
              "POWER_TEST P8: "
              "DEINIT完成,AIP1620仍由外部电源供电,请记录V/I/P。 ");
        }
        break;

      case 9:
        ret = AIP_1620_PowerTest_EnterLowestPower();
        if (ret == ESP_OK)
        {
          MY_LOGI(
              "POWER_TEST P9: PREPARED , RAM=ALL_ZERO , DISPLAY=OFF , "
              "GPIO=HIGH_Z。请先记录ESP32供电时的AIP1620 V/I/P,"
              "再手动断开ESP32供电并重新记录；AIP1620外部电源保持供电。 ");
        }
        break;

      case 10:
        ret = AIP_1620_PowerTest_EnterLowestPower();
        if (ret == ESP_OK)
        {
          MY_LOGI(
              "POWER_TEST P10: LOWEST_POWER , RAM=ALL_ZERO , "
              "DISPLAY=OFF , GPIO=HIGH_Z,ESP32和AIP1620均保持供电,"
              "请等待电流稳定后记录AIP1620回路的V/I/P。 ");
        }
        break;
      default:
        if ((command >= 31) && (command <= 35))
        {
          uint8_t index = (uint8_t)(command - 31);
          ret = AIP_1620_PowerTest_EnsureInit();
          if (ret == ESP_OK)
          {
            AIP_1620_PowerTest_SetTypicalPage(s_power_test_brightness[index]);
            MY_LOGI(
                "POWER_TEST T1-%u: RAM=TYPICAL_1234_COLON_ICONS , "
                "DISPLAY=ON , RAW_BRIGHTNESS=%u,请记录V/I/P。 ",
                (unsigned int)(index + 1U),
                (unsigned int)s_power_test_brightness[index]);
          }
        }
        else if ((command >= 41) && (command <= 45))
        {
          uint8_t index = (uint8_t)(command - 41);
          ret = AIP_1620_PowerTest_EnsureInit();
          if (ret == ESP_OK)
          {
            AIP_1620_PowerTest_SetAllPage(s_power_test_brightness[index]);
            MY_LOGI(
                "POWER_TEST T2-%u: RAM=ALL_SEGMENTS , DISPLAY=ON , "
                "RAW_BRIGHTNESS=%u,请记录V/I/P。 ",
                (unsigned int)(index + 1U),
                (unsigned int)s_power_test_brightness[index]);
          }
        }
        else if ((command >= 51) && (command <= 55))
        {
          uint8_t index = (uint8_t)(command - 51);
          ret = AIP_1620_PowerTest_EnsureInit();
          if (ret == ESP_OK)
          {
            AIP_1620_PowerTest_SetBlankPage(s_power_test_brightness[index]);
            MY_LOGI(
                "POWER_TEST T3-%u: RAM=ALL_ZERO , DISPLAY=ON , "
                "RAW_BRIGHTNESS=%u,请记录V/I/P。 ",
                (unsigned int)(index + 1U),
                (unsigned int)s_power_test_brightness[index]);
          }
        }
        else
        {
          MY_LOGI("输入无效,请输入0~18、31~35、41~45或51~55。 ");
        }
        break;
    }

    if (ret != ESP_OK)
    {
      MY_LOGE("POWER_TEST执行失败,错误码=%d ", (int)ret);
    }
  }
}

/**
 * @brief 创建串口功耗测试任务。
 *
 * 本函数只创建任务并立即返回；测试任务会阻塞等待串口输入,不会占住app_main。
 */
esp_err_t Inf_AIP_1620_Power_Test(void)
{
  if (s_power_test_task_handle != NULL)
  {
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t ret = AIP_1620_PowerTest_UART_Init();
  if (ret != ESP_OK)
  {
    return ret;
  }

  BaseType_t result =
      xTaskCreate(AIP_1620_PowerTest_Task, "aip1620_power_test",
                  AIP1620_POWER_TEST_TASK_STACK_SIZE, NULL,
                  AIP1620_POWER_TEST_TASK_PRIORITY, &s_power_test_task_handle);
  return (result == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}
#endif /* CONFIG_AIP1620_POWER_TEST */
