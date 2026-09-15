#ifndef PROJECT_NAME_APP_SLEEP_RECORD_UART_TEST_H
#define PROJECT_NAME_APP_SLEEP_RECORD_UART_TEST_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** @brief 启动睡眠记录研发串口；正式固件关闭配置后返回 ESP_ERR_NOT_SUPPORTED。 */
esp_err_t App_SleepRecordUartTest_Start(void);

#ifdef __cplusplus
}
#endif

#endif /* PROJECT_NAME_APP_SLEEP_RECORD_UART_TEST_H */
