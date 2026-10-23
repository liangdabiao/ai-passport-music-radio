// main/mr_store.c —— 见 mr_store.h。
#include "mr_store.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "mr_station.h"
#include "mr_volume.h"

static const char *TAG = "mr_store";

static const char *NAMESPACE = "musicradio";
static const char *KEY_STATION = "station";
static const char *KEY_VOLUME = "volume";

esp_err_t mr_store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS 需要重建（%s），擦除后重试", esp_err_to_name(err));
        err = nvs_flash_erase();
        if (err != ESP_OK) return err;
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t mr_store_load_station(uint8_t *index)
{
    if (!index) return ESP_ERR_INVALID_ARG;
    *index = 0;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return err; // 首次开机 NOT_FOUND：回 0

    uint8_t value = 0;
    err = nvs_get_u8(handle, KEY_STATION, &value);
    nvs_close(handle);
    if (err != ESP_OK) return err;

    if (!mr_station_valid(value)) value = 0; // 台单缩编后的陈旧值
    *index = value;
    return ESP_OK;
}

esp_err_t mr_store_save_station(const uint8_t index)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_u8(handle, KEY_STATION, index);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t mr_store_load_volume(uint8_t *level)
{
    if (!level) return ESP_ERR_INVALID_ARG;
    *level = MR_VOLUME_DEFAULT;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;

    uint8_t value = MR_VOLUME_DEFAULT;
    err = nvs_get_u8(handle, KEY_VOLUME, &value);
    nvs_close(handle);
    if (err != ESP_OK) return err;

    if (value > MR_VOLUME_MAX || value % MR_VOLUME_STEP != 0) {
        value = MR_VOLUME_DEFAULT; // 只认格点上的档位
    }
    *level = value;
    return ESP_OK;
}

esp_err_t mr_store_save_volume(const uint8_t level)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_u8(handle, KEY_VOLUME, level);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
