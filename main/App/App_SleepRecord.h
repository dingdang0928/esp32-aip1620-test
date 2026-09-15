/**
 * @file App_SleepRecord.h
 * @brief 睡眠记录业务状态机。
 *
 * 按键、手机 App 和研发串口均应调用本文件中的接口。调用方不得直接操作
 * Inf_SleepStorage，否则会绕过绑定、校时、最短时长和取消规则。
 */

#ifndef PROJECT_NAME_APP_SLEEP_RECORD_H
#define PROJECT_NAME_APP_SLEEP_RECORD_H

#include <stdbool.h>
#include <stdint.h>

#include "Inf_RTC.h"
#include "Inf_SleepStorage.h"
#include "esp_err.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** @brief 当前 APP 层睡眠记录负载格式版本。 */
#define APP_SLEEP_RECORD_SCHEMA_VERSION 1U

/** @brief 小于 10 分钟的记录不保存。 */
#define APP_SLEEP_RECORD_MIN_DURATION_MINUTES 10U

/** @brief 记录达到 12 小时时产生一次提醒。 */
#define APP_SLEEP_RECORD_REMINDER_MINUTES (12U * 60U)

/** @brief 记录达到 24 小时时自动取消。 */
#define APP_SLEEP_RECORD_AUTO_CANCEL_MINUTES (24U * 60U)

  typedef enum
  {
    APP_SLEEP_RECORD_EVENT_STARTED = 0,
    APP_SLEEP_RECORD_EVENT_SAVED,
    APP_SLEEP_RECORD_EVENT_DISCARDED_TOO_SHORT,
    APP_SLEEP_RECORD_EVENT_CANCELLED,
    APP_SLEEP_RECORD_EVENT_REMINDER_12H,
    APP_SLEEP_RECORD_EVENT_AUTO_CANCELLED_24H,
  } app_sleep_record_event_t;

  /** @brief APP 层对外返回的已完成睡眠记录。 */
  typedef struct
  {
    uint32_t record_id;
    uint32_t start_timestamp; /**< Unix 秒，精确到分钟，秒字段恒为 0。 */
    uint32_t end_timestamp;   /**< Unix 秒，精确到分钟，秒字段恒为 0。 */
  } app_sleep_record_t;

  /** @brief 睡眠记录状态快照。 */
  typedef struct
  {
    bool bound;
    bool time_valid;
    bool recording;
    bool reminder_12h_sent;
    uint32_t start_timestamp;
    uint32_t elapsed_seconds;
    uint8_t stored_count;
  } app_sleep_record_status_t;

  /**
   * @brief 初始化 RTC、睡眠存储和睡眠记录状态机。
   *
   * 本模块为产品全生命周期服务，不提供 Deinit。初始化完成后，按键任务、BLE/App
   * 任务和串口测试任务可并发调用本模块接口；接口内部会串行化状态和 NVS 操作。
   */
  esp_err_t App_SleepRecord_Init(void);

  /** @brief 更新设备绑定状态。解除绑定不会中断已经开始的记录。 */
  esp_err_t App_SleepRecord_SetBound(bool bound);

  /** @brief 设置/校准 RTC 时间。 */
  esp_err_t App_SleepRecord_SetTime(const inf_rtc_time_t *time);

  /**
   * @brief 开始睡眠记录。
   *
   * 仅在设备已绑定且 RTC 已完成有效校时时允许开始。
   */
  esp_err_t App_SleepRecord_Start(void);

  /**
   * @brief 正常结束睡眠记录。
   *
   * 不足 10 分钟时正常结束但不保存，返回 ESP_OK，并把 new_record_id 写为
   * INF_SLEEP_STORAGE_INVALID_RECORD_ID。达到 10 分钟时保存完成记录。
   */
  esp_err_t App_SleepRecord_Stop(uint32_t *new_record_id);

  /** @brief 取消进行中的记录；取消的数据不保存，也不占用 50 条配额。 */
  esp_err_t App_SleepRecord_Cancel(void);

  /** @brief 获取当前业务状态和已保存记录数量。 */
  esp_err_t App_SleepRecord_GetStatus(app_sleep_record_status_t *status);

  /** @brief 获取已完成记录数量。 */
  esp_err_t App_SleepRecord_GetCount(uint8_t *count);

  /** @brief 按时间顺序读取记录，index=0 为当前最早记录。 */
  esp_err_t App_SleepRecord_ReadByIndex(uint8_t index,
                                        app_sleep_record_t *record);

  /** @brief 按 record_id 读取记录。 */
  esp_err_t App_SleepRecord_ReadById(uint32_t record_id,
                                     app_sleep_record_t *record);

  /**
   * @brief 清除全部已完成记录，同时取消当前进行中的记录。
   *
   * 该接口预留给恢复出厂流程和研发测试，手机 App 普通业务不应直接开放此能力。
   */
  esp_err_t App_SleepRecord_ClearAll(void);

  /**
   * @brief 睡眠业务事件回调（弱实现）。
   *
   * 后续按键显示或 App 上传模块可提供同名强实现。回调不在内部互斥锁中执行，允许
   * 再次调用 App_SleepRecord 的查询接口。GAP-28 未明确前，本模块不自行定义上传协议。
   */
  void App_SleepRecord_OnEvent(app_sleep_record_event_t event,
                               uint32_t record_id,
                               uint32_t elapsed_minutes);

#if CONFIG_SLEEP_RECORD_UART_TEST
  /**
   * @brief 仅供串口测试加速时间，用于验证 10 分钟、12 小时和 24 小时边界。
   *
   * 该函数只调整当前记录的测试起点，不写 NVS，正式固件中不会编译。
   */
  esp_err_t App_SleepRecord_TestSetElapsedMinutes(uint32_t elapsed_minutes);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PROJECT_NAME_APP_SLEEP_RECORD_H */
