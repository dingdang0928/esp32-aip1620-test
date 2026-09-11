#include <stdbool.h>
#include <stdint.h>

#include "Inf/Inf_AIP1620.h"
#include "Inf_AIP1620.h"
#include "Com_Debug.h"
#include "esp_err.h"
#include "freertos/idf_additions.h"
#include "Inf_RTC.h"
#include "Inf_RGB.h"
#include "freertos/projdefs.h"

void app_main(void)
{
  ESP_ERROR_CHECK(Inf_AIP_1620_Power_Test());

  while (1)
  {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
