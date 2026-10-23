// main/mr_http.h —— 流媒体 HTTP 的纯逻辑解析：响应头扫描/解析 + chunked 剥框。
//
// 与 ESP-IDF/LVGL 解耦，宿主测试直接编译它。设备端 mr_player 用它处理
// raw socket 读到的字节流（hot-daily 真机教训：esp_http_client 在 C3+lwip
// 上不可用；家族经 raw socket 全程验证 —— 本项目沿同一路径）。
//
// 设计要点：
//   头部扫描容忍分片到达（每次 socket read 都可能是半截头）；1xx 中间响应
//   跳过继续找真正的响应头 —— 有的台源会先回 100 Continue。
//   chunked 解码是有状态流式状态机：输出只可能 ≤ 输入，天然适配环形缓冲。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// 在流缓冲里找响应头终点（"\r\n\r\n"）。返回头部总长（含终点四字节）；
// 未找到返回 0；have 超过 cap 仍找不到返回 SIZE_MAX（流损坏，应放弃）。
size_t mr_http_head_scan(const char *buf, size_t have, size_t cap);

typedef struct {
    int status;               // 状态码（1xx 已被调用方跳过）
    bool chunked;             // Transfer-Encoding: chunked
    long long content_length; // Content-Length，没有则 -1
    bool accept_ranges;       // Accept-Ranges 存在（信息性，不参与判定）
    char location[192];       // 3xx 的 Location（原样，无则空串）
    char content_type[48];    // 小写化后的 Content-Type（截到分号前）
    char icy_name[64];        // icy-name（台名，无则空串）
    int icy_br;               // icy-br 码率 kbps（无则 0）
} mr_http_head_t;

// 解析一个完整头部（mr_http_head_scan 返回的长度范围）。头部字段大小写不
// 敏感；任何畸形都不算致命——尽力解析，status 解析失败返回 false。
bool mr_http_head_parse(const char *head, size_t len, mr_http_head_t *out);

// ---------------------------------------------------------------- chunked ----
typedef enum {
    MR_CK_SIZE = 0,  // 正在读 chunk 长度行（十六进制 + CRLF）
    MR_CK_SIZE_LF,   // 长度行收完 '\r'，等 '\n'
    MR_CK_DATA,      // 正在读 chunk 数据
    MR_CK_DATA_CR,   // 数据读完，等 '\r'
    MR_CK_DATA_LF,   // 数据读完收了 '\r'，等 '\n'
    MR_CK_TRAILER,   // 0 长度 chunk 后的尾注：滚窗扫 "\r\n\r\n"
    MR_CK_DONE,      // 流结束，不再消费
    MR_CK_BAD,       // 流损坏
} mr_chunk_state_t;

typedef struct {
    mr_chunk_state_t state;
    size_t remain;     // 当前 chunk 剩余字节数
    uint64_t size_acc; // 长度行累计值
    int size_digits;   // 长度行已收到的位数（防溢出）
    uint64_t window;   // 尾注滚动窗口（低 32 位为最近 4 字节）
} mr_chunker_t;

void mr_chunker_init(mr_chunker_t *c);

// 流式剥 chunked 框架：消费输入（推进 *in / *n），产出净荷到 out（推进 *cap）。
// 一次调用可同时消费与产出多次；坏流置 state = MR_CK_BAD 并停止消费。
// 调用方循环喂入直到输入耗尽或输出满。干净流（非 chunked）不要走这里。
void mr_chunker_feed(mr_chunker_t *c, const uint8_t **in, size_t *n,
                     uint8_t *out, size_t *cap);
