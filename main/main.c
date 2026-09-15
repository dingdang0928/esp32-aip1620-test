#include "App_ClockDisplay.h"
#include "App_AIP1620PowerTest.h"
#include "Dri_BLE.h"
#include "esp_err.h"
#include "sdkconfig.h"

void app_main(void)
{
  // Dri_BLE_Init();

#if CONFIG_AIP1620_POWER_TEST
  // 测试AIP1620功耗模式
  ESP_ERROR_CHECK(App_AIP1620PowerTest_Start());
#endif

  /*
   * 正式产品在蓝牙配网、网络校时、按键设置或外部 RTC 读取成功后，
   * 调用 App_ClockDisplay_SetTime() 更新时间。
   * 显示刷新由 App_ClockDisplay 内部任务负责，app_main 无需轮询。
   */
}
