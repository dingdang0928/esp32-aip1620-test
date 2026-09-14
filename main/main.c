#include "App_ClockDisplay.h"
#include "Inf_RGB.h"
#include "esp_err.h"
#include "Inf_AIP1620_Test.h"

inf_rtc_time_t virtual_time;
void app_main(void)
{
  // ESP_ERROR_CHECK(inf_rgb_init());
  // inf_rgb_deinit();
  // ESP_ERROR_CHECK(App_ClockDisplay_Init());

  // 测试AIP1620功耗模式
  Inf_AIP_1620_Power_Test();

  /*
   * 正式产品在蓝牙配网、网络校时、按键设置或外部 RTC 读取成功后，
   * 调用 App_ClockDisplay_SetTime() 更新时间。
   * 显示刷新由 App_ClockDisplay 内部任务负责，app_main 无需轮询。
   */

  // 手动测试
  virtual_time.day = 11;
  virtual_time.month = 9;
  virtual_time.year = 2026;
  virtual_time.hour = 0;
  virtual_time.minute = 0;
  virtual_time.second = 0;
  // ESP_ERROR_CHECK(App_ClockDisplay_SetTime(&virtual_time));
}
