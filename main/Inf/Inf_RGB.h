#ifndef __INF_RGB_H__
#define __INF_RGB_H__

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include <math.h>
#include <stdint.h>

esp_err_t inf_rgb_init(void);
esp_err_t inf_rgb_deinit(void);

#endif
