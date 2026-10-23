#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "mr_http.h"

static void feed_all(mr_chunker_t *ck, const char *in, size_t n,
                     uint8_t *out, size_t cap, size_t *produced)
{
    const uint8_t *p = (const uint8_t *)in;
    size_t left = n;
    size_t room = cap;
    *produced = 0;
    // 模拟设备端调用方式：循环喂入直到输入耗尽或输出满。
    while (left > 0 && room > 0) {
        const size_t out_before = room;
        mr_chunker_feed(ck, &p, &left, out + (cap - room), &room);
        *produced += out_before - room;
        if (ck->state == MR_CK_BAD || ck->state == MR_CK_DONE) break;
    }
}

int main(void)
{
    // ---------------- 头部扫描 ----------------
    const char *full =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: audio/mpeg\r\n"
        "icy-br:128\r\n"
        "\r\nBODY";
    assert(mr_http_head_scan(full, strlen(full), 2048) == strlen(full) - 4);

    // 半截头：未完成返回 0。
    assert(mr_http_head_scan("HTTP/1.1 200 OK\r\nCon", 19, 2048) == 0);

    // 上限兜底：缓冲超限还没找到终点。
    char junk[600];
    memset(junk, 'A', sizeof(junk));
    assert(mr_http_head_scan(junk, sizeof(junk), 256) == SIZE_MAX);

    // ---------------- 头部解析 ----------------
    mr_http_head_t h;
    assert(mr_http_head_parse(full, strlen(full) - 4, &h));
    assert(h.status == 200);
    assert(!h.chunked);
    assert(h.content_length == -1);
    assert(strcmp(h.content_type, "audio/mpeg") == 0);
    assert(h.icy_br == 128);
    assert(h.icy_name[0] == '\0');

    const char *icy =
        "HTTP/1.0 200 OK\r\n"
        "content-type: AUDIO/MPEG;codec=MP3\r\n"
        "ICY-Name:CNR-1 中国之声\r\n"
        "icy-br: 64 \r\n"
        "Content-Length: 123456\r\n"
        "\r\n";
    assert(mr_http_head_parse(icy, strlen(icy) - 2, &h));
    assert(strcmp(h.content_type, "audio/mpeg") == 0); // 小写化 + 截分号
    assert(strcmp(h.icy_name, "CNR-1 中国之声") == 0);
    assert(h.icy_br == 64);
    assert(h.content_length == 123456);

    const char *redir =
        "HTTP/1.1 302 Found\r\n"
        "Location: http://a.example.com/live.mp3\r\n"
        "Content-Length: 0\r\n"
        "\r\n";
    assert(mr_http_head_parse(redir, strlen(redir) - 2, &h));
    assert(h.status == 302);
    assert(strcmp(h.location, "http://a.example.com/live.mp3") == 0);

    // 100-Continue 后跟真头部：扫描应找到第一段；解析失败后调用方跳过。
    const char *cont =
        "HTTP/1.1 100 Continue\r\n\r\n"
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: audio/aac\r\n"
        "\r\n";
    const size_t first = mr_http_head_scan(cont, strlen(cont), 2048);
    assert(first == 25);
    assert(mr_http_head_parse(cont, first, &h));
    assert(h.status == 100);
    // 跳过 1xx 后再解析第二段。
    const char *second = cont + first;
    assert(mr_http_head_parse(second, strlen(second) - 2, &h));
    assert(h.status == 200);
    assert(strcmp(h.content_type, "audio/aac") == 0);

    // 坏头部。
    assert(!mr_http_head_parse("NOT HTTP", 8, &h));
    assert(!mr_http_head_parse("HTTP/1.1 abc\r\n\r\n", 16, &h));
    assert(!mr_http_head_parse(full, 8, &h));

    // ---------------- chunked：整段 ----------------
    mr_chunker_t ck;
    uint8_t out[256];
    size_t produced;
    const char *chunked = "4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n";
    mr_chunker_init(&ck);
    feed_all(&ck, chunked, strlen(chunked), out, sizeof(out), &produced);
    assert(ck.state == MR_CK_DONE);
    assert(produced == 9);
    assert(memcmp(out, "Wikipedia", 9) == 0);

    // ---------------- chunked：逐字节喂（分片最恶劣形态）----------------
    mr_chunker_init(&ck);
    produced = 0;
    size_t room = sizeof(out);
    for (size_t i = 0; i < strlen(chunked); i++) {
        const uint8_t *p = (const uint8_t *)&chunked[i];
        size_t one = 1;
        mr_chunker_feed(&ck, &p, &one, out + produced, &room);
        produced = sizeof(out) - room;
        assert(ck.state != MR_CK_BAD);
    }
    assert(ck.state == MR_CK_DONE);
    assert(produced == 9);
    assert(memcmp(out, "Wikipedia", 9) == 0);

    // ---------------- chunked：输出容量不足（环满续传语义）----------------
    mr_chunker_init(&ck);
    produced = 0;
    memset(out, 0, sizeof(out));
    {
        const uint8_t *p = (const uint8_t *)chunked;
        size_t left = strlen(chunked);
        size_t room2 = 5; // 只给 5 字节空间
        while (left > 0 && room2 > 0) {
            mr_chunker_feed(&ck, &p, &left, out + produced, &room2);
        }
        assert(produced == 5 - room2 || room2 == 0);
        // 第一轮只产出 5 字节（"Wikipe"截断为5）。
        assert(room2 == 0);
        assert(memcmp(out, "Wikip", 5) == 0);
        assert(ck.state == MR_CK_DATA);
        // 继续给空间：剩余全部产出。
        room2 = sizeof(out) - 5;
        while (left > 0 && room2 > 0) {
            mr_chunker_feed(&ck, &p, &left, out + 5, &room2);
        }
        assert(left == 0);
        assert(memcmp(out, "Wikipedia", 9) == 0);
        assert(ck.state == MR_CK_DONE);
    }

    // ---------------- chunked：坏流 ----------------
    const char *bad = "Z\r\nWiki\r\n";
    mr_chunker_init(&ck);
    feed_all(&ck, bad, strlen(bad), out, sizeof(out), &produced);
    assert(ck.state == MR_CK_BAD);

    // chunk 扩展（v1 不支持）：';' 后非法。
    const char *ext = "4;name=v\r\nWiki\r\n0\r\n\r\n";
    mr_chunker_init(&ck);
    feed_all(&ck, ext, strlen(ext), out, sizeof(out), &produced);
    assert(ck.state == MR_CK_BAD);

    // 尾注：0 长度后带 trailer 头，滚动窗口扫到空行结束。
    const char *trailer = "5\r\nhello\r\n0\r\nX-T: 1\r\n\r\nTAIL";
    mr_chunker_init(&ck);
    feed_all(&ck, trailer, strlen(trailer), out, sizeof(out), &produced);
    assert(ck.state == MR_CK_DONE);
    assert(produced == 5);
    assert(memcmp(out, "hello", 5) == 0);

    printf("test_mr_http OK\n");
    return 0;
}
