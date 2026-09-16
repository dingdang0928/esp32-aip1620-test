/**
 * @file Inf_SleepStorage.h
 * @brief 睡眠记录持久化存储接口。
 *
 * 本模块属于 INF 层，只负责睡眠记录的可靠存储、顺序读取、原位更新和
 * 循环覆盖，不负责睡眠业务状态机。记录负载以不透明字节流保存，其内容
 * 及版本迁移由 APP 层负责，因此本模块不理解开始时间、结束时间、同步
 * 状态等业务字段。
 *
 * 存储容量由 INF_SLEEP_STORAGE_CAPACITY 定义。容量已满时，追加新记录会覆盖最早生成的记录，
 * 不会以“容量已满”为由拒绝新记录。记录顺序由 INF 层分配的单调递增
 * record_id 确定，不依赖可能被重新校准的 RTC 墙钟时间。
 *
 * @note 所有接口均为同步接口，不创建任务、不持有业务消息队列。
 * @note 本模块不是多调用者并发接口，统一由 App_SleepRecord 任务调用。
 * @note 所有接口均不得在中断上下文中调用。
 * @note 当前接口只管理已经完成的睡眠记录。进行中记录是否保存检查点，
 *       待 GAP-29 确认后再独立设计，检查点不得占用完成记录配额。
 */

#ifndef PROJECT_NAME_INF_SLEEP_STORAGE_H
#define PROJECT_NAME_INF_SLEEP_STORAGE_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** @brief 睡眠记录最大保存数量。 */
/** 测试阶段使用 5 条；正式发布前改回 50U。 */
#define INF_SLEEP_STORAGE_CAPACITY 5U

/**
 * @brief 单条记录允许保存的最大业务负载长度，单位为字节。
 *
 * INF 层不会解释负载内容。该限制用于约束 Flash 占用并为调用者提供固定
 * 缓冲区上限；后续增加 APP 层字段时，可在不修改接口的情况下调整此值。
 */
#define INF_SLEEP_STORAGE_MAX_PAYLOAD_SIZE 64U

/** @brief 表示当前没有有效记录编号。有效 record_id 从 1 开始。 */
#define INF_SLEEP_STORAGE_INVALID_RECORD_ID UINT32_C(0)

  /**
   * @brief 一条持久化记录的公共信息。
   *
   * schema_version 用于帮助 APP 层选择正确的反序列化方式，INF 层不会根据
   * schema_version 解释或修改负载内容。负载长度由读取接口的 actual_length
   * 参数返回，不在本结构体中重复保存。
   */
  typedef struct
  {
    uint32_t record_id;     /**< INF 层分配的单调递增记录编号。 */
    uint8_t schema_version; /**< APP 层负载格式版本。 */
  } inf_sleep_storage_record_info_t;

  /**
   * @brief 初始化睡眠记录持久化存储。
   *
   * 初始化 cfg NVS 分区后，扫描睡眠记录专用命名空间，校验元数据和记录
   * 完整性，并在元数据异常时尝试根据有效记录恢复顺序。本函数不长期持有
   * NVS 句柄，也不得因为初始化或校验失败而自动擦除整个 NVS 分区。
   *
   * @retval ESP_OK 初始化成功。
   * @retval ESP_ERR_INVALID_STATE 模块已经初始化。
   * @return 其他值表示底层持久化存储初始化、打开或校验失败。
   */
  esp_err_t Inf_SleepStorage_Init(void);

  /**
   * @brief 追加一条已经完成的睡眠记录。
   *
   * INF 层为新记录分配 record_id，并将 schema_version 与不透明业务负载
   * 一起持久化。存储未满时写入下一个空闲槽；存储已满时覆盖最早记录，
   * 然后将最早记录位置向后移动一格。
   *
   * @param[in] schema_version APP 层负载格式版本。
   * @param[in] payload 指向待保存业务负载的指针，不能为 NULL。
   * @param[in] payload_length 负载长度，范围为
   *                           1~INF_SLEEP_STORAGE_MAX_PAYLOAD_SIZE。
   * @param[out] new_record_id 保存新分配记录编号的指针；不需要时可为 NULL。
   * @param[out] overwritten_record_id 保存被覆盖记录编号的指针；不需要时可为
   *                                    NULL。未发生覆盖时写入
   *                                    INF_SLEEP_STORAGE_INVALID_RECORD_ID。
   *
   * @retval ESP_OK 记录保存成功。
   * @retval ESP_ERR_INVALID_ARG payload 为 NULL 或负载长度不合法。
   * @retval ESP_ERR_INVALID_STATE 模块尚未初始化。
   * @return 其他值表示记录写入或提交失败。
   */
  esp_err_t Inf_SleepStorage_Append(uint8_t schema_version, const void *payload,
                                    size_t payload_length,
                                    uint32_t *new_record_id,
                                    uint32_t *overwritten_record_id);

  /**
   * @brief 按生成顺序读取一条睡眠记录。
   *
   * index 为逻辑索引，不是 Flash 物理槽号。index=0 表示当前最早记录，
   * index=count-1 表示当前最新记录。
   *
   * @param[in] index 要读取记录的逻辑索引。
   * @param[out] info 保存记录公共信息的指针，不能为 NULL。
   * @param[out] payload 保存业务负载的缓冲区，不能为 NULL。
   * @param[in] payload_capacity payload 缓冲区容量，单位为字节。
   * @param[out] actual_length 保存实际负载长度的指针，不能为 NULL。当缓冲区
   *                           不足时，也应返回所需长度。
   *
   * @retval ESP_OK 读取成功。
   * @retval ESP_ERR_INVALID_ARG 输出参数为 NULL。
   * @retval ESP_ERR_INVALID_SIZE payload 缓冲区容量不足。
   * @retval ESP_ERR_NOT_FOUND index 超出当前有效记录范围。
   * @retval ESP_ERR_INVALID_STATE 模块尚未初始化。
   * @return 其他值表示底层读取或完整性校验失败。
   */
  esp_err_t Inf_SleepStorage_ReadByIndex(uint8_t index,
                                         inf_sleep_storage_record_info_t *info,
                                         void *payload, size_t payload_capacity,
                                         size_t *actual_length);

  /**
   * @brief 根据 record_id 查找并读取一条睡眠记录。
   *
   * @param[in] record_id 目标记录编号，不能为
   *                      INF_SLEEP_STORAGE_INVALID_RECORD_ID。
   * @param[out] info 保存记录公共信息的指针，不能为 NULL。
   * @param[out] payload 保存业务负载的缓冲区，不能为 NULL。
   * @param[in] payload_capacity payload 缓冲区容量，单位为字节。
   * @param[out] actual_length 保存实际负载长度的指针，不能为 NULL。
   *
   * @retval ESP_OK 读取成功。
   * @retval ESP_ERR_INVALID_ARG 参数不合法。
   * @retval ESP_ERR_INVALID_SIZE payload 缓冲区容量不足。
   * @retval ESP_ERR_NOT_FOUND 未找到指定 record_id。
   * @retval ESP_ERR_INVALID_STATE 模块尚未初始化。
   * @return 其他值表示底层读取或完整性校验失败。
   */
  esp_err_t Inf_SleepStorage_ReadById(uint32_t record_id,
                                      inf_sleep_storage_record_info_t *info,
                                      void *payload, size_t payload_capacity,
                                      size_t *actual_length);

  /**
   * @brief 原位更新指定记录的APP层负载。
   *
   * 本函数保持 record_id 和记录顺序不变，可供 APP 层更新已经持久化的同步
   * 标记或进行负载格式迁移。INF 层不会检查新旧负载的业务含义。
   *
   * @param[in] record_id 目标记录编号。
   * @param[in] schema_version 新的APP层负载格式版本。
   * @param[in] payload 指向新业务负载的指针，不能为 NULL。
   * @param[in] payload_length 负载长度，范围为
   *                           1~INF_SLEEP_STORAGE_MAX_PAYLOAD_SIZE。
   *
   * @retval ESP_OK 更新成功。
   * @retval ESP_ERR_INVALID_ARG 参数不合法。
   * @retval ESP_ERR_NOT_FOUND 未找到指定 record_id。
   * @retval ESP_ERR_INVALID_STATE 模块尚未初始化。
   * @return 其他值表示记录写入或提交失败。
   */
  esp_err_t Inf_SleepStorage_UpdateById(uint32_t record_id,
                                        uint8_t schema_version,
                                        const void *payload,
                                        size_t payload_length);

  /**
   * @brief 获取当前有效睡眠记录数量。
   *
   * 存储容量固定为 INF_SLEEP_STORAGE_CAPACITY，无需通过运行期结构体重复
   * 返回。最早和最新记录可分别通过逻辑索引 0 和 count-1 读取。
   *
   * @param[out] count 保存当前有效记录数量，范围为
   *                   0~INF_SLEEP_STORAGE_CAPACITY，不能为 NULL。
   *
   * @retval ESP_OK 获取数量成功。
   * @retval ESP_ERR_INVALID_ARG count 为 NULL。
   * @retval ESP_ERR_INVALID_STATE 模块尚未初始化。
   */
  esp_err_t Inf_SleepStorage_GetCount(uint8_t *count);

  /**
   * @brief 清除全部已完成睡眠记录。
   *
   * 本函数只清除睡眠记录专用命名空间，不得擦除整个 cfg 分区，也不得删除
   * 闹钟、DIY、网络凭据或其他模块的数据。主要供恢复出厂流程调用。
   *
   * @retval ESP_OK 清除成功。
   * @retval ESP_ERR_INVALID_STATE 模块尚未初始化。
   * @return 其他值表示擦除或提交失败。
   */
  esp_err_t Inf_SleepStorage_ClearAll(void);

#ifdef __cplusplus
}
#endif

#endif /* PROJECT_NAME_INF_SLEEP_STORAGE_H */
