// main/mr_store.h —— 设置的 NVS 落盘：上次电台、音量档位。
//
// 命名空间与家族里其它应用刻意不同名（"musicradio"）：同机刷过别的应用时互不
// 覆盖。WiFi 凭据归 esp-wifi-connect 组件自己的 NVS，这里不碰。
#pragma once

#include <stdint.h>

#include "esp_err.h"

// 打开 NVS（含分区重建兜底）。失败时应用仍可运行，只是不持久化。
esp_err_t mr_store_init(void);

// 上次电台（首次开机回 0；越界值按 0 处理）。
esp_err_t mr_store_load_station(uint8_t *index);
esp_err_t mr_store_save_station(uint8_t index);

// 音量档位（首次开机回出厂默认；越界值按默认处理）。
esp_err_t mr_store_load_volume(uint8_t *level);
esp_err_t mr_store_save_volume(uint8_t level);
