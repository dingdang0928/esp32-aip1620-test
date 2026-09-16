#include "App_ClockDisplay.h"
#include "App_AIP1620PowerTest.h"
#include "App_SleepRecord.h"
#include "App_SleepRecordUartTest.h"
#include "Dri_BLE.h"
#include "esp_err.h"
#include "sdkconfig.h"
#include "Inf_RGB.h"

void app_main(void)
{
  inf_rgb_init();  // 放着自己看的,装饰用
  ESP_ERROR_CHECK(App_SleepRecord_Init());
  ESP_ERROR_CHECK(App_ClockDisplay_Init());
  Dri_BLE_Init();

#if CONFIG_AIP1620_POWER_TEST
  // 测试AIP1620功耗模式
  ESP_ERROR_CHECK(App_AIP1620PowerTest_Start());
#endif

#if CONFIG_SLEEP_RECORD_UART_TEST
  ESP_ERROR_CHECK(App_SleepRecordUartTest_Start());
#endif

  /*
   * 正式产品收到云端 UTC 毫秒时间戳和 POSIX 时区后，调用
   * Inf_RTC_SyncFromCloud()。时钟显示任务会自动读取并刷新本地时间。
   * 显示刷新由 App_ClockDisplay 内部任务负责，app_main 无需轮询。
   */
}
