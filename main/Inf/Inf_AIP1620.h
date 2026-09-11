#ifndef PROJECT_NAME_INF_AIP1620_H
#define PROJECT_NAME_INF_AIP1620_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/** 亮度范围为 0~7。 */
// 客户要求5档亮度
#define INF_AIP1620_BRIGHTNESS_MAX 8U

/** 数码管共有 4 个数字位，位置从左到右为 0~3。 */
#define INF_AIP1620_DIGIT_COUNT 4U

/** GRID6 图标位使用 SEG1~SEG4，mask 的 bit0~bit3 分别控制对应图标。 */
#define INF_AIP1620_ICON_COUNT 4U
#define INF_AIP1620_ICON_4_MASK (1U << 3)
#define INF_AIP1620_ICON_ALL_MASK ((1U << INF_AIP1620_ICON_COUNT) - 1U)

/** 按手册流程初始化 GPIO、显示 RAM，并开启显示。 */
esp_err_t Inf_AIP_1620_Init(void);

/** 关闭显示、清空RAM,并将通信 GPIO 释放为高阻态。 */
esp_err_t Inf_AIP_1620_Deinit(void);

/** 进入串口功耗测试菜单，输入编号后执行对应工况。 */
esp_err_t Inf_AIP_1620_Power_Test(void);

/** 清空所有数字、冒号和图标。 */
// RAM清空,不关显示
void Inf_AIP_1620_ClearAll(void);

/** 点亮灯板上的所有灯，用于测试。 */
void Inf_AIP_1620_Display_All(void);

/** 开启显示。 */
void Inf_AIP_1620_Display_Enable(void);

/** 关闭显示。 */
void Inf_AIP_1620_Display_Disable(void);

/** 设置亮度，超出范围时按最高亮度处理。 */
void Inf_AIP_1620_Set_Brightness(uint8_t brightness);

/** 在指定位置显示 0~9；参数无效时不处理。 */
void Inf_AIP_1620_Display_Digit(uint8_t position, uint8_t digit);

/** 显示 0~9999，leading_zero 为 true 时补前导零。 */
void Inf_AIP_1620_Display_Number(uint16_t number, bool leading_zero);

/** 控制中间冒号。 */
void Inf_AIP_1620_Display_Mid_Dot(bool enable);

/** 控制 GRID6 上的 4 个图标；bit0~bit3 分别对应 SEG1~SEG4。 */
// 从上往下：bit0=图标1，bit1=图标2，bit2=图标3，bit3=图标4（最下方）。
void Inf_AIP_1620_Display_Icons(uint8_t icon_mask);

/** 启用低电量提醒：最下方 ICON4 按 1 秒周期持续闪烁。 */
esp_err_t Inf_AIP_1620_Low_Battery_Warning_Enable(void);

/** 退出低电量提醒并熄灭 ICON4。 */
void Inf_AIP_1620_Low_Battery_Warning_Disable(void);

#endif /* PROJECT_NAME_INF_AIP1620_H */
