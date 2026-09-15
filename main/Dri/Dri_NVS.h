/**
 * @file Dri_NVS.h
 * @brief NVS 非易失性存储驱动接口。
 *
 * 本模块属于 DRI 层，负责对 ESP-IDF NVS 接口进行简单封装。上层只需要
 * 指定命名空间和键名，不需要管理 NVS 句柄，也不需要单独执行提交操作。
 *
 * 当前工程使用分区表中的 cfg 分区。每次读写操作均在驱动内部完成命名
 * 空间的打开和关闭；写入及删除操作成功后，由驱动内部执行提交。
 *
 * @note 接口均为同步接口，只能在任务上下文中调用，不能在中断中调用。
 * @note 初始化失败时不自动擦除 cfg 分区，防止误删产品数据。
 */

#ifndef DRI_NVS_H
#define DRI_NVS_H

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

  /**
   * @brief 初始化 cfg NVS 分区。
   *
   * 系统启动时调用一次，运行期间不需要反初始化。重复调用时应直接返回
   * ESP_OK。初始化失败时只返回错误，不自动擦除分区。
   *
   * @return
   *      - ESP_OK：初始化成功或已经初始化
   *      - ESP_ERR_NOT_FOUND：分区表中不存在 cfg 分区
   *      - 其他错误：底层 NVS 初始化失败
   */
  esp_err_t Dri_NVS_Init(void);

  /**
   * @brief 从指定命名空间读取一项二进制数据。
   *
   * 当 data 为 NULL 时，仅查询数据长度，并通过 length 返回所需空间；当
   * data 不为 NULL 时，length 输入缓冲区容量，成功后返回实际数据长度。
   *
   * @param[in] namespace_name NVS 命名空间名称，长度为 1~15 个字符。
   * @param[in] key 键名，长度为 1~15 个字符。
   * @param[out] data 接收数据的缓冲区；仅查询长度时可以为 NULL。
   * @param[in,out] length 输入缓冲区容量，输出实际或所需数据长度。
   *
   * @return
   *      - ESP_OK：读取成功
   *      - ESP_ERR_INVALID_ARG：参数错误
   *      - ESP_ERR_INVALID_STATE：Dri_NVS_Init() 尚未成功执行
   *      - ESP_ERR_NOT_FOUND：命名空间或键不存在
   *      - ESP_ERR_INVALID_SIZE：接收缓冲区空间不足
   *      - 其他错误：底层 NVS 读取失败
   */
  esp_err_t Dri_NVS_ReadBlob(const char *namespace_name, const char *key,
                             void *data, size_t *length);

  /**
   * @brief 向指定命名空间写入一项二进制数据。
   *
   * 驱动内部依次完成打开命名空间、写入、提交和关闭。只有提交成功时本
   * 函数才返回 ESP_OK。
   *
   * @param[in] namespace_name NVS 命名空间名称，长度为 1~15 个字符。
   * @param[in] key 键名，长度为 1~15 个字符。
   * @param[in] data 待写入数据的地址，不能为 NULL。
   * @param[in] length 待写入数据的长度，必须大于 0。
   *
   * @return
   *      - ESP_OK：写入并提交成功
   *      - ESP_ERR_INVALID_ARG：参数错误
   *      - ESP_ERR_INVALID_STATE：Dri_NVS_Init() 尚未成功执行
   *      - 其他错误：底层 NVS 写入或提交失败
   */
  esp_err_t Dri_NVS_WriteBlob(const char *namespace_name, const char *key,
                              const void *data, size_t length);

  /**
   * @brief 删除指定命名空间中的一个键。
   *
   * 删除成功后由驱动内部提交，不影响同一命名空间中的其他键。
   *
   * @param[in] namespace_name NVS 命名空间名称，长度为 1~15 个字符。
   * @param[in] key 待删除的键名，长度为 1~15 个字符。
   *
   * @return
   *      - ESP_OK：删除并提交成功
   *      - ESP_ERR_INVALID_ARG：参数错误
   *      - ESP_ERR_INVALID_STATE：Dri_NVS_Init() 尚未成功执行
   *      - ESP_ERR_NOT_FOUND：命名空间或键不存在
   *      - 其他错误：底层 NVS 删除或提交失败
   */
  esp_err_t Dri_NVS_EraseKey(const char *namespace_name, const char *key);

  /**
   * @brief 清除指定命名空间中的全部键。
   *
   * 本函数只清除目标命名空间，不擦除整个 cfg 分区，也不影响其他模块的
   * 数据。清除成功后由驱动内部提交。命名空间不存在或本身为空时，也返回
   * ESP_OK，便于恢复出厂流程重复调用。
   *
   * @param[in] namespace_name NVS 命名空间名称，长度为 1~15 个字符。
   *
   * @return
   *      - ESP_OK：清除并提交成功，或命名空间原本不存在
   *      - ESP_ERR_INVALID_ARG：参数错误
   *      - ESP_ERR_INVALID_STATE：Dri_NVS_Init() 尚未成功执行
   *      - 其他错误：底层 NVS 清除或提交失败
   */
  esp_err_t Dri_NVS_EraseNamespace(const char *namespace_name);

#ifdef __cplusplus
}
#endif

#endif /* DRI_NVS_H */
