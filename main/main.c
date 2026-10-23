// main/main.c —— 音乐电台的入口。
//
// 与家族联网应用的骨架一致：WiFi（esp-wifi-connect 组件全权管理凭据/重连/
// 配网热点）+ 流媒体播放任务（raw-socket HTTP → libhelix MP3 → ES8311）。
// LVGL 锁、按键队列、栈深预算全部沿用家族已验证的模式。
//
// 三键语义（家族惯例 CLICK+LONG）：
//   上/下 短按  换台（收音机页，线性循环）
//   确定 短按   播放/暂停 / 进入
//   确定 长按   返回（收音机页还会停播）；标题页=进配网
//   上/下 长按  音量 ±（收音机页浮层）
//
// 任务清单：main_task（初始化）、LVGL 任务（BSP）、输入任务（8KB，按键分
// 发）、播放任务（16KB，收流+解码+写 PCM）。BLE 不使用（配网走热点）。
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"

#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "fap_screenshot.h"
#include "mr_app.h"
#include "mr_player.h"
#include "mr_sfx.h"
#include "mr_store.h"
#include "mr_wifi.h"

static const char *TAG = "musicradio";

#define INPUT_QUEUE_DEPTH 8
#define INPUT_TASK_STACK  8192
#define INPUT_TASK_PRIO   5

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t event;
} input_event_t;

static QueueHandle_t s_input_queue;
static TaskHandle_t s_input_task;
static volatile bool s_input_ready;

// 按键回调运行在共享的 esp_timer 任务上：只做一次非阻塞入队，立刻返回。
static void on_key(bsp_btn_t btn, bsp_btn_ev_t event, void *user)
{
    (void)user;
    if (!s_input_ready || !s_input_queue) return;
    const input_event_t input = { .btn = btn, .event = event };
    (void)xQueueSend(s_input_queue, &input, 0);
}

static bool translate(const input_event_t *input, mr_key_t *out)
{
    // 家族同款：CLICK（松手判定，带消抖）+ LONG。WiFi 发射会干扰 ADC 分压
    // 采样 —— PRESS（按下瞬发）会出幻影按键（真机教训），禁用。
    if (input->event == BSP_BTN_CLICK) {
        switch (input->btn) {
            case BSP_BTN_UP:   *out = MR_KEY_UP;   return true;
            case BSP_BTN_DOWN: *out = MR_KEY_DOWN; return true;
            case BSP_BTN_OK:   *out = MR_KEY_OK;   return true;
            default:           return false;
        }
    }
    if (input->event == BSP_BTN_LONG) {
        switch (input->btn) {
            case BSP_BTN_OK:   *out = MR_KEY_BACK;     return true;
            case BSP_BTN_UP:   *out = MR_KEY_VOL_UP;   return true;
            case BSP_BTN_DOWN: *out = MR_KEY_VOL_DOWN; return true;
            default:           return false;
        }
    }
    return false; // PRESS/DOUBLE 忽略。
}

static void input_task(void *argument)
{
    (void)argument;
    input_event_t input;
    for (;;) {
        if (xQueueReceive(s_input_queue, &input, portMAX_DELAY) != pdTRUE) continue;
        mr_key_t key;
        if (translate(&input, &key)) mr_app_key(key);
    }
}

static esp_err_t input_start(void)
{
    s_input_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(input_event_t));
    if (!s_input_queue) return ESP_ERR_NO_MEM;

    if (xTaskCreate(input_task, "mr_input", INPUT_TASK_STACK, NULL,
                    INPUT_TASK_PRIO, &s_input_task) != pdPASS) {
        vQueueDelete(s_input_queue);
        s_input_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    const esp_err_t err = bsp_button_init(on_key, NULL);
    if (err != ESP_OK) {
        vTaskDelete(s_input_task);
        s_input_task = NULL;
        vQueueDelete(s_input_queue);
        s_input_queue = NULL;
        return err;
    }
    return ESP_OK;
}

// WiFi 事件（esp-wifi-connect 上下文）：只翻状态旗，UI 各页自行轮询。
static void on_wifi_connected(void)
{
    mr_app_note_wifi(true);
}

static void on_wifi_disconnected(void)
{
    mr_app_note_wifi(false);
}

void app_main(void)
{
    ESP_LOGI(TAG, "音乐电台 启动");

    const esp_sleep_wakeup_cause_t wakeup = esp_sleep_get_wakeup_cause();
    if (wakeup != ESP_SLEEP_WAKEUP_UNDEFINED) {
        ESP_LOGI(TAG, "休眠唤醒原因: %d", wakeup);
    }

    esp_err_t err = bsp_i2c_init();
    if (err != ESP_OK) ESP_LOGW(TAG, "I2C 初始化失败: %s", esp_err_to_name(err));
    (void)bsp_i2c_scan();

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败。检查 SPI 接线"
                      "(MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    bsp_display_backlight(100);

    err = mr_store_init();
    if (err != ESP_OK) ESP_LOGW(TAG, "存储不可用: %s", esp_err_to_name(err));

    if (bsp_audio_init() != ESP_OK) {
        ESP_LOGW(TAG, "音频编解码初始化失败，将以静音方式运行");
    } else if (mr_sfx_start() != ESP_OK) {
        ESP_LOGW(TAG, "音效服务启动失败，将以静音方式运行");
    }
    if (bsp_battery_init() != ESP_OK) {
        ESP_LOGW(TAG, "电量计不可用，顶栏会显示 --");
    }

    const esp_err_t input_err = input_start();
    if (input_err != ESP_OK) {
        ESP_LOGE(TAG, "按键初始化失败: %s", esp_err_to_name(input_err));
    }

    // WiFi：有存档凭据自动连，无存档直接开配网热点（esp-wifi-connect 全权管理）。
    mr_wifi_start(on_wifi_connected, on_wifi_disconnected);

    // 流媒体播放任务：收流 + 解码 + 写 PCM。解码器状态在 libhelix 内是静态
    // arena（不占堆），任务建在 WiFi 之后只花 12KB 栈 + 12KB 环形缓冲，
    // 真机实测两难全时这个顺序最稳。
    mr_player_start();

    if (bsp_lvgl_lock(1000)) {
        mr_app_start();
        bsp_lvgl_unlock();
        s_input_ready = true;
    }
    // 社区发布助手的串口截屏协议（只读）。
    fap_screenshot_start();

    ESP_LOGI(TAG, "就绪:显示=OK 按键=OK");
}
