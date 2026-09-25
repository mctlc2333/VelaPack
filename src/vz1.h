// VelaPack - VZ1 编解码器
// 单块压缩算法：LZ77（64KiB 窗口、哈希链、惰性匹配）
// + 规范霍夫曼编码（字面量/长度共用字母表，距离独立字母表）。
//
// 符号字母表（LL，共 286 个符号）：
//   0..255   字面量
//   256      块结束（EOB）
//   257..285 匹配长度码（29 个，覆盖长度 4..258，含附加位）
// 距离字母表（D，共 32 个符号）：
//   0..31    距离码（覆盖 1..65536，含附加位）
//
// 块负载布局：
//   [143 字节] LL 码长表（286 个符号 × 4bit）
//   [16  字节] D  码长表（32  个符号 × 4bit）
//   [变长]     MSB-first 位流，以 EOB 结束
#ifndef VLP_VZ1_H
#define VLP_VZ1_H

#include <stdint.h>
#include <stddef.h>
#include <vector>

namespace vlp {

// 压缩级别 1..9：1 最快，9 压缩率最高
bool vz1_compress(const uint8_t* in, size_t inlen, int level,
                  std::vector<uint8_t>& out);

// 解压到 out（outlen 为期望的原始长度）；数据损坏返回 false
bool vz1_decompress(const uint8_t* in, size_t inlen,
                    uint8_t* out, size_t outlen);

} // namespace vlp

#endif // VLP_VZ1_H
