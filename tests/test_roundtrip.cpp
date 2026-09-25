// VelaPack - 单元测试（往返一致性 / CRC 检错 / 空文件）
#include "vlp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail = 0;
static void check(int cond, const char* msg) {
    if (!cond) { printf("FAIL: %s\n", msg); ++fail; }
}

static void test_roundtrip(int level) {
    // 测试 1：可压缩文本（C 源码重复率高）
    const char* text =
        "The quick brown fox jumps over the lazy dog. "
        "The quick brown fox jumps over the lazy dog. "
        "Pack my box with five dozen liquor jugs. ";
    size_t len = strlen(text);
    uint8_t* comp = NULL;
    size_t clen = 0;
    int r = vlp_compress((const uint8_t*)text, len, &comp, &clen, level);
    check(r == VLP_OK, "compress text");
    check(clen > 0, "compressed non-empty");

    uint8_t* dec = NULL;
    size_t dlen = 0;
    r = vlp_decompress(comp, clen, &dec, &dlen);
    check(r == VLP_OK, "decompress text");
    check(dlen == len, "decompressed length");
    check(memcmp(text, dec, len) == 0, "decompressed content");

    free(comp);
    free(dec);
}

static void test_empty() {
    uint8_t* comp = NULL;
    size_t clen = 0;
    int r = vlp_compress(NULL, 0, &comp, &clen, 6);
    check(r == VLP_OK, "compress empty");
    check(clen > 0, "compressed empty is a valid container");

    uint8_t* dec = NULL;
    size_t dlen = 0;
    r = vlp_decompress(comp, clen, &dec, &dlen);
    check(r == VLP_OK, "decompress empty");
    check(dlen == 0, "empty roundtrip length");
    free(comp);
    free(dec);
}

static void test_random() {
    size_t len = 4096;
    uint8_t* data = (uint8_t*)malloc(len);
    for (size_t i = 0; i < len; ++i)
        data[i] = (uint8_t)(i * 7 + (i >> 3));

    uint8_t* comp = NULL;
    size_t clen = 0;
    int r = vlp_compress(data, len, &comp, &clen, 6);
    check(r == VLP_OK, "compress random");

    uint8_t* dec = NULL;
    size_t dlen = 0;
    r = vlp_decompress(comp, clen, &dec, &dlen);
    check(r == VLP_OK, "decompress random");
    check(dlen == len, "decompressed random length");
    check(memcmp(data, dec, len) == 0, "decompressed random content");

    free(data); free(comp); free(dec);
}

// 无元数据时文件头长度：16B 固定字段 + 4B 头CRC
static const size_t kHeaderLenNoMeta = 20;
// 数据块头长度：4B "VLPB" + 20B 字段 + 4B CRC
static const size_t kBlockHeaderLen = 28;

// 损坏测试 1：翻转文件头 CRC 字段（偏移 16）。
// 计算值不变、存储值改变，CRC 必然不匹配，结果完全确定。
static void test_header_crc_error() {
    const char* text = "Hello VelaPack!";
    size_t len = strlen(text);
    uint8_t* comp = NULL;
    size_t clen = 0;
    int r = vlp_compress((const uint8_t*)text, len, &comp, &clen, 6);
    check(r == VLP_OK && clen >= 16, "prepare header crc test");

    comp[16] ^= 0x55;
    uint8_t* dec = NULL;
    size_t dlen = 0;
    r = vlp_decompress(comp, clen, &dec, &dlen);
    check(r == VLP_ERR_CRC, "detect header CRC corruption");

    free(comp);
    free(dec);
}

// 损坏测试 2：翻转第一个块负载的首字节。
// 该位置被 crc32_stored 覆盖（CRC32 对任意非零差分均产生非零差），
// 不依赖压缩输出布局，也不会落到 padding 上。
static void test_payload_crc_error() {
    const char* text =
        "Hello VelaPack! repeated-content repeated-content repeated-content";
    size_t len = strlen(text);
    const size_t payload_off = kHeaderLenNoMeta + kBlockHeaderLen;

    uint8_t* comp = NULL;
    size_t clen = 0;
    int r = vlp_compress((const uint8_t*)text, len, &comp, &clen, 6);
    check(r == VLP_OK && clen > payload_off, "prepare payload crc test");

    comp[payload_off] ^= 0x55;
    uint8_t* dec = NULL;
    size_t dlen = 0;
    r = vlp_decompress(comp, clen, &dec, &dlen);
    check(r == VLP_ERR_CRC || r == VLP_ERR_DATA, "detect payload corruption");

    free(comp);
    free(dec);
}

int main() {
    printf("VelaPack unit tests\n");
    for (int lvl = 1; lvl <= 9; ++lvl) test_roundtrip(lvl);
    test_empty();
    test_random();
    test_header_crc_error();
    test_payload_crc_error();
    if (fail) {
        printf("\n%d test(s) FAILED\n", fail);
        return 1;
    }
    printf("\nAll tests PASSED\n");
    return 0;
}
