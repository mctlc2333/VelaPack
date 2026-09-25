// VelaPack - .vlp 容器格式层
// 负责文件头 / 元数据 / 数据块 / 页脚的读写与 CRC 校验。
// 布局详见 docs/SPEC.md。
#ifndef VLP_FORMAT_H
#define VLP_FORMAT_H

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>
#include <map>

namespace vlp {

// 魔数：文件头 / 数据块 / 页脚各自独立，记录类型判别不存在歧义
extern const uint8_t VLP_MAGIC[4];       // 文件头 "VLP1"
extern const uint8_t VLP_BLOCK_MAGIC[4]; // 数据块 "VLPB"
extern const uint8_t VLP_FOOTER_MAGIC[4];// 页脚   "VLPE"

// 块头固定大小：4 魔数 + 20 字段 + 4 CRC = 28 字节
const uint32_t VLP_BLOCK_HEADER_SIZE = 28;

const uint8_t VLP_VERSION_MAJOR = 1;
const uint8_t VLP_VERSION_MINOR = 0;

// 编解码器标识
const uint16_t VLP_CODEC_STORE = 0; // 原样存储
const uint16_t VLP_CODEC_VZ1 = 1;   // LZ77 + Huffman

// 默认块大小：1 MiB
const uint32_t VLP_DEFAULT_BLOCK_SIZE = 1u << 20;
const uint32_t VLP_MAX_BLOCK_SIZE = 1u << 26; // 64 MiB 上限

// 错误码（数值与 C API 的 VLP_* 宏一一对应）
enum VlpError {
    kOk = 0,
    kErrIO = 1,          // 读写失败
    kErrFormat = 2,      // 不是合法的 .vlp 文件
    kErrCRC = 3,         // 校验和不匹配
    kErrUnsupported = 4, // 版本/编解码器不受支持
    kErrData = 5,        // 压缩数据损坏
    kErrArg = 6          // 参数错误
};

// 抽象读写接口：文件与内存缓冲共用同一套格式逻辑
struct Reader {
    virtual ~Reader() {}
    // 返回实际读取的字节数（0 = EOF）
    virtual size_t read(uint8_t* buf, size_t len) = 0;
};

struct Writer {
    virtual ~Writer() {}
    virtual bool write(const uint8_t* buf, size_t len) = 0;
};

struct CompressOptions {
    uint32_t block_size; // 每块原始字节数
    int level;           // 1..9
    std::map<std::string, std::string> metadata; // 可选元数据
    CompressOptions() : block_size(VLP_DEFAULT_BLOCK_SIZE), level(6) {}
};

struct ArchiveInfo {
    uint8_t version_major;
    uint8_t version_minor;
    uint32_t block_size;
    uint64_t orig_size;   // 原始总大小（来自页脚）
    uint64_t comp_size;   // 压缩负载总大小（来自页脚）
    uint32_t block_count;
    std::map<std::string, std::string> metadata;
};

// 压缩：从 in 读取全部数据，写入 .vlp 流到 out
VlpError compress_stream(Reader& in, Writer& out, const CompressOptions& opt);

// 解压：读取 .vlp 流，写出原始数据。
// 若 out 为 NULL 则仅校验（vlp t 命令使用）。
VlpError decompress_stream(Reader& in, Writer* out, ArchiveInfo* info);

} // namespace vlp

#endif // VLP_FORMAT_H
