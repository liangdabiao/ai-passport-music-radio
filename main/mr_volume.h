// main/mr_volume.h —— 音量档位与缩放。
//
// 不依赖 ESP-IDF 与 LVGL，宿主测试直接编译它。电台是长时间听的东西，10%
// 一档（0..100 共 11 档）比家族老的 6 档细腻；长按上/下增减，到顶/到底夹住
// （不回绕 —— 听音乐时手一滑从 100 跳到 0 是惊吓，不是功能）。
// 0 档就是关，于是不需要单独的静音开关 —— 与家族各应用同一套决定。
#pragma once

#include <stddef.h>
#include <stdint.h>

#define MR_VOLUME_STEP 10
#define MR_VOLUME_OFF  0
#define MR_VOLUME_MAX  100

// 出厂默认档位（书房 3W 喇叭实测 60% 起步舒服；ES8311 输出余量大）。
#define MR_VOLUME_DEFAULT 60

// 电台这一路的编解码器音量基准（用户音量 100% 时 = 满量程，由 codec 自己限幅）。
#define MR_VOLUME_BASE_RADIO 100

// 音效在用户音量 100% 时的编解码器音量基准。必须与 mr_sfx.c 的
// MR_SFX_BASE_VOLUME 一致 —— 由 tests/test_mr_volume.c 钉住。
#define MR_VOLUME_BASE_SFX 58

// 档位加/减一格，到顶/到底夹住。越界输入按关处理（回到可调范围）。
uint8_t mr_volume_up(uint8_t level);
uint8_t mr_volume_down(uint8_t level);

// 把用户档位乘到某一路的基准音量上。整数截断（不是四舍五入）：与家族同一套算术。
uint8_t mr_volume_scale(uint8_t base, uint8_t level);

// 档位文本："关" 或 "60%"。返回写入的字节数；容量不足时返回 0 并把 out 置为
// 空串（不留半截字符串）。
uint16_t mr_volume_text(uint8_t level, char *out, size_t capacity);
