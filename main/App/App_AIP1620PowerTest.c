#include "App_AIP1620PowerTest.h"

#include "sdkconfig.h"

#if CONFIG_AIP1620_POWER_TEST

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "APP_DisplayAIP1620.h"
#include "Common/Com_Debug.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define POWER_TEST_TASK_STACK_SIZE 6144U
#define POWER_TEST_TASK_PRIORITY 5U
#define POWER_TEST_UART_RX_BUFFER_SIZE 512U
#define POWER_TEST_UART_RETRY_MS 20U
#define POWER_TEST_BLINK_INTERVAL_MS 500U
#define POWER_TEST_CLEAR_PREPARE_MS 1000U

typedef enum
{
  POWER_TEST_ICON_MODE_NONE = 0,
  POWER_TEST_ICON_MODE_ICON4_BLINK,
  POWER_TEST_ICON_MODE_ICON4_STEADY,
  POWER_TEST_ICON_MODE_ALL_BLINK,
  POWER_TEST_ICON_MODE_ALL_STEADY,
} power_test_icon_mode_t;

static const app_display_aip1620_brightness_t s_brightness_levels[] = {
    APP_DISPLAY_AIP1620_BRIGHTNESS_1, APP_DISPLAY_AIP1620_BRIGHTNESS_2,
    APP_DISPLAY_AIP1620_BRIGHTNESS_3, APP_DISPLAY_AIP1620_BRIGHTNESS_4,
    APP_DISPLAY_AIP1620_BRIGHTNESS_5,
};

static TaskHandle_t s_power_test_task;
static bool s_display_initialized;
static uint8_t s_brightness_index;
static power_test_icon_mode_t s_icon_mode;

static esp_err_t App_AIP1620PowerTest_UartInit(void)
{
  const uart_port_t uart_port = (uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM;
  if (!uart_is_driver_installed(uart_port))
  {
    esp_err_t ret = uart_driver_install(
        uart_port, POWER_TEST_UART_RX_BUFFER_SIZE, 0, 0, NULL, 0);
    if (ret != ESP_OK)
    {
      return ret;
    }
  }
  uart_vfs_dev_use_driver(uart_port);
  return ESP_OK;
}

static void App_AIP1620PowerTest_PrintMenu(void)
{
  MY_LOGI("========== AiP1620 功耗测试菜单 ==========");
  MY_LOGI("----- 基础功耗场景 -----");
  MY_LOGI(" 0: P0  ESP32 断电基线（AiP1620 外部电源保持供电）");
  MY_LOGI(" 1: P1  ESP32 上电，AiP1620 未初始化");
  MY_LOGI(" 2: P2  初始化，RAM 全 0，显示开启");
  MY_LOGI(" 3: P3  全亮最高档，1 秒后清屏（Clear 瞬态测试）");
  MY_LOGI(" 4: P4  RAM 全 0，显示关闭");
  MY_LOGI(" 5: P5  全亮页面，最低亮度");
  MY_LOGI(" 6: P6  全亮页面，最高亮度");
  MY_LOGI(" 7: P7  RAM 全亮，显示关闭");
  MY_LOGI(" 8: P8  Deinit，显示关闭，GPIO 释放为高阻");
  MY_LOGI(" 9: P9  进入最低功耗，再手动断开 ESP32 供电");
  MY_LOGI("10: P10 软件最低功耗（RAM 全 0、显示关闭、GPIO 高阻）");

  MY_LOGI("----- 显示控制 -----");
  MY_LOGI("11: 亮度增加一档");
  MY_LOGI("12: 亮度降低一档");
  MY_LOGI("13: 开启显示（保留当前 RAM 和亮度）");
  MY_LOGI("14: 关闭显示（保留当前 RAM 和亮度）");
  MY_LOGI("15: 最下方 ICON4 持续闪烁（低电量提醒）");
  MY_LOGI("16: 最下方 ICON4 常亮");
  MY_LOGI("17: 全部 ICON 持续闪烁");
  MY_LOGI("18: 全部 ICON 常亮");
  MY_LOGI("19: 当前全部显示内容闪烁（亮、灭各 500 ms）");
  MY_LOGI("20: 停止内容闪烁并恢复完整显示");
  MY_LOGI("21~30: 预留命令");

  MY_LOGI("----- 页面与亮度组合 -----");
  MY_LOGI("31~35: 典型页面 1234 + 冒号 + 全部 ICON，亮度 1~5");
  MY_LOGI("41~45: 全亮页面 8888 + 冒号 + 全部 ICON，亮度 1~5");
  MY_LOGI("51~55: 空白页面，亮度 1~5");
  MY_LOGI("提示：ICON 模式运行时仍可使用 11/12 调节亮度");
  MY_LOGI("测量：场景稳定后记录 AiP1620 供电回路的 V/I/P");
  MY_LOGI("===========================================");
  MY_LOGI("请输入菜单编号并回车：");
}

static const char *App_AIP1620PowerTest_GetCommandDescription(int command)
{
  static const char *const descriptions[] = {
      "P0 ESP32 断电基线操作说明",
      "P1 ESP32 上电、AiP1620 未初始化操作说明",
      "P2 RAM 全 0，显示开启",
      "P3 全亮最高档，1 秒后清屏",
      "P4 RAM 全 0，显示关闭",
      "P5 全亮页面，最低亮度",
      "P6 全亮页面，最高亮度",
      "P7 RAM 全亮，显示关闭",
      "P8 显示关闭并释放 GPIO",
      "P9 最低功耗后手动断开 ESP32 供电",
      "P10 软件最低功耗",
      "亮度增加一档",
      "亮度降低一档",
      "开启显示",
      "关闭显示",
      "ICON4 持续闪烁",
      "ICON4 常亮",
      "全部 ICON 持续闪烁",
      "全部 ICON 常亮",
      "当前全部显示内容闪烁",
      "停止内容闪烁并恢复显示",
  };

  if ((command >= 0) &&
      (command < (int)(sizeof(descriptions) / sizeof(descriptions[0]))))
  {
    return descriptions[command];
  }
  return NULL;
}

static void App_AIP1620PowerTest_PrintCommand(int command)
{
  const char *description = App_AIP1620PowerTest_GetCommandDescription(command);
  if (description != NULL)
  {
    MY_LOGI("收到命令 %d：%s", command, description);
  }
  else if ((command >= 31) && (command <= 35))
  {
    MY_LOGI("收到命令 %d：显示典型页面，亮度第 %d 档", command, command - 30);
  }
  else if ((command >= 41) && (command <= 45))
  {
    MY_LOGI("收到命令 %d：显示全亮页面，亮度第 %d 档", command, command - 40);
  }
  else if ((command >= 51) && (command <= 55))
  {
    MY_LOGI("收到命令 %d：显示空白页面，亮度第 %d 档", command, command - 50);
  }
  else if ((command >= 21) && (command <= 30))
  {
    MY_LOGW("收到命令 %d：该命令为预留项，当前尚未实现", command);
  }
}

static bool App_AIP1620PowerTest_ReadCommand(int *command)
{
  char input[8];
  size_t length = 0U;
  bool invalid = false;

  while (true)
  {
    int ch = getchar();
    if (ch == EOF)
    {
      vTaskDelay(pdMS_TO_TICKS(POWER_TEST_UART_RETRY_MS));
      continue;
    }
    if ((ch == '\r') || (ch == '\n'))
    {
      if ((length == 0U) && !invalid)
      {
        continue;
      }
      input[length] = '\0';
      break;
    }
    if ((ch >= '0') && (ch <= '9') && (length < (sizeof(input) - 1U)))
    {
      input[length++] = (char)ch;
    }
    else
    {
      invalid = true;
    }
  }

  char *end = NULL;
  long value = strtol(input, &end, 10);
  if (invalid || (end == input) || (*end != '\0') || (value < 0L) ||
      (value > 55L))
  {
    return false;
  }
  *command = (int)value;
  return true;
}

static esp_err_t App_AIP1620PowerTest_EnsureDisplay(void)
{
  if (s_display_initialized)
  {
    return ESP_OK;
  }

  MY_LOGI("AiP1620 初始化开始");
  esp_err_t ret = App_DisplayAIP1620_Init();
  if (ret == ESP_OK)
  {
    s_display_initialized = true;
    s_brightness_index = 2U;
    MY_LOGI("AiP1620 初始化完成");
  }
  return ret;
}

static esp_err_t App_AIP1620PowerTest_StopDisplay(void)
{
  if (!s_display_initialized)
  {
    return ESP_OK;
  }

  esp_err_t ret = App_DisplayAIP1620_Deinit();
  if (ret == ESP_OK)
  {
    s_display_initialized = false;
    s_icon_mode = POWER_TEST_ICON_MODE_NONE;
  }
  return ret;
}

static esp_err_t App_AIP1620PowerTest_SetBrightness(uint8_t index)
{
  if (index >= (sizeof(s_brightness_levels) / sizeof(s_brightness_levels[0])))
  {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t ret = App_DisplayAIP1620_SetBrightness(s_brightness_levels[index]);
  if (ret == ESP_OK)
  {
    s_brightness_index = index;
  }
  return ret;
}

static esp_err_t App_AIP1620PowerTest_AdjustBrightness(bool increase)
{
  if (increase)
  {
    if ((s_brightness_index + 1U) >=
        (sizeof(s_brightness_levels) / sizeof(s_brightness_levels[0])))
    {
      MY_LOGI("已经是最高亮度");
      return ESP_OK;
    }
    ++s_brightness_index;
  }
  else
  {
    if (s_brightness_index == 0U)
    {
      MY_LOGI("已经是最低亮度");
      return ESP_OK;
    }
    --s_brightness_index;
  }

  MY_LOGI("当前亮度：第 %u 档", (unsigned int)(s_brightness_index + 1U));
  return App_AIP1620PowerTest_SetBrightness(s_brightness_index);
}

static esp_err_t App_AIP1620PowerTest_ShowTypical(uint8_t brightness_index)
{
  esp_err_t ret = App_DisplayAIP1620_ShowNumber(1234U, true);
  if (ret == ESP_OK)
  {
    ret = App_DisplayAIP1620_SetColon(true);
  }
  if (ret == ESP_OK)
  {
    ret = App_DisplayAIP1620_SetIcons(APP_DISPLAY_AIP1620_ICON_ALL);
  }
  if (ret == ESP_OK)
  {
    ret = App_AIP1620PowerTest_SetBrightness(brightness_index);
  }
  if (ret == ESP_OK)
  {
    ret = App_DisplayAIP1620_SetEnabled(true);
  }
  return ret;
}

static esp_err_t App_AIP1620PowerTest_ShowAll(uint8_t brightness_index)
{
  esp_err_t ret = App_DisplayAIP1620_ShowNumber(8888U, true);
  if (ret == ESP_OK)
  {
    ret = App_DisplayAIP1620_SetColon(true);
  }
  if (ret == ESP_OK)
  {
    ret = App_DisplayAIP1620_SetIcons(APP_DISPLAY_AIP1620_ICON_ALL);
  }
  if (ret == ESP_OK)
  {
    ret = App_AIP1620PowerTest_SetBrightness(brightness_index);
  }
  if (ret == ESP_OK)
  {
    ret = App_DisplayAIP1620_SetEnabled(true);
  }
  return ret;
}

static esp_err_t App_AIP1620PowerTest_LoadAllWhileDisabled(void)
{
  /* 队列按发送顺序处理：先关闭显示，再更新 RAM，避免全亮瞬闪。 */
  esp_err_t ret = App_DisplayAIP1620_SetEnabled(false);
  if (ret == ESP_OK)
  {
    ret = App_DisplayAIP1620_ShowNumber(8888U, true);
  }
  if (ret == ESP_OK)
  {
    ret = App_DisplayAIP1620_SetColon(true);
  }
  if (ret == ESP_OK)
  {
    ret = App_DisplayAIP1620_SetIcons(APP_DISPLAY_AIP1620_ICON_ALL);
  }
  return ret;
}

static esp_err_t App_AIP1620PowerTest_ShowBlank(uint8_t brightness_index)
{
  esp_err_t ret = App_DisplayAIP1620_Clear();
  if (ret == ESP_OK)
  {
    ret = App_AIP1620PowerTest_SetBrightness(brightness_index);
  }
  if (ret == ESP_OK)
  {
    ret = App_DisplayAIP1620_SetEnabled(true);
  }
  return ret;
}

static esp_err_t App_AIP1620PowerTest_ExitIconMode(void)
{
  if (s_icon_mode == POWER_TEST_ICON_MODE_NONE)
  {
    return ESP_OK;
  }

  esp_err_t ret = App_DisplayAIP1620_StopBlink();
  if (ret == ESP_OK)
  {
    ret = App_DisplayAIP1620_SetIcons(APP_DISPLAY_AIP1620_ICON_NONE);
  }
  if (ret == ESP_OK)
  {
    s_icon_mode = POWER_TEST_ICON_MODE_NONE;
  }
  return ret;
}

static esp_err_t App_AIP1620PowerTest_EnterIconMode(int command)
{
  power_test_icon_mode_t mode = (power_test_icon_mode_t)(command - 14);
  if (s_icon_mode == mode)
  {
    return ESP_OK;
  }

  esp_err_t ret = App_AIP1620PowerTest_ExitIconMode();
  if (ret != ESP_OK)
  {
    return ret;
  }

  bool all_icons = (command == 17) || (command == 18);
  bool blink = (command == 15) || (command == 17);
  uint8_t icons =
      all_icons ? APP_DISPLAY_AIP1620_ICON_ALL : APP_DISPLAY_AIP1620_ICON_4;
  ret = App_DisplayAIP1620_SetIcons(icons);
  if (ret == ESP_OK && blink)
  {
    uint16_t blink_mask = all_icons ? (APP_DISPLAY_AIP1620_BLINK_ICON_1 |
                                       APP_DISPLAY_AIP1620_BLINK_ICON_2 |
                                       APP_DISPLAY_AIP1620_BLINK_ICON_3 |
                                       APP_DISPLAY_AIP1620_BLINK_ICON_4)
                                    : APP_DISPLAY_AIP1620_BLINK_ICON_4;
    ret =
        App_DisplayAIP1620_StartBlink(blink_mask, POWER_TEST_BLINK_INTERVAL_MS);
  }
  if (ret == ESP_OK)
  {
    s_icon_mode = mode;
  }
  return ret;
}

static esp_err_t App_AIP1620PowerTest_Execute(int command)
{
  bool keep_icon_mode = ((command >= 15) && (command <= 18)) ||
                        (command == 11) || (command == 12);
  esp_err_t ret = ESP_OK;
  if (!keep_icon_mode)
  {
    ret = App_AIP1620PowerTest_ExitIconMode();
    if (ret != ESP_OK)
    {
      return ret;
    }
  }

  if (((command >= 2) && (command <= 7)) ||
      ((command >= 11) && (command <= 20)) ||
      ((command >= 31) && (command <= 35)) ||
      ((command >= 41) && (command <= 45)) ||
      ((command >= 51) && (command <= 55)))
  {
    ret = App_AIP1620PowerTest_EnsureDisplay();
    if (ret != ESP_OK)
    {
      return ret;
    }
  }

  switch (command)
  {
    case 0:
      MY_LOGI("P0：保持 AiP1620 外部供电，断开 ESP32 后记录 V/I/P");
      break;
    case 1:
      MY_LOGI("P1：上电未初始化状态，记录 V/I/P");
      break;
    case 2:
      ret = App_AIP1620PowerTest_ShowBlank(s_brightness_index);
      break;
    case 3:
      ret = App_AIP1620PowerTest_ShowAll(4U);
      if (ret == ESP_OK)
      {
        vTaskDelay(pdMS_TO_TICKS(POWER_TEST_CLEAR_PREPARE_MS));
        ret = App_DisplayAIP1620_Clear();
      }
      break;
    case 4:
      ret = App_DisplayAIP1620_Clear();
      if (ret == ESP_OK)
      {
        ret = App_DisplayAIP1620_SetEnabled(false);
      }
      break;
    case 5:
      ret = App_AIP1620PowerTest_ShowAll(0U);
      break;
    case 6:
      ret = App_AIP1620PowerTest_ShowAll(4U);
      break;
    case 7:
      ret = App_AIP1620PowerTest_LoadAllWhileDisabled();
      break;
    case 8:
      ret = App_AIP1620PowerTest_StopDisplay();
      break;
    case 9:
    case 10:
      if (s_display_initialized)
      {
        ret = App_DisplayAIP1620_Clear();
        if (ret == ESP_OK)
        {
          ret = App_AIP1620PowerTest_StopDisplay();
        }
      }
      MY_LOGI("P%d：RAM 清空、显示关闭、GPIO 高阻", command);
      break;
    case 11:
      ret = App_AIP1620PowerTest_AdjustBrightness(true);
      break;
    case 12:
      ret = App_AIP1620PowerTest_AdjustBrightness(false);
      break;
    case 13:
      ret = App_DisplayAIP1620_SetEnabled(true);
      break;
    case 14:
      ret = App_DisplayAIP1620_SetEnabled(false);
      break;
    case 15:
    case 16:
    case 17:
    case 18:
      ret = App_AIP1620PowerTest_EnterIconMode(command);
      break;
    case 19:
      ret = App_DisplayAIP1620_StartBlink(APP_DISPLAY_AIP1620_BLINK_ALL,
                                          POWER_TEST_BLINK_INTERVAL_MS);
      break;
    case 20:
      ret = App_DisplayAIP1620_StopBlink();
      break;
    default:
      if ((command >= 31) && (command <= 35))
      {
        ret = App_AIP1620PowerTest_ShowTypical((uint8_t)(command - 31));
      }
      else if ((command >= 41) && (command <= 45))
      {
        ret = App_AIP1620PowerTest_ShowAll((uint8_t)(command - 41));
      }
      else if ((command >= 51) && (command <= 55))
      {
        ret = App_AIP1620PowerTest_ShowBlank((uint8_t)(command - 51));
      }
      else
      {
        ret = ((command >= 21) && (command <= 30)) ? ESP_ERR_NOT_SUPPORTED
                                                   : ESP_ERR_INVALID_ARG;
      }
      break;
  }
  return ret;
}

static void App_AIP1620PowerTest_Task(void *argument)
{
  (void)argument;
  App_AIP1620PowerTest_PrintMenu();

  while (true)
  {
    int command;
    if (!App_AIP1620PowerTest_ReadCommand(&command))
    {
      MY_LOGI("输入无效，请输入菜单编号并回车");
      MY_LOGI("请输入菜单编号并回车：");
      continue;
    }

    App_AIP1620PowerTest_PrintCommand(command);
    esp_err_t ret = App_AIP1620PowerTest_Execute(command);
    if (ret == ESP_OK)
    {
      MY_LOGI("命令 %d 执行完成", command);
    }
    else if (ret == ESP_ERR_NOT_SUPPORTED)
    {
      MY_LOGW("命令 %d 尚未实现，请选择其他菜单项", command);
    }
    else
    {
      MY_LOGE("功耗测试命令 %d 执行失败：%s", command, esp_err_to_name(ret));
    }
    MY_LOGI("请输入菜单编号并回车：");
  }
}

esp_err_t App_AIP1620PowerTest_Start(void)
{
  if (s_power_test_task != NULL)
  {
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t ret = App_AIP1620PowerTest_UartInit();
  if (ret != ESP_OK)
  {
    return ret;
  }

  BaseType_t result = xTaskCreate(App_AIP1620PowerTest_Task, "aip1620_test",
                                  POWER_TEST_TASK_STACK_SIZE, NULL,
                                  POWER_TEST_TASK_PRIORITY, &s_power_test_task);
  return (result == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

#else

esp_err_t App_AIP1620PowerTest_Start(void)
{
  return ESP_ERR_NOT_SUPPORTED;
}

#endif /* CONFIG_AIP1620_POWER_TEST */
