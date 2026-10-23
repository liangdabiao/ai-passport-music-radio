// main/mr_player.c —— 见 mr_player.h。
//
// 内存预算（C3 无 PSRAM，全部内部 RAM）：环形缓冲 24KB + helix 解码器状态
// ~20KB（MP3InitDecoder 内部 malloc）+ 任务栈 16KB。换台只换 URL，缓冲与
// 解码器全程复用。
//
// 节拍：单任务循环「收流 → 攒够水位 → 逐帧解码 → bsp_audio_write 阻塞写出」，
// I2S 反压就是解码的节拍器（每帧 ≈26ms@44.1kHz），天然按实时走。staging 与
// PCM 帧缓冲是文件级静态区，不占任务栈。
//
// 格式契约：本模块在出声时独占 codec（采样率按流设置）；停播后 codec 保留
// 流格式，mr_sfx 出声前必须先恢复 16kHz（见 mr_sfx.c）。
#include "mr_player.h"

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "bsp_audio.h"

#include "mp3dec.h"
#include "mr_http.h"
#include "mr_station.h"
#include "mr_volume.h"

static const char *TAG = "mr_player";

#define PLAYER_TASK_STACK 10240
#define PLAYER_TASK_PRIO  6  // 高于 LVGL(4)/输入(5)：解码不能被 UI 挤掉

#define RING_SIZE    (10 * 1024) // 压缩流环形缓冲（≈1.5s@64kbps；LVGL 池 40KB 下的平衡点）
#define START_MARK   (6 * 1024)  // 攒到这个水位才开始解码（≈0.75s@64kbps）
#define LOW_WATER    (2 * 1024)  // 解码停点：留一帧余量就回去收流
#define DECODE_PER_LOOP 4        // 每轮最多解几帧（频繁回来续 DMA，防欠载）
#define MR_GAIN_MASTER  3        // 100% 档位的数字增益倍数（补偿 codec 音量曲线）
#define RECV_MS      20          // 单次 recv 超时：短到不拦住 DMA 喂数
#define STAGING_SIZE 2048        // 解码线性暂存（≥2×最大帧 1052B，容回绕）
#define HEAD_MAX     2048        // 响应头上限
#define HEAD_DEADLINE_MS 10000   // 等响应头的总时限
#define REDIRECT_MAX 3           // 3xx 最多跟 3 跳
#define RETRY_MAX    3           // 断流重试（1s/2s/4s 退避）
#define STARVE_MS    8000        // 连续断流多久判死
#define READ_BURST   1           // 每轮循环只收一块：时间优先给 DMA

// 文案进 UI 状态行（16px 一行 ≤13 全角，短句）。
#define MSG_CONNECT   "连接中…"
#define MSG_BUFFERING "缓冲中…"
#define MSG_PLAYING   "正在播放"
#define MSG_NO_URL    "电台地址无效"
#define MSG_UNREACH   "连不上电台"
#define MSG_BADSTREAM "电台数据异常"

typedef enum {
    SESS_REQUEST = 0, // 新请求/停止：移交控制权
    SESS_REDIRECT,    // 跟随 3xx（*location_out 给出新地址）
    SESS_FAIL,        // 流失败（*msg_out 给出错误文案）
} session_end_t;

typedef struct {
    mr_play_status_t st;      // 快照区（lock 保护）
    SemaphoreHandle_t lock;

    TaskHandle_t task;
    volatile uint32_t serial; // 请求代号：play/stop 各 +1
    volatile uint8_t req_station;
    volatile bool req_valid;

    // 以下仅任务上下文触碰：
    uint8_t *ring;
    size_t ring_r, ring_w, ring_used;
    HMP3Decoder dec;
    bool dec_open;
    bool format_set;          // 本次会话是否已按流设置过 codec
    uint32_t fmt_hz;
    uint8_t fmt_ch;
} player_t;

static player_t P;
static int16_t s_pcm[1152 * 2]; // 一帧 PCM 上限（1152 样本 × 2 声道）
static uint8_t s_staging[STAGING_SIZE];
static uint8_t s_level = MR_VOLUME_DEFAULT; // 用户音量档（播放器侧副本）
static uint8_t s_applied_codec_vol;      // 已下发的 codec 音量（0 = 需重下发）

static uint8_t mr_level_get(void) { return s_level; }

// ---------------------------------------------------------------- 快照 ----
static void publish(const mr_play_state_t state, const char *msg)
{
    xSemaphoreTake(P.lock, portMAX_DELAY);
    P.st.state = state;
    if (msg) snprintf(P.st.msg, sizeof(P.st.msg), "%s", msg);
    xSemaphoreGive(P.lock);
}

static void publish_retry(const unsigned attempt, const unsigned total)
{
    xSemaphoreTake(P.lock, portMAX_DELAY);
    P.st.state = MR_PLAY_CONNECT;
    snprintf(P.st.msg, sizeof(P.st.msg), "重连 %u/%u…", attempt, total);
    xSemaphoreGive(P.lock);
}

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

// ---------------------------------------------------------------- 环形 ----
static void ring_reset(void)
{
    P.ring_r = P.ring_w = P.ring_used = 0;
}

static size_t ring_space(void)
{
    return RING_SIZE - P.ring_used;
}

// 拷出至多 want 字节到线性区（不消费，回绕自动拼头尾）。返回拷到的字节数。
static size_t ring_peek_linear(uint8_t *dst, const size_t want)
{
    size_t take = want < P.ring_used ? want : P.ring_used;
    const size_t tail = RING_SIZE - P.ring_r;
    const size_t first = take < tail ? take : tail;
    memcpy(dst, P.ring + P.ring_r, first);
    if (first < take) {
        memcpy(dst + first, P.ring, take - first);
    }
    return take;
}

static void ring_drop(size_t n)
{
    if (n > P.ring_used) n = P.ring_used;
    P.ring_r = (P.ring_r + n) % RING_SIZE;
    P.ring_used -= n;
}

// 净荷尽量塞进环形缓冲，返回**实际消费**的输入字节数。
//
// 绝不丢字节：环满时只消费塞得下的部分，剩下的留在调用方的缓冲里，等解码
// 腾出空间再来（真机教训：旧实现把读进来的尾巴悄悄丢掉，流被截断后解码器
// 读到假帧，表现为速度漂移、丝丝声、频繁断流重连）。
static size_t ring_push_some(const uint8_t *data, const size_t len,
                             const bool chunked, mr_chunker_t *ck,
                             bool *bad)
{
    *bad = false;
    size_t used = 0;
    while (used < len) {
        if (chunked && (ck->state == MR_CK_DONE || ck->state == MR_CK_BAD)) break;
        const size_t space = ring_space();
        if (space == 0) break;
        uint8_t *out = P.ring + P.ring_w;
        size_t cap = RING_SIZE - P.ring_w; // 到缓冲尾的直线长度
        if (cap > space) cap = space;

        if (chunked) {
            const uint8_t *inp = data + used;
            size_t inlen = len - used;
            const size_t cap_before = cap;
            mr_chunker_feed(ck, &inp, &inlen, out, &cap);
            const size_t consumed = (len - used) - inlen;
            const size_t produced = cap_before - cap;
            P.ring_w = (P.ring_w + produced) % RING_SIZE;
            P.ring_used += produced;
            used += consumed;
            if (ck->state == MR_CK_BAD) {
                *bad = true;
                break;
            }
            if (consumed == 0 && produced == 0) break; // DONE / 无进展
        } else {
            const size_t move = (len - used) < cap ? (len - used) : cap;
            memcpy(out, data + used, move);
            P.ring_w = (P.ring_w + move) % RING_SIZE;
            P.ring_used += move;
            used += move;
        }
    }
    return used;
}

// 收流残留（没塞进环形缓冲的字节）。会话开始时清零。
static uint8_t s_pending[2048];
static size_t s_pending_len;

static void pending_reset(void) { s_pending_len = 0; }

// --------------------------------------------------------------- socket ----
// 解析 "http://host[:port]/path"。
static bool split_url(const char *url, char *host, const size_t host_cap,
                      char *path, const size_t path_cap, int *port)
{
    if (strncmp(url, "http://", 7) != 0) return false;
    url += 7;
    const char *slash = strchr(url, '/');
    const char *colon = strchr(url, ':');
    if (!slash) return false;

    size_t host_len = (size_t)(slash - url);
    if (colon && colon < slash) {
        host_len = (size_t)(colon - url);
        *port = atoi(colon + 1);
        if (*port <= 0 || *port > 65535) return false;
    } else {
        *port = 80;
    }
    if (host_len == 0 || host_len >= host_cap) return false;
    memcpy(host, url, host_len);
    host[host_len] = '\0';

    const size_t path_len = strlen(slash);
    if (path_len >= path_cap) return false;
    memcpy(path, slash, path_len + 1);
    return true;
}

static int tcp_open(const char *host, const int port)
{
    const struct hostent *he = gethostbyname(host);
    if (!he || !he->h_addr_list[0]) {
        ESP_LOGW(TAG, "DNS 失败: %s", host);
        return -1;
    }
    const int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    struct timeval tv = { .tv_sec = 8, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)); // connect 阶段

    struct sockaddr_in dest = {
        .sin_family = AF_INET,
        .sin_port = htons((uint16_t)port),
        .sin_addr = *(struct in_addr *)he->h_addr_list[0],
    };
    if (connect(sock, (struct sockaddr *)&dest, sizeof(dest)) != 0) {
        ESP_LOGW(TAG, "TCP 失败 %s:%d", host, port);
        close(sock);
        return -1;
    }
    tv.tv_sec = 0;
    tv.tv_usec = RECV_MS * 1000; // 收流阶段：短超时，读不到就回去喂 DMA
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return sock;
}

// 发 GET、读完整响应头。成功返回 0：连接保持打开（净荷续用同一连接），
// *out_head 是 malloc 的头部（调用方 free），头后已到的净荷经 *out_body 带回
// （可能 NULL）。1xx 跳过；3xx 带出 Location 并保持打开（调用方自行关闭）。
static int fetch_head(const char *host, const int port, const char *path,
                      const int sock, char **out_head, size_t *out_head_len,
                      char **out_body, size_t *out_body_len, char *out_location,
                      const size_t location_cap)
{
    *out_head = NULL;
    *out_body = NULL;
    *out_head_len = *out_body_len = 0;
    out_location[0] = '\0';

    char request[512];
    const int rl = snprintf(request, sizeof(request),
                            "GET %s HTTP/1.1\r\n"
                            "Host: %s:%d\r\n"
                            "User-Agent: VLC/3.0 LibVLC/3.0\r\n"
                            "Accept: */*\r\n"
                            "Icy-MetaData: 0\r\n"
                            "Connection: close\r\n"
                            "\r\n",
                            path, host, port);
    if (rl <= 0 || (size_t)rl >= sizeof(request)) return -1;
    if (write(sock, request, (size_t)rl) != rl) return -1;

    char *buf = malloc(HEAD_MAX);
    if (!buf) return -1;
    size_t have = 0;
    const int64_t deadline = now_ms() + HEAD_DEADLINE_MS;
    for (;;) {
        if (have >= HEAD_MAX) { free(buf); return -1; }
        if (now_ms() > deadline) { free(buf); return -1; }
        const int n = read(sock, buf + have, HEAD_MAX - have);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            free(buf);
            return -1;
        }
        if (n == 0) { free(buf); return -1; }
        have += (size_t)n;

        const size_t head_len = mr_http_head_scan(buf, have, HEAD_MAX);
        if (head_len == 0) continue;
        if (head_len == SIZE_MAX) { free(buf); return -1; }

        mr_http_head_t head;
        if (!mr_http_head_parse(buf, head_len, &head)) { free(buf); return -1; }
        if (head.status < 200) { // 1xx：丢弃，找真正的响应头
            memmove(buf, buf + head_len, have - head_len);
            have -= head_len;
            continue;
        }

        *out_head = buf;
        *out_head_len = head_len;
        if (head.status >= 300 && head.status < 400 && head.location[0]) {
            snprintf(out_location, location_cap, "%s", head.location);
            return 0;
        }
        if (head.status != 200) { free(buf); *out_head = NULL; return -1; }

        const size_t body_have = have - head_len;
        if (body_have > 0) {
            char *body = malloc(body_have);
            if (body) {
                memcpy(body, buf + head_len, body_have);
                *out_body = body;
                *out_body_len = body_have;
            }
        }
        return 0;
    }
}

// 泵送式收流：先把残留字节塞进环形缓冲，塞完再读新块；环满就带着残留返回，
// 让调用方解码腾出空间后再来。返回 false = 断流/坏流。
static bool fill_ring(const int sock, const bool chunked, mr_chunker_t *ck,
                      int64_t *last_data_ms)
{
    for (int i = 0; i < READ_BURST; i++) {
        if (s_pending_len == 0) {
            const int n = read(sock, s_pending, sizeof(s_pending));
            if (n > 0) {
                *last_data_ms = now_ms();
                s_pending_len = (size_t)n;
            } else if (n == 0) {
                return false;                                  // 服务端关流
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return true;                                   // 暂无数据
            } else {
                return false;                                  // 真错误
            }
        }
        bool bad = false;
        const size_t used =
            ring_push_some(s_pending, s_pending_len, chunked, ck, &bad);
        if (bad) return false;
        if (used > 0) {
            memmove(s_pending, s_pending + used, s_pending_len - used);
            s_pending_len -= used;
        }
        if (s_pending_len > 0) return true; // 环满：带残留返回，先解码腾地方
    }
    return true;
}

// ---------------------------------------------------------------- 解码 ----
// 解一帧并写出。返回 1=出一帧；0=数据不足/找同步中；-1=写设备失败。
static int decode_write(void)
{
    const size_t have = ring_peek_linear(s_staging, STAGING_SIZE);
    if (have < 64) return 0;

    uint8_t *in = s_staging;
    int bytes_left = (int)have;
    const int sync = MP3FindSyncWord(s_staging, bytes_left);
    if (sync < 0) {
        ring_drop(have); // 整段无同步字：当垃圾丢（断流恢复期的正常代价）
        return 0;
    }
    if (sync > 0) {
        ring_drop((size_t)sync);
        in += sync;
        bytes_left -= sync;
    }

    uint8_t *inptr = in;
    const int64_t t_dec0 = esp_timer_get_time();
    const int err = MP3Decode(P.dec, &inptr, &bytes_left, s_pcm, 0);
    const int64_t t_dec1 = esp_timer_get_time();
    const int consumed = (int)have - sync - bytes_left;

    if (err != 0) {
        if (err == ERR_MP3_INDATA_UNDERFLOW || err == ERR_MP3_MAINDATA_UNDERFLOW) {
            return 0; // 下一帧还没到齐：等数据，不动指针
        }
        ring_drop((size_t)(sync + consumed + 2)); // 跳过坏帧重新找同步
        return 0;
    }

    ring_drop((size_t)(sync + consumed));

    MP3FrameInfo fi;
    MP3GetLastFrameInfo(P.dec, &fi);
    if (fi.samprate == 0 || fi.nChans == 0 || fi.outputSamps == 0) return 0;

    // ES8311 是单声道 codec：立体声流一律降混成单声道再送 I2S。
    // 真机实测：2ch 每帧写出 41ms（26ms 的音频），慢于实时 → 声音发闷拖沓；
    // 1ch 恰好实时。降混 (L+R)/2 保留全部内容，听感无损。
    if (fi.nChans == 2) {
        const int frames = fi.outputSamps / 2;
        for (int i = 0; i < frames; i++) {
            const int l = s_pcm[i * 2];
            const int r = s_pcm[i * 2 + 1];
            s_pcm[i] = (int16_t)((l + r) / 2);
        }
    }
    if (!P.format_set || P.fmt_hz != (uint32_t)fi.samprate || P.fmt_ch != 1) {
        if (bsp_audio_set_format((uint32_t)fi.samprate, 16, 1) != ESP_OK) {
            ESP_LOGE(TAG, "设置流格式失败 %uHz", fi.samprate);
            return -1;
        }
        P.format_set = true;
        P.fmt_hz = (uint32_t)fi.samprate;
        P.fmt_ch = 1;
        // 音效会把 codec 音量压到自己的基准（34%）；换到流格式后必须把电台
        // 音量显式调回用户档，否则电台比音效还小声（真机实测教训）。
        s_applied_codec_vol = 0; // 重新打开的 codec 需要重新下发音量
        ESP_LOGI(TAG, "流格式: %uHz %uch %ukbps 音量=%u%% free=%u largest=%u",
                 fi.samprate, fi.nChans, fi.bitrate / 1000,
                 (unsigned)mr_level_get(),
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    }

    xSemaphoreTake(P.lock, portMAX_DELAY);
    P.st.kbps = (uint16_t)(fi.bitrate / 1000);
    P.st.sample_hz = (uint32_t)fi.samprate;
    P.st.channels = (uint8_t)fi.nChans;
    xSemaphoreGive(P.lock);

    const int out_samples = fi.nChans == 2 ? fi.outputSamps / 2 : fi.outputSamps;
    // 音量：ES8311 的音量曲线 100 档才是 0dB（60 档约 -19dB，真机上小得听不清），
    // 所以 codec 吃满量程，用户档位改成软件增益：100% = 3 倍（约 +9.5dB）。
    if (s_applied_codec_vol != 100) {
        bsp_audio_set_volume(100);
        s_applied_codec_vol = 100;
    }
    // Q8 定点：gain_q8 = 档位/100 × MASTER × 256。整数百分比会把它算成 1/100
    // （真机踩过：60% × 3 / 100 = 1，声音被压小 100 倍）。
    const int32_t gain_q8 = (int32_t)mr_level_get() * MR_GAIN_MASTER * 256 / 100;
    if (gain_q8 != 256) {
        for (int i = 0; i < out_samples; i++) {
            int32_t v = ((int32_t)s_pcm[i] * gain_q8) >> 8;
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            s_pcm[i] = (int16_t)v;
        }
    }
    const int64_t t_wr0 = esp_timer_get_time();
    const esp_err_t wr = bsp_audio_write(s_pcm, (size_t)out_samples * sizeof(int16_t));
    const int64_t t_wr1 = esp_timer_get_time();
    // 真机诊断：解码与写出耗时 + PCM 峰值（峰值 0 = 解码出静音）。
    static int64_t next_timing;
    static int max_amp_seen;
    int amp = 0;
    for (int i = 0; i < out_samples; i++) {
        const int v = s_pcm[i] < 0 ? -s_pcm[i] : s_pcm[i];
        if (v > amp) amp = v;
    }
    if (amp > max_amp_seen) max_amp_seen = amp;
    if (t_wr1 >= next_timing) {
        next_timing = t_wr1 + 2000 * 1000;
        ESP_LOGD(TAG, "耗时: decode=%lldus write=%lldus samples=%d amp=%d peak=%d",
                 (long long)(t_dec1 - t_dec0), (long long)(t_wr1 - t_wr0),
                 fi.outputSamps, amp, max_amp_seen);
    }
    if (wr != ESP_OK) {
        // 偶发写出失败（DMA/codec 抖动）跳过本帧；连续失败才判死会话。
        static unsigned consecutive;
        if (++consecutive >= 20) {
            consecutive = 0;
            return -1;
        }
        return 0;
    }
    return 1;
}

// ------------------------------------------------------------- 一次会话 ----
// 与一个流地址周旋到结束。返回值见 session_end_t；redirect_out / msg_out
// 分别在 SESS_REDIRECT / SESS_FAIL 时有效。
static session_end_t play_one(const char *url, const uint32_t my_serial,
                              char *redirect_out, const size_t redirect_cap,
                              const char **msg_out)
{
    *msg_out = MSG_UNREACH;
    char host[96];
    char path[128];
    int port;
    if (!split_url(url, host, sizeof(host), path, sizeof(path), &port)) {
        *msg_out = MSG_NO_URL;
        return SESS_FAIL;
    }

    const int sock = tcp_open(host, port);
    if (sock < 0) return SESS_FAIL;

    char *head = NULL;
    char *body = NULL;
    size_t head_len = 0;
    size_t body_len = 0;
    char location[192];
    const int fr = fetch_head(host, port, path, sock, &head, &head_len,
                              &body, &body_len, location, sizeof(location));
    if (fr != 0) {
        close(sock);
        return SESS_FAIL;
    }
    if (location[0]) {
        snprintf(redirect_out, redirect_cap, "%s", location);
        free(head);
        close(sock);
        return SESS_REDIRECT;
    }

    mr_http_head_t h;
    mr_http_head_parse(head, head_len, &h);
    free(head);
    ESP_LOGI(TAG, "已连上: %s (%s)", h.content_type, h.icy_name);
    publish(MR_PLAY_BUFFERING, MSG_BUFFERING);

    ring_reset();
    pending_reset();
    P.format_set = false; // 新会话重新按流设置 codec（sfx 可能动过格式）
    mr_chunker_t ck;
    mr_chunker_init(&ck);
    if (body) {
        bool bad = false;
        const size_t used = ring_push_some((const uint8_t *)body, body_len,
                                           h.chunked, &ck, &bad);
        if (!bad && used < body_len) {
            // 头部之后已带进来的大块净荷塞不下：留给泵送路径。
            s_pending_len = body_len - used;
            memcpy(s_pending, body + used, s_pending_len);
        }
        free(body);
        if (bad) {
            close(sock);
            *msg_out = MSG_BADSTREAM;
            return SESS_FAIL;
        }
    }

    // 收流-解码主循环。
    bool playing = false;
    int64_t last_data = now_ms();
    for (;;) {
        if (P.serial != my_serial) { // 新请求/停止：立刻让位
            close(sock);
            return SESS_REQUEST;
        }
        const int64_t t_fill0 = now_ms();
        if (!fill_ring(sock, h.chunked, &ck, &last_data)) {
            close(sock);
            *msg_out = MSG_BADSTREAM;
            return SESS_FAIL;
        }
        const int64_t t_fill1 = now_ms();
        if (now_ms() - last_data > STARVE_MS) {
            close(sock);
            *msg_out = MSG_UNREACH;
            return SESS_FAIL;
        }

        if (!playing && P.ring_used >= START_MARK) {
            playing = true;
            publish(MR_PLAY_PLAYING, MSG_PLAYING);
        }
        if (playing) {
            // 解到低水位（剩一帧的空间）再回去收流：环形缓冲长期保持低位，
            // TCP 窗口不会堵，避免服务端判死断流（真机实测：每轮只解一帧
            // 时 6 秒就被踢）。
            int burst = 0;
            for (;;) {
                if (P.serial != my_serial) break;
                const int rc = decode_write();
                if (rc < 0) {
                    close(sock);
                    *msg_out = MSG_BADSTREAM;
                    return SESS_FAIL;
                }
                if (rc == 0) break;               // 数据不足/找同步
                burst++;
                if (burst >= DECODE_PER_LOOP) break; // 本次上限
                if (P.ring_used <= LOW_WATER) break; // 到低水位，回去收流
            }
            const int64_t t_dec1 = now_ms();
            // 真机诊断（卡顿定位用）：帧率、饿死次数、最长空转 + 分段耗时。
            static int64_t next_diag;
            static unsigned diag_frames;
            static unsigned diag_starve;
            static int64_t diag_max_gap;
            static int64_t diag_last_ok;
            static int64_t diag_max_fill, diag_max_dec;
            if (t_fill1 - t_fill0 > diag_max_fill) diag_max_fill = t_fill1 - t_fill0;
            if (t_dec1 - t_fill1 > diag_max_dec) diag_max_dec = t_dec1 - t_fill1;
            diag_frames += (unsigned)burst;
            if (burst == 0) diag_starve++;
            if (burst > 0) {
                if (diag_last_ok && now_ms() - diag_last_ok > diag_max_gap) {
                    diag_max_gap = now_ms() - diag_last_ok;
                }
                diag_last_ok = now_ms();
            }
            if (now_ms() >= next_diag) {
                next_diag = now_ms() + 4000;
                ESP_LOGD(TAG,
                         "诊断: ring=%u frames=%u starve=%u maxgap=%lldms "
                         "fill=%lldms dec=%lldms",
                         (unsigned)P.ring_used, diag_frames, diag_starve,
                         (long long)diag_max_gap, (long long)diag_max_fill,
                         (long long)diag_max_dec);
                diag_frames = diag_starve = 0;
                diag_max_gap = 0;
                diag_max_fill = diag_max_dec = 0;
            }
        } else {
            // 真机诊断：缓冲阶段每 2 秒报水位（确认出声后移除）。
            static int64_t next_buf_diag;
            if (now_ms() >= next_buf_diag) {
                next_buf_diag = now_ms() + 2000;
                ESP_LOGD(TAG, "缓冲诊断: ring=%u/%u", (unsigned)P.ring_used,
                         (unsigned)RING_SIZE);
            }
            vTaskDelay(pdMS_TO_TICKS(20)); // 缓冲中：小睡，让出 CPU
        }
    }
}

// ------------------------------------------------------------- 播放外壳 ----
// 重试与重定向的外层循环，直到成功进入长连接播放或宣告失败。
static void play_station(const uint8_t station)
{
    char url[192];
    snprintf(url, sizeof(url), "%s", MR_STATIONS[station].url);
    const uint32_t my_serial = P.serial; // 本次播放的请求代号
    unsigned retry = 0;
    unsigned redirect = 0;

    for (;;) {
        char location[192];
        const char *msg = MSG_UNREACH;
        const session_end_t end = play_one(url, my_serial, location,
                                           sizeof(location), &msg);

        if (end == SESS_REQUEST) return; // 新请求/停止：移交控制权
        if (end == SESS_REDIRECT) {
            if (++redirect > REDIRECT_MAX) {
                publish(MR_PLAY_ERROR, MSG_UNREACH);
                return;
            }
            snprintf(url, sizeof(url), "%s", location);
            ESP_LOGI(TAG, "重定向 -> %s", url);
            continue;
        }
        ESP_LOGW(TAG, "会话结束: %s（第 %u 次失败）", msg, retry + 1);
        if (++retry > RETRY_MAX) {
            publish(MR_PLAY_ERROR, msg);
            return;
        }
        publish_retry(retry, RETRY_MAX);
        vTaskDelay(pdMS_TO_TICKS(1000 << (retry - 1)));
        if (P.serial != my_serial) return; // 退避期间来了新请求
    }
}

static void player_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (!P.req_valid) {
            if (P.st.state != MR_PLAY_IDLE) publish(MR_PLAY_IDLE, NULL);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // 消费请求：置 req_valid=false 后，serial 只会被下一次用户动作推进。
        // play_station 在入口拍下当前 serial 作为「本次播放」的代号。
        const uint8_t station = P.req_station;
        P.req_valid = false;

        xSemaphoreTake(P.lock, portMAX_DELAY);
        P.st.station = station;
        P.st.msg[0] = '\0';
        P.st.kbps = 0;
        P.st.sample_hz = 0;
        P.st.channels = 0;
        xSemaphoreGive(P.lock);

        play_station(station);
        ring_reset();
    }
}

// ---------------------------------------------------------------- 对外 ----
void mr_player_start(void)
{
    if (P.task) return;
    P.lock = xSemaphoreCreateMutex();
    if (!P.lock) {
        ESP_LOGE(TAG, "互斥量创建失败");
        return;
    }
    P.ring = malloc(RING_SIZE);
    if (!P.ring) {
        ESP_LOGE(TAG, "环形缓冲分配失败");
        vSemaphoreDelete(P.lock);
        P.lock = NULL;
        return;
    }
    P.dec = MP3InitDecoder();
    if (!P.dec) {
        ESP_LOGE(TAG, "MP3 解码器初始化失败: free=%u largest=%u",
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        free(P.ring);
        P.ring = NULL;
        vSemaphoreDelete(P.lock);
        P.lock = NULL;
        return;
    }
    ESP_LOGI(TAG, "解码器就绪: free=%u largest=%u",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    P.dec_open = true;
    if (xTaskCreate(player_task, "mr_play", PLAYER_TASK_STACK, NULL,
                    PLAYER_TASK_PRIO, &P.task) != pdPASS) {
        ESP_LOGE(TAG, "播放任务创建失败");
        MP3FreeDecoder(P.dec);
        P.dec_open = false;
        free(P.ring);
        P.ring = NULL;
        vSemaphoreDelete(P.lock);
        P.lock = NULL;
    }
}

void mr_player_play(const uint8_t station_index)
{
    if (!P.task || !mr_station_valid(station_index)) return;
    P.req_station = station_index;
    P.req_valid = true;
    P.serial++;
}

void mr_player_stop(void)
{
    if (!P.task) return;
    P.req_valid = false;
    P.serial++;
}

bool mr_player_is_streaming(void)
{
    if (!P.lock) return false;
    xSemaphoreTake(P.lock, portMAX_DELAY);
    const mr_play_state_t state = P.st.state;
    xSemaphoreGive(P.lock);
    return state == MR_PLAY_CONNECT || state == MR_PLAY_BUFFERING ||
           state == MR_PLAY_PLAYING;
}

void mr_player_status(mr_play_status_t *out)
{
    if (!out) return;
    if (!P.lock) {
        memset(out, 0, sizeof(*out));
        return;
    }
    xSemaphoreTake(P.lock, portMAX_DELAY);
    *out = P.st;
    xSemaphoreGive(P.lock);
}

void mr_player_set_volume(const uint8_t level)
{
    s_level = level;
    bsp_audio_set_volume(mr_volume_scale(MR_VOLUME_BASE_RADIO, level));
}
