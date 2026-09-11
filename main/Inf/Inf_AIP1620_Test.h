#ifndef PROJECT_NAME_INF_AIP1620_TEST_H
#define PROJECT_NAME_INF_AIP1620_TEST_H

#include <stdint.h>
#include <stdio.h>

#include "esp_err.h"
#include "ff.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 这些接口只用于研发和功耗测试，不属于正式产品 API。
 * 使用前需要在 menuconfig 中开启 CONFIG_AIP1620_POWER_TEST。
 */
esp_err_t Inf_AIP_1620_Power_Test(void);
void Inf_AIP_1620_Display_All(void);
void Inf_AIP_1620_Display_Digit(uint8_t position, uint8_t digit);
void Inf_AIP_1620_Set_Brightness(uint8_t raw_brightness);
esp_err_t Inf_AIP_1620_Low_Battery_Warning_Enable(void);
void Inf_AIP_1620_Low_Battery_Warning_Disable(void);

#ifdef __cplusplus
}
#endif

#endif /* PROJECT_NAME_INF_AIP1620_TEST_H */
