/**
 * @file Inf_SleepStorage.c
 * @brief 睡眠记录持久化存储实现。
 *
 * 本模块使用 cfg NVS 分区中的独立命名空间保存固定数量的记录槽。每个槽均
 * 保存完整的记录头、APP 负载和 CRC32；启动时扫描所有槽并重建逻辑顺序，
 * 因此记录写入成功但元数据尚未提交时掉电，也可以在下次启动时恢复。
 *
 * 本文件不实现 10 分钟有效门限、12 小时提醒、24 小时作废、绑定校验、
 * 校时校验、云端同步或串口命令解析，这些均属于 APP 层职责。
 */

#include "Inf_SleepStorage.h"

#include <stdbool.h>
#include <string.h>

#include "Dri_NVS.h"
#include "esp_crc.h"
#include "esp_log.h"

/** @brief 睡眠记录专用 NVS 命名空间。 */
#define INF_SLEEP_STORAGE_NAMESPACE "sleep_store"

/** @brief 持久化元数据键名。 */
#define INF_SLEEP_STORAGE_METADATA_KEY "meta"

/** @brief 槽键名格式为 slot00~slot49，包含字符串结束符。 */
#define INF_SLEEP_STORAGE_SLOT_KEY_SIZE 7U

/** @brief 单条记录的固定头长度。 */
#define INF_SLEEP_STORAGE_RECORD_HEADER_SIZE 12U

/** @brief 单条记录尾部 CRC32 长度。 */
#define INF_SLEEP_STORAGE_CRC_SIZE 4U

/** @brief 单条记录 Blob 的最小长度。 */
#define INF_SLEEP_STORAGE_RECORD_MIN_SIZE                                      \
  (INF_SLEEP_STORAGE_RECORD_HEADER_SIZE + INF_SLEEP_STORAGE_CRC_SIZE)

/** @brief 单条记录 Blob 的最大长度。 */
#define INF_SLEEP_STORAGE_RECORD_MAX_SIZE                                      \
  (INF_SLEEP_STORAGE_RECORD_MIN_SIZE + INF_SLEEP_STORAGE_MAX_PAYLOAD_SIZE)

/** @brief 元数据固定长度。 */
#define INF_SLEEP_STORAGE_METADATA_SIZE 16U

/** @brief 元数据参与 CRC32 计算的长度。 */
#define INF_SLEEP_STORAGE_METADATA_CRC_OFFSET 12U

/** @brief 睡眠记录持久化格式版本。 */
#define INF_SLEEP_STORAGE_FORMAT_VERSION 1U

/** @brief 记录魔数，按小端保存后对应字符 SLPR。 */
#define INF_SLEEP_STORAGE_RECORD_MAGIC UINT32_C(0x52504C53)

/** @brief 元数据魔数，按小端保存后对应字符 SLPM。 */
#define INF_SLEEP_STORAGE_METADATA_MAGIC UINT32_C(0x4D504C53)

/** @brief 记录头字段偏移。 */
#define INF_SLEEP_STORAGE_RECORD_MAGIC_OFFSET 0U
#define INF_SLEEP_STORAGE_RECORD_ID_OFFSET 4U
#define INF_SLEEP_STORAGE_RECORD_LENGTH_OFFSET 8U
#define INF_SLEEP_STORAGE_RECORD_SCHEMA_OFFSET 9U
#define INF_SLEEP_STORAGE_RECORD_VERSION_OFFSET 10U
#define INF_SLEEP_STORAGE_RECORD_RESERVED_OFFSET 11U

/** @brief 元数据字段偏移。 */
#define INF_SLEEP_STORAGE_METADATA_MAGIC_OFFSET 0U
#define INF_SLEEP_STORAGE_METADATA_VERSION_OFFSET 4U
#define INF_SLEEP_STORAGE_METADATA_COUNT_OFFSET 5U
#define INF_SLEEP_STORAGE_METADATA_NEXT_SLOT_OFFSET 6U
#define INF_SLEEP_STORAGE_METADATA_RESERVED_OFFSET 7U
#define INF_SLEEP_STORAGE_METADATA_NEXT_ID_OFFSET 8U

static const char *TAG = "Inf_SleepStorage";

/** @brief 从 Flash 解码出的元数据，不直接以 C 结构体格式落盘。 */
typedef struct
{
  uint32_t next_record_id;
  uint8_t count;
  uint8_t next_slot;
} inf_sleep_storage_metadata_t;

/** @brief 模块是否已经成功初始化。 */
static bool s_initialized = false;

/** @brief 当前有效记录数。 */
static uint8_t s_count = 0U;

/** @brief 下次分配的记录编号；0 表示 uint32_t 编号已经耗尽。 */
static uint32_t s_next_record_id = 1U;

/** @brief 下次追加记录使用的物理槽号。 */
static uint8_t s_next_slot = 0U;

/** @brief 按物理槽保存有效 record_id；0 表示该槽无有效记录。 */
static uint32_t s_record_ids[INF_SLEEP_STORAGE_CAPACITY];

/** @brief 按 record_id 从小到大保存物理槽号。 */
static uint8_t s_slot_order[INF_SLEEP_STORAGE_CAPACITY];

_Static_assert(INF_SLEEP_STORAGE_CAPACITY <= UINT8_MAX,
               "sleep storage capacity must fit in uint8_t");
_Static_assert(INF_SLEEP_STORAGE_MAX_PAYLOAD_SIZE <= UINT8_MAX,
               "sleep payload length must fit in uint8_t");

/** @brief 以小端格式写入 uint32_t。 */
static void Inf_SleepStorage_PutU32(uint8_t *destination, uint32_t value)
{
  destination[0] = (uint8_t)(value & UINT32_C(0xFF));
  destination[1] = (uint8_t)((value >> 8U) & UINT32_C(0xFF));
  destination[2] = (uint8_t)((value >> 16U) & UINT32_C(0xFF));
  destination[3] = (uint8_t)((value >> 24U) & UINT32_C(0xFF));
}

/** @brief 从小端字节序读取 uint32_t。 */
static uint32_t Inf_SleepStorage_GetU32(const uint8_t *source)
{
  return ((uint32_t)source[0]) | ((uint32_t)source[1] << 8U) |
         ((uint32_t)source[2] << 16U) | ((uint32_t)source[3] << 24U);
}

/** @brief 计算持久化数据的 CRC32。 */
static uint32_t Inf_SleepStorage_CalculateCrc(const uint8_t *data,
                                              size_t length)
{
  return esp_crc32_le(0U, data, (uint32_t)length);
}

/** @brief 生成物理槽对应的 NVS 键名。 */
static void Inf_SleepStorage_MakeSlotKey(
    uint8_t slot, char key[INF_SLEEP_STORAGE_SLOT_KEY_SIZE])
{
  memcpy(key, "slot00", INF_SLEEP_STORAGE_SLOT_KEY_SIZE);
  key[4] = (char)('0' + (slot / 10U));
  key[5] = (char)('0' + (slot % 10U));
}

/** @brief 清空运行期索引并恢复初始状态。 */
static void Inf_SleepStorage_ResetRuntimeState(void)
{
  memset(s_record_ids, 0, sizeof(s_record_ids));
  memset(s_slot_order, 0, sizeof(s_slot_order));
  s_count = 0U;
  s_next_record_id = 1U;
  s_next_slot = 0U;
}

/** @brief 根据当前槽状态更新下次写入槽。 */
static void Inf_SleepStorage_UpdateNextSlot(void)
{
  if (s_count >= INF_SLEEP_STORAGE_CAPACITY)
  {
    s_next_slot = s_slot_order[0];
    return;
  }

  for (uint8_t slot = 0U; slot < INF_SLEEP_STORAGE_CAPACITY; ++slot)
  {
    if (s_record_ids[slot] == INF_SLEEP_STORAGE_INVALID_RECORD_ID)
    {
      s_next_slot = slot;
      return;
    }
  }

  /* 正常情况下不会到达此处，保留确定值便于故障定位。 */
  s_next_slot = 0U;
}

/**
 * @brief 按 record_id 顺序向运行期索引插入一个槽。
 *
 * @retval ESP_OK 插入成功。
 * @retval ESP_ERR_INVALID_STATE 记录编号重复或运行期索引已满。
 */
static esp_err_t Inf_SleepStorage_InsertOrderedSlot(uint8_t slot,
                                                    uint32_t record_id)
{
  if (s_count >= INF_SLEEP_STORAGE_CAPACITY)
  {
    return ESP_ERR_INVALID_STATE;
  }

  uint8_t insert_position = s_count;

  for (uint8_t index = 0U; index < s_count; ++index)
  {
    uint32_t existing_id = s_record_ids[s_slot_order[index]];

    if (existing_id == record_id)
    {
      return ESP_ERR_INVALID_STATE;
    }

    if ((insert_position == s_count) && (record_id < existing_id))
    {
      insert_position = index;
    }
  }

  for (uint8_t index = s_count; index > insert_position; --index)
  {
    s_slot_order[index] = s_slot_order[index - 1U];
  }

  s_record_ids[slot] = record_id;
  s_slot_order[insert_position] = slot;
  ++s_count;

  return ESP_OK;
}

/** @brief 判断记录读取错误是否表示单槽内容无效。 */
static bool Inf_SleepStorage_IsRecordContentError(esp_err_t err)
{
  return ((err == ESP_ERR_INVALID_SIZE) || (err == ESP_ERR_INVALID_CRC) ||
          (err == ESP_ERR_INVALID_VERSION) ||
          (err == ESP_ERR_INVALID_RESPONSE));
}

/** @brief 编码并写入一条记录。 */
static esp_err_t Inf_SleepStorage_WriteRecord(uint8_t slot, uint32_t record_id,
                                              uint8_t schema_version,
                                              const void *payload,
                                              size_t payload_length)
{
  uint8_t blob[INF_SLEEP_STORAGE_RECORD_MAX_SIZE] = {0U};

  Inf_SleepStorage_PutU32(&blob[INF_SLEEP_STORAGE_RECORD_MAGIC_OFFSET],
                          INF_SLEEP_STORAGE_RECORD_MAGIC);
  Inf_SleepStorage_PutU32(&blob[INF_SLEEP_STORAGE_RECORD_ID_OFFSET], record_id);
  blob[INF_SLEEP_STORAGE_RECORD_LENGTH_OFFSET] = (uint8_t)payload_length;
  blob[INF_SLEEP_STORAGE_RECORD_SCHEMA_OFFSET] = schema_version;
  blob[INF_SLEEP_STORAGE_RECORD_VERSION_OFFSET] =
      INF_SLEEP_STORAGE_FORMAT_VERSION;
  blob[INF_SLEEP_STORAGE_RECORD_RESERVED_OFFSET] = 0U;
  memcpy(&blob[INF_SLEEP_STORAGE_RECORD_HEADER_SIZE], payload, payload_length);

  size_t crc_offset = INF_SLEEP_STORAGE_RECORD_HEADER_SIZE + payload_length;
  uint32_t crc = Inf_SleepStorage_CalculateCrc(blob, crc_offset);
  Inf_SleepStorage_PutU32(&blob[crc_offset], crc);

  char key[INF_SLEEP_STORAGE_SLOT_KEY_SIZE];
  Inf_SleepStorage_MakeSlotKey(slot, key);

  return Dri_NVS_WriteBlob(INF_SLEEP_STORAGE_NAMESPACE, key, blob,
                           crc_offset + INF_SLEEP_STORAGE_CRC_SIZE);
}

/**
 * @brief 读取并校验一个物理槽。
 *
 * payload 为 NULL 时只校验并返回记录信息，供启动扫描使用。
 */
static esp_err_t Inf_SleepStorage_ReadRecord(
    uint8_t slot, inf_sleep_storage_record_info_t *info, void *payload,
    size_t payload_capacity, size_t *actual_length)
{
  char key[INF_SLEEP_STORAGE_SLOT_KEY_SIZE];
  Inf_SleepStorage_MakeSlotKey(slot, key);

  size_t blob_length = 0U;
  esp_err_t err = Dri_NVS_ReadBlob(INF_SLEEP_STORAGE_NAMESPACE, key, NULL,
                                   &blob_length);

  if (err != ESP_OK)
  {
    return err;
  }

  if ((blob_length < INF_SLEEP_STORAGE_RECORD_MIN_SIZE) ||
      (blob_length > INF_SLEEP_STORAGE_RECORD_MAX_SIZE))
  {
    return ESP_ERR_INVALID_SIZE;
  }

  uint8_t blob[INF_SLEEP_STORAGE_RECORD_MAX_SIZE] = {0U};
  size_t read_length = blob_length;
  err = Dri_NVS_ReadBlob(INF_SLEEP_STORAGE_NAMESPACE, key, blob, &read_length);

  if (err != ESP_OK)
  {
    return err;
  }

  if (read_length != blob_length)
  {
    return ESP_ERR_INVALID_SIZE;
  }

  if (Inf_SleepStorage_GetU32(
          &blob[INF_SLEEP_STORAGE_RECORD_MAGIC_OFFSET]) !=
      INF_SLEEP_STORAGE_RECORD_MAGIC)
  {
    return ESP_ERR_INVALID_RESPONSE;
  }

  if (blob[INF_SLEEP_STORAGE_RECORD_VERSION_OFFSET] !=
      INF_SLEEP_STORAGE_FORMAT_VERSION)
  {
    return ESP_ERR_INVALID_VERSION;
  }

  if (blob[INF_SLEEP_STORAGE_RECORD_RESERVED_OFFSET] != 0U)
  {
    return ESP_ERR_INVALID_RESPONSE;
  }

  size_t stored_payload_length =
      blob[INF_SLEEP_STORAGE_RECORD_LENGTH_OFFSET];
  size_t expected_length = INF_SLEEP_STORAGE_RECORD_MIN_SIZE +
                           stored_payload_length;

  if ((stored_payload_length > INF_SLEEP_STORAGE_MAX_PAYLOAD_SIZE) ||
      (blob_length != expected_length))
  {
    return ESP_ERR_INVALID_SIZE;
  }

  uint32_t record_id = Inf_SleepStorage_GetU32(
      &blob[INF_SLEEP_STORAGE_RECORD_ID_OFFSET]);

  if (record_id == INF_SLEEP_STORAGE_INVALID_RECORD_ID)
  {
    return ESP_ERR_INVALID_RESPONSE;
  }

  size_t crc_offset =
      INF_SLEEP_STORAGE_RECORD_HEADER_SIZE + stored_payload_length;
  uint32_t stored_crc = Inf_SleepStorage_GetU32(&blob[crc_offset]);
  uint32_t calculated_crc = Inf_SleepStorage_CalculateCrc(blob, crc_offset);

  if (stored_crc != calculated_crc)
  {
    return ESP_ERR_INVALID_CRC;
  }

  if (actual_length != NULL)
  {
    *actual_length = stored_payload_length;
  }

  if (info != NULL)
  {
    info->record_id = record_id;
    info->schema_version = blob[INF_SLEEP_STORAGE_RECORD_SCHEMA_OFFSET];
  }

  if (payload == NULL)
  {
    return ESP_OK;
  }

  if (payload_capacity < stored_payload_length)
  {
    return ESP_ERR_INVALID_SIZE;
  }

  memcpy(payload, &blob[INF_SLEEP_STORAGE_RECORD_HEADER_SIZE],
         stored_payload_length);

  return ESP_OK;
}

/** @brief 将当前运行期状态写入元数据。 */
static esp_err_t Inf_SleepStorage_WriteMetadata(void)
{
  uint8_t blob[INF_SLEEP_STORAGE_METADATA_SIZE] = {0U};

  Inf_SleepStorage_PutU32(&blob[INF_SLEEP_STORAGE_METADATA_MAGIC_OFFSET],
                          INF_SLEEP_STORAGE_METADATA_MAGIC);
  blob[INF_SLEEP_STORAGE_METADATA_VERSION_OFFSET] =
      INF_SLEEP_STORAGE_FORMAT_VERSION;
  blob[INF_SLEEP_STORAGE_METADATA_COUNT_OFFSET] = s_count;
  blob[INF_SLEEP_STORAGE_METADATA_NEXT_SLOT_OFFSET] = s_next_slot;
  blob[INF_SLEEP_STORAGE_METADATA_RESERVED_OFFSET] = 0U;
  Inf_SleepStorage_PutU32(&blob[INF_SLEEP_STORAGE_METADATA_NEXT_ID_OFFSET],
                          s_next_record_id);

  uint32_t crc = Inf_SleepStorage_CalculateCrc(
      blob, INF_SLEEP_STORAGE_METADATA_CRC_OFFSET);
  Inf_SleepStorage_PutU32(&blob[INF_SLEEP_STORAGE_METADATA_CRC_OFFSET], crc);

  return Dri_NVS_WriteBlob(INF_SLEEP_STORAGE_NAMESPACE,
                           INF_SLEEP_STORAGE_METADATA_KEY, blob, sizeof(blob));
}

/** @brief 读取并校验持久化元数据。 */
static esp_err_t Inf_SleepStorage_ReadMetadata(
    inf_sleep_storage_metadata_t *metadata, bool *found)
{
  *found = false;

  size_t blob_length = 0U;
  esp_err_t err = Dri_NVS_ReadBlob(INF_SLEEP_STORAGE_NAMESPACE,
                                   INF_SLEEP_STORAGE_METADATA_KEY, NULL,
                                   &blob_length);

  if (err == ESP_ERR_NOT_FOUND)
  {
    return ESP_OK;
  }

  if (err != ESP_OK)
  {
    return err;
  }

  if (blob_length != INF_SLEEP_STORAGE_METADATA_SIZE)
  {
    return ESP_ERR_INVALID_SIZE;
  }

  uint8_t blob[INF_SLEEP_STORAGE_METADATA_SIZE] = {0U};
  size_t read_length = sizeof(blob);
  err = Dri_NVS_ReadBlob(INF_SLEEP_STORAGE_NAMESPACE,
                         INF_SLEEP_STORAGE_METADATA_KEY, blob, &read_length);

  if (err != ESP_OK)
  {
    return err;
  }

  if (read_length != sizeof(blob))
  {
    return ESP_ERR_INVALID_SIZE;
  }

  if (Inf_SleepStorage_GetU32(
          &blob[INF_SLEEP_STORAGE_METADATA_MAGIC_OFFSET]) !=
      INF_SLEEP_STORAGE_METADATA_MAGIC)
  {
    return ESP_ERR_INVALID_RESPONSE;
  }

  if (blob[INF_SLEEP_STORAGE_METADATA_VERSION_OFFSET] !=
      INF_SLEEP_STORAGE_FORMAT_VERSION)
  {
    return ESP_ERR_INVALID_VERSION;
  }

  if (blob[INF_SLEEP_STORAGE_METADATA_RESERVED_OFFSET] != 0U)
  {
    return ESP_ERR_INVALID_RESPONSE;
  }

  uint32_t stored_crc = Inf_SleepStorage_GetU32(
      &blob[INF_SLEEP_STORAGE_METADATA_CRC_OFFSET]);
  uint32_t calculated_crc = Inf_SleepStorage_CalculateCrc(
      blob, INF_SLEEP_STORAGE_METADATA_CRC_OFFSET);

  if (stored_crc != calculated_crc)
  {
    return ESP_ERR_INVALID_CRC;
  }

  metadata->count = blob[INF_SLEEP_STORAGE_METADATA_COUNT_OFFSET];
  metadata->next_slot = blob[INF_SLEEP_STORAGE_METADATA_NEXT_SLOT_OFFSET];
  metadata->next_record_id = Inf_SleepStorage_GetU32(
      &blob[INF_SLEEP_STORAGE_METADATA_NEXT_ID_OFFSET]);

  if ((metadata->count > INF_SLEEP_STORAGE_CAPACITY) ||
      (metadata->next_slot >= INF_SLEEP_STORAGE_CAPACITY))
  {
    return ESP_ERR_INVALID_RESPONSE;
  }

  *found = true;

  return ESP_OK;
}

/** @brief 扫描全部物理槽并重建运行期顺序。 */
static esp_err_t Inf_SleepStorage_ScanRecords(void)
{
  for (uint8_t slot = 0U; slot < INF_SLEEP_STORAGE_CAPACITY; ++slot)
  {
    inf_sleep_storage_record_info_t info = {0};
    size_t payload_length = 0U;
    esp_err_t err = Inf_SleepStorage_ReadRecord(slot, &info, NULL, 0U,
                                                &payload_length);

    if (err == ESP_ERR_NOT_FOUND)
    {
      continue;
    }

    if (Inf_SleepStorage_IsRecordContentError(err))
    {
      ESP_LOGW(TAG, "忽略无效睡眠记录槽 %u：%s", (unsigned int)slot,
               esp_err_to_name(err));
      continue;
    }

    if (err != ESP_OK)
    {
      return err;
    }

    err = Inf_SleepStorage_InsertOrderedSlot(slot, info.record_id);

    if (err != ESP_OK)
    {
      ESP_LOGW(TAG, "忽略重复睡眠记录编号 %lu（槽 %u）",
               (unsigned long)info.record_id, (unsigned int)slot);
    }
  }

  if (s_count == 0U)
  {
    s_next_record_id = 1U;
  }
  else
  {
    uint32_t newest_record_id = s_record_ids[s_slot_order[s_count - 1U]];
    s_next_record_id = (newest_record_id == UINT32_MAX)
                           ? INF_SLEEP_STORAGE_INVALID_RECORD_ID
                           : (newest_record_id + 1U);
  }

  Inf_SleepStorage_UpdateNextSlot();

  return ESP_OK;
}

/** @brief 根据 record_id 查找物理槽。 */
static esp_err_t Inf_SleepStorage_FindSlotById(uint32_t record_id,
                                               uint8_t *slot)
{
  for (uint8_t index = 0U; index < s_count; ++index)
  {
    uint8_t current_slot = s_slot_order[index];

    if (s_record_ids[current_slot] == record_id)
    {
      *slot = current_slot;
      return ESP_OK;
    }
  }

  return ESP_ERR_NOT_FOUND;
}

esp_err_t Inf_SleepStorage_Init(void)
{
  if (s_initialized)
  {
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t err = Dri_NVS_Init();

  if (err != ESP_OK)
  {
    return err;
  }

  Inf_SleepStorage_ResetRuntimeState();

  err = Inf_SleepStorage_ScanRecords();

  if (err != ESP_OK)
  {
    Inf_SleepStorage_ResetRuntimeState();
    return err;
  }

  inf_sleep_storage_metadata_t metadata = {0};
  bool metadata_found = false;
  bool rewrite_metadata = false;
  err = Inf_SleepStorage_ReadMetadata(&metadata, &metadata_found);

  if (Inf_SleepStorage_IsRecordContentError(err))
  {
    ESP_LOGW(TAG, "睡眠记录元数据无效，将根据有效记录重建：%s",
             esp_err_to_name(err));
    rewrite_metadata = true;
  }
  else if (err != ESP_OK)
  {
    Inf_SleepStorage_ResetRuntimeState();
    return err;
  }
  else if (!metadata_found)
  {
    rewrite_metadata = true;
  }
  else
  {
    /*
     * 当元数据中的 next_record_id 更大时保留该值，避免有效槽损坏后重复
     * 使用已经分配过的编号。0 表示编号已耗尽，也必须保留。
     */
    if ((metadata.next_record_id == INF_SLEEP_STORAGE_INVALID_RECORD_ID) ||
        ((s_next_record_id != INF_SLEEP_STORAGE_INVALID_RECORD_ID) &&
         (metadata.next_record_id > s_next_record_id)))
    {
      s_next_record_id = metadata.next_record_id;
    }

    if ((metadata.count != s_count) ||
        (metadata.next_slot != s_next_slot) ||
        (metadata.next_record_id != s_next_record_id))
    {
      rewrite_metadata = true;
    }
  }

  if (rewrite_metadata)
  {
    err = Inf_SleepStorage_WriteMetadata();

    if (err != ESP_OK)
    {
      Inf_SleepStorage_ResetRuntimeState();
      return err;
    }
  }

  s_initialized = true;
  ESP_LOGI(TAG, "睡眠记录存储初始化完成，有效记录 %u/%u",
           (unsigned int)s_count,
           (unsigned int)INF_SLEEP_STORAGE_CAPACITY);

  return ESP_OK;
}

esp_err_t Inf_SleepStorage_Append(uint8_t schema_version,
                                  const void *payload, size_t payload_length,
                                  uint32_t *new_record_id,
                                  uint32_t *overwritten_record_id)
{
  if (new_record_id != NULL)
  {
    *new_record_id = INF_SLEEP_STORAGE_INVALID_RECORD_ID;
  }

  if (overwritten_record_id != NULL)
  {
    *overwritten_record_id = INF_SLEEP_STORAGE_INVALID_RECORD_ID;
  }

  if (!s_initialized)
  {
    return ESP_ERR_INVALID_STATE;
  }

  if ((payload == NULL) || (payload_length == 0U) ||
      (payload_length > INF_SLEEP_STORAGE_MAX_PAYLOAD_SIZE))
  {
    return ESP_ERR_INVALID_ARG;
  }

  if (s_next_record_id == INF_SLEEP_STORAGE_INVALID_RECORD_ID)
  {
    ESP_LOGE(TAG, "睡眠记录编号已耗尽");
    return ESP_ERR_INVALID_STATE;
  }

  uint8_t slot = s_next_slot;
  uint32_t assigned_record_id = s_next_record_id;
  uint32_t overwritten_id = INF_SLEEP_STORAGE_INVALID_RECORD_ID;
  bool storage_full = (s_count >= INF_SLEEP_STORAGE_CAPACITY);

  if (storage_full)
  {
    overwritten_id = s_record_ids[slot];
  }

  esp_err_t err = Inf_SleepStorage_WriteRecord(
      slot, assigned_record_id, schema_version, payload, payload_length);

  if (err != ESP_OK)
  {
    return err;
  }

  if (storage_full)
  {
    for (uint8_t index = 1U; index < s_count; ++index)
    {
      s_slot_order[index - 1U] = s_slot_order[index];
    }
  }
  else
  {
    ++s_count;
  }

  s_record_ids[slot] = assigned_record_id;
  s_slot_order[s_count - 1U] = slot;
  s_next_record_id = (assigned_record_id == UINT32_MAX)
                         ? INF_SLEEP_STORAGE_INVALID_RECORD_ID
                         : (assigned_record_id + 1U);
  Inf_SleepStorage_UpdateNextSlot();

  if (new_record_id != NULL)
  {
    *new_record_id = assigned_record_id;
  }

  if (overwritten_record_id != NULL)
  {
    *overwritten_record_id = overwritten_id;
  }

  err = Inf_SleepStorage_WriteMetadata();

  if (err != ESP_OK)
  {
    /* 记录已提交，下次初始化会扫描槽并恢复元数据。 */
    ESP_LOGE(TAG, "记录 %lu 已保存，但元数据提交失败：%s",
             (unsigned long)assigned_record_id, esp_err_to_name(err));
    return err;
  }

  ESP_LOGI(TAG, "睡眠记录 %lu 已保存到槽 %u",
           (unsigned long)assigned_record_id, (unsigned int)slot);

  return ESP_OK;
}

esp_err_t Inf_SleepStorage_ReadByIndex(
    uint8_t index, inf_sleep_storage_record_info_t *info, void *payload,
    size_t payload_capacity, size_t *actual_length)
{
  if (!s_initialized)
  {
    return ESP_ERR_INVALID_STATE;
  }

  if ((info == NULL) || (payload == NULL) || (actual_length == NULL))
  {
    return ESP_ERR_INVALID_ARG;
  }

  if (index >= s_count)
  {
    return ESP_ERR_NOT_FOUND;
  }

  return Inf_SleepStorage_ReadRecord(s_slot_order[index], info, payload,
                                     payload_capacity, actual_length);
}

esp_err_t Inf_SleepStorage_ReadById(
    uint32_t record_id, inf_sleep_storage_record_info_t *info, void *payload,
    size_t payload_capacity, size_t *actual_length)
{
  if (!s_initialized)
  {
    return ESP_ERR_INVALID_STATE;
  }

  if ((record_id == INF_SLEEP_STORAGE_INVALID_RECORD_ID) || (info == NULL) ||
      (payload == NULL) || (actual_length == NULL))
  {
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t slot = 0U;
  esp_err_t err = Inf_SleepStorage_FindSlotById(record_id, &slot);

  if (err != ESP_OK)
  {
    return err;
  }

  return Inf_SleepStorage_ReadRecord(slot, info, payload, payload_capacity,
                                     actual_length);
}

esp_err_t Inf_SleepStorage_UpdateById(uint32_t record_id,
                                      uint8_t schema_version,
                                      const void *payload,
                                      size_t payload_length)
{
  if (!s_initialized)
  {
    return ESP_ERR_INVALID_STATE;
  }

  if ((record_id == INF_SLEEP_STORAGE_INVALID_RECORD_ID) ||
      (payload == NULL) || (payload_length == 0U) ||
      (payload_length > INF_SLEEP_STORAGE_MAX_PAYLOAD_SIZE))
  {
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t slot = 0U;
  esp_err_t err = Inf_SleepStorage_FindSlotById(record_id, &slot);

  if (err != ESP_OK)
  {
    return err;
  }

  err = Inf_SleepStorage_WriteRecord(slot, record_id, schema_version, payload,
                                     payload_length);

  if (err == ESP_OK)
  {
    ESP_LOGI(TAG, "睡眠记录 %lu 已更新", (unsigned long)record_id);
  }

  return err;
}

esp_err_t Inf_SleepStorage_GetCount(uint8_t *count)
{
  if (!s_initialized)
  {
    return ESP_ERR_INVALID_STATE;
  }

  if (count == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  *count = s_count;

  return ESP_OK;
}

esp_err_t Inf_SleepStorage_ClearAll(void)
{
  if (!s_initialized)
  {
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t err =
      Dri_NVS_EraseNamespace(INF_SLEEP_STORAGE_NAMESPACE);

  if (err != ESP_OK)
  {
    return err;
  }

  Inf_SleepStorage_ResetRuntimeState();
  ESP_LOGI(TAG, "全部睡眠记录已清除");

  return ESP_OK;
}
