// main/mr_player.h —— 音乐电台流媒体播放任务。
//
// 架构（单任务，天然免锁）：
//   raw-socket HTTP GET（hot-daily 真机教训：esp_http_client 在 C3+lwip 上
//   不可用；astock-quote 全程 raw socket 验证过）→ 环形缓冲（24KB，吸收网络
//   抖动）→ 攒够启动水位后逐帧解码（libhelix 定点 MP3）→ bsp_audio_write()
//   阻塞写出 —— I2S 的反压就是解码的节拍器，天然按实时走。
//
// 一次只有一路流：play() 换台 = 停旧连新；协议细节（重定向、100-continue、
// chunked）在 mr_http 纯逻辑层，本模块只做设备端粘合。
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MR_PLAY_IDLE = 0,  // 未播放
    MR_PLAY_CONNECT,   // 连接/握手中
    MR_PLAY_BUFFERING, // 已连上，攒缓冲
    MR_PLAY_PLAYING,   // 播放中
    MR_PLAY_ERROR,     // 失败（msg 有中文短句，UI 直接显示）
} mr_play_state_t;

typedef struct {
    mr_play_state_t state;
    char msg[64];       // 状态短句（连接中… / 重试 2/3 / 电台无响应 等）
    uint8_t station;    // 当前（或最近一次请求的）台
    uint16_t kbps;      // 码率：icy-br 优先，否则帧头推算；0 = 未知
    uint32_t sample_hz; // 解码出的采样率；0 = 未知
    uint8_t channels;   // 1/2；0 = 未知
} mr_play_status_t;

// 创建播放任务（幂等）。app 启动时调用一次。
void mr_player_start(void);

// 请求播放指定台（正在播则先停旧流再连新台 —— 换台同用这一口）。
void mr_player_play(uint8_t station_index);

// 请求停止（幂等）。完成后回 IDLE。
void mr_player_stop(void);

// 是否正在出声（CONNECT/BUFFERING/PLAYING）。音效门用它让路。
bool mr_player_is_streaming(void);

// 状态快照（内部互斥，UI 的 LVGL 定时器里随便调）。
void mr_player_status(mr_play_status_t *out);

// 应用音量档位（0..100 用户档）到编解码器。播放中与待机时都可调；
// 音效模块共用这个全局 codec 音量（各自在出声前重设自己的基准）。
void mr_player_set_volume(uint8_t level);
