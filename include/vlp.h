// VelaPack - C API 头文件（vlp.h）
// 所有函数均遵循 C 链接约定，可直接从 C/C++ 及其他语言 FFI 调用。

#ifndef VLP_H
#define VLP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 错误码（与 C++ 内部一致）
#define VLP_OK             0
#define VLP_ERR_IO         1
#define VLP_ERR_FORMAT     2
#define VLP_ERR_CRC        3
#define VLP_ERR_UNSUPPORTED 4
#define VLP_ERR_DATA       5
#define VLP_ERR_ARG        6

// 压缩级别
#define VLP_LEVEL_FASTEST 1
#define VLP_LEVEL_BALANCED 6
#define VLP_LEVEL_BEST    9

// 元数据项（用于 vlp_file_compress_with_meta）
typedef struct vlp_meta_item {
    const char* key;
    const char* value;
} vlp_meta_item_t;

// 归档信息（用于 vlp_file_info）
typedef struct vlp_archive_info {
    uint32_t block_size;
    uint64_t orig_size;
    uint64_t comp_size;
    uint32_t block_count;
    // 元数据（若不需要可传 NULL）
    vlp_meta_item_t* meta;
    size_t meta_count;
} vlp_archive_info_t;

// ---------------- 内存级 API ----------------

// 压缩字节数组。成功后 *out 指向堆分配的缓冲区，调用者需用 free() 释放；
// outlen 为输出字节数。level 范围 1..9。
int vlp_compress(const uint8_t* in, size_t inlen,
                 uint8_t** out, size_t* outlen, int level);

// 解压字节数组。
int vlp_decompress(const uint8_t* in, size_t inlen,
                   uint8_t** out, size_t* outlen);

// ---------------- 文件级 API ----------------

// 压缩文件
int vlp_file_compress(const char* src, const char* dst, int level);

// 带元数据的压缩文件
int vlp_file_compress_meta(const char* src, const char* dst, int level,
                           const vlp_meta_item_t* meta, size_t meta_count);

// 解压文件
int vlp_file_decompress(const char* src, const char* dst);

// 测试文件完整性（不输出数据）。info 可为 NULL。
int vlp_file_test(const char* path, vlp_archive_info_t* info);

// 释放由 vlp_file_test 分配的 info->meta 数组
void vlp_archive_info_free(vlp_archive_info_t* info);

// ---------------- 辅助 ----------------

// 返回错误码对应的人类可读描述
const char* vlp_strerror(int err);

// 库版本字符串（如 "1.0.0"）
const char* vlp_version(void);

#ifdef __cplusplus
}
#endif

#endif // VLP_H
