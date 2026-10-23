// main/mr_app.c —— 见 mr_app.h。
#include "mr_app.h"

#include <string.h>

#include "bsp_display.h"
#include "esp_log.h"

#include "mr_player.h"
#include "mr_sfx.h"
#include "mr_station.h"
#include "mr_store.h"
#include "mr_volume.h"

// 页面实现（mr_page_*.c）。
lv_obj_t *mr_page_title_enter(void);
void mr_page_title_leave(void);
void mr_page_title_key(mr_key_t key);
lv_obj_t *mr_page_tuner_enter(void);
void mr_page_tuner_leave(void);
void mr_page_tuner_key(mr_key_t key);
// 音量变化后由 mr_app 通知收音机页刷新浮层（只在收音机页有效）。
void mr_page_tuner_on_volume(void);
lv_obj_t *mr_page_wifi_enter(void);
void mr_page_wifi_leave(void);
void mr_page_wifi_key(mr_key_t key);

static const char *TAG = "mr_app";

typedef enum {
    PAGE_NONE = 0,
    PAGE_TITLE,
    PAGE_RADIO,
    PAGE_WIFI,
} page_id_t;

static uint8_t s_station;
static uint8_t s_volume = MR_VOLUME_DEFAULT;
static bool s_wifi_connected;

static lv_obj_t *s_screen;
static page_id_t s_page = PAGE_NONE;
static void (*s_leave)(void);

static void replace_page(const page_id_t page, lv_obj_t *(*enter)(void),
                         void (*leave)(void))
{
    if (s_leave) s_leave();
    lv_obj_t *next = enter();
    if (s_screen) lv_obj_delete(s_screen);
    s_screen = next;
    s_page = page;
    s_leave = leave;
    lv_scr_load(s_screen);
}

void mr_app_goto_title(void) { replace_page(PAGE_TITLE, mr_page_title_enter, mr_page_title_leave); }
void mr_app_goto_radio(void) { replace_page(PAGE_RADIO, mr_page_tuner_enter, mr_page_tuner_leave); }
void mr_app_goto_wifi(void) { replace_page(PAGE_WIFI, mr_page_wifi_enter, mr_page_wifi_leave); }

uint8_t mr_app_station(void) { return s_station; }

void mr_app_set_station(const uint8_t index)
{
    if (!mr_station_valid(index)) return;
    s_station = index;
    (void)mr_store_save_station(index);
}

uint8_t mr_app_volume(void) { return s_volume; }

void mr_app_volume_step(const int dir)
{
    s_volume = dir > 0 ? mr_volume_up(s_volume) : mr_volume_down(s_volume);
    (void)mr_store_save_volume(s_volume);
    mr_player_set_volume(s_volume);
    mr_sfx_set_volume(s_volume);
    mr_page_tuner_on_volume(); // 只在收音机页有实体；其他页调用无害
}

bool mr_app_wifi_connected(void) { return s_wifi_connected; }
void mr_app_note_wifi(const bool connected) { s_wifi_connected = connected; }

void mr_app_key(const mr_key_t key)
{
    if (!bsp_lvgl_lock(500)) {
        ESP_LOGW(TAG, "未能取得 LVGL 锁，本次按键被丢弃");
        return;
    }
    switch (s_page) {
        case PAGE_TITLE: mr_page_title_key(key); break;
        case PAGE_RADIO: mr_page_tuner_key(key); break;
        case PAGE_WIFI:  mr_page_wifi_key(key); break;
        default: break;
    }
    bsp_lvgl_unlock();
}

void mr_app_start(void)
{
    uint8_t saved = 0;
    if (mr_store_load_station(&saved) == ESP_OK) {
        s_station = saved;
        ESP_LOGI(TAG, "恢复上次电台：%u %s", s_station, MR_STATIONS[s_station].name);
    } else {
        ESP_LOGI(TAG, "无电台存档，从第 1 台开始");
    }

    uint8_t volume = MR_VOLUME_DEFAULT;
    if (mr_store_load_volume(&volume) == ESP_OK) {
        s_volume = volume;
    } else {
        ESP_LOGI(TAG, "无音量存档，用 %u%%", MR_VOLUME_DEFAULT);
    }
    mr_player_set_volume(s_volume);

    s_page = PAGE_NONE;
    s_leave = NULL;
    s_screen = NULL;
    mr_app_goto_title();
}
