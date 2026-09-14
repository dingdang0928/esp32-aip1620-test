#ifndef PROJECT_NAME_APP_AIP1620_POWER_TEST_H
#define PROJECT_NAME_APP_AIP1620_POWER_TEST_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

  /** 启动仅供研发使用的 AiP1620 串口功耗测试任务。 */
  esp_err_t App_AIP1620PowerTest_Start(void);

#ifdef __cplusplus
}
#endif

#endif /* PROJECT_NAME_APP_AIP1620_POWER_TEST_H */
