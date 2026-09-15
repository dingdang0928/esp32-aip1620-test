/**
 * @file Dri_NVS.c
 * @brief NVS 非易失性存储驱动实现。
 */

#include "Dri_NVS.h"

#include <stdbool.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

/** @brief 当前工程用于保存产品配置和业务记录的 NVS 分区名称。 */
#define DRI_NVS_PARTITION_NAME "cfg"

static const char *TAG = "Dri_NVS";

/** @brief 标记 cfg 分区是否已经由本驱动成功初始化。 */
static bool s_nvs_initialized = false;

/**
 * @brief 将 NVS 专用错误码转换为上层可直接使用的通用错误码。
 *
 * DRI 层不向 INF 层暴露 nvs.h，因此需要转换上层必须判断的错误。其他底层
 * 故障保留原始错误码，便于日志定位具体原因。
 *
 * @param[in] err ESP-IDF NVS 接口返回的错误码。
 *
 * @return 转换后的工程错误码。
 */
static esp_err_t Dri_NVS_ConvertError(esp_err_t err)
{
  switch (err)
  {
    case ESP_ERR_NVS_NOT_FOUND:
      return ESP_ERR_NOT_FOUND;

    case ESP_ERR_NVS_INVALID_LENGTH:
      return ESP_ERR_INVALID_SIZE;

    case ESP_ERR_NVS_INVALID_NAME:
      return ESP_ERR_INVALID_ARG;

    case ESP_ERR_NVS_NOT_INITIALIZED:
      return ESP_ERR_INVALID_STATE;

    default:
      return err;
  }
}

/**
 * @brief 检查 NVS 名称是否合法。
 *
 * ESP-IDF 要求命名空间和键名长度均不超过 15 个字符。max_size 包含字符串
 * 结束符，因此合法字符串长度必须小于 max_size。
 *
 * @param[in] name 待检查的名称。
 * @param[in] max_size ESP-IDF 规定的名称缓冲区最大长度。
 *
 * @return true 名称合法。
 * @return false 名称为空或长度超限。
 */
static bool Dri_NVS_IsNameValid(const char *name, size_t max_size)
{
  if (name == NULL)
  {
    return false;
  }

  size_t name_length = strlen(name);

  return ((name_length > 0U) && (name_length < max_size));
}

/**
 * @brief 打开 cfg 分区中的指定命名空间。
 *
 * @param[in] namespace_name 命名空间名称。
 * @param[in] open_mode 打开模式。
 * @param[out] handle 返回的 NVS 句柄。
 *
 * @return ESP-IDF NVS 接口返回的错误码。
 */
static esp_err_t Dri_NVS_Open(const char *namespace_name,
                              nvs_open_mode_t open_mode, nvs_handle_t *handle)
{
  if (!s_nvs_initialized)
  {
    return ESP_ERR_INVALID_STATE;
  }

  if ((!Dri_NVS_IsNameValid(namespace_name, NVS_NS_NAME_MAX_SIZE)) ||
      (handle == NULL))
  {
    return ESP_ERR_INVALID_ARG;
  }

  return nvs_open_from_partition(DRI_NVS_PARTITION_NAME, namespace_name,
                                 open_mode, handle);
}

esp_err_t Dri_NVS_Init(void)
{
  if (s_nvs_initialized)
  {
    return ESP_OK;
  }

  esp_err_t err = nvs_flash_init_partition(DRI_NVS_PARTITION_NAME);

  if (err != ESP_OK)
  {
    /*
     * 产品数据位于 cfg 分区，初始化失败时不能照搬 Demo 自动擦除分区，
     * 应将错误交给上层决定恢复策略。
     */
    ESP_LOGE(TAG, "cfg NVS 分区初始化失败：%s", esp_err_to_name(err));
    return err;
  }

  s_nvs_initialized = true;
  ESP_LOGI(TAG, "cfg NVS 分区初始化成功");

  return ESP_OK;
}

esp_err_t Dri_NVS_ReadBlob(const char *namespace_name, const char *key,
                           void *data, size_t *length)
{
  if ((!Dri_NVS_IsNameValid(key, NVS_KEY_NAME_MAX_SIZE)) || (length == NULL))
  {
    return ESP_ERR_INVALID_ARG;
  }

  nvs_handle_t handle = 0;
  esp_err_t err = Dri_NVS_Open(namespace_name, NVS_READONLY, &handle);

  if (err != ESP_OK)
  {
    return Dri_NVS_ConvertError(err);
  }

  err = nvs_get_blob(handle, key, data, length);
  nvs_close(handle);

  return Dri_NVS_ConvertError(err);
}

esp_err_t Dri_NVS_WriteBlob(const char *namespace_name, const char *key,
                            const void *data, size_t length)
{
  if ((!Dri_NVS_IsNameValid(key, NVS_KEY_NAME_MAX_SIZE)) || (data == NULL) ||
      (length == 0U))
  {
    return ESP_ERR_INVALID_ARG;
  }

  nvs_handle_t handle = 0;
  esp_err_t err = Dri_NVS_Open(namespace_name, NVS_READWRITE, &handle);

  if (err != ESP_OK)
  {
    return Dri_NVS_ConvertError(err);
  }

  err = nvs_set_blob(handle, key, data, length);

  if (err == ESP_OK)
  {
    /* nvs_set_blob() 只更新缓存，提交成功后才能保证数据持久化。 */
    err = nvs_commit(handle);
  }

  nvs_close(handle);

  return Dri_NVS_ConvertError(err);
}

esp_err_t Dri_NVS_EraseKey(const char *namespace_name, const char *key)
{
  if (!Dri_NVS_IsNameValid(key, NVS_KEY_NAME_MAX_SIZE))
  {
    return ESP_ERR_INVALID_ARG;
  }

  nvs_handle_t handle = 0;
  esp_err_t err = Dri_NVS_Open(namespace_name, NVS_READWRITE, &handle);

  if (err != ESP_OK)
  {
    return Dri_NVS_ConvertError(err);
  }

  err = nvs_erase_key(handle, key);

  if (err == ESP_OK)
  {
    err = nvs_commit(handle);
  }

  nvs_close(handle);

  return Dri_NVS_ConvertError(err);
}

esp_err_t Dri_NVS_EraseNamespace(const char *namespace_name)
{
  nvs_handle_t handle = 0;
  esp_err_t err = Dri_NVS_Open(namespace_name, NVS_READONLY, &handle);

  if (err == ESP_ERR_NVS_NOT_FOUND)
  {
    /* 清除不存在的命名空间视为成功，使恢复出厂操作可以安全重复执行。 */
    return ESP_OK;
  }

  if (err != ESP_OK)
  {
    return Dri_NVS_ConvertError(err);
  }

  nvs_close(handle);
  handle = 0;

  err = Dri_NVS_Open(namespace_name, NVS_READWRITE, &handle);

  if (err != ESP_OK)
  {
    return Dri_NVS_ConvertError(err);
  }

  err = nvs_erase_all(handle);

  if (err == ESP_OK)
  {
    err = nvs_commit(handle);
  }

  nvs_close(handle);

  return Dri_NVS_ConvertError(err);
}
