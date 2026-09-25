// VelaPack - 规范霍夫曼编码（canonical Huffman）
// 码长上限 15 位；码表只存储每个符号的码长（4 bit/符号），
// 解码端按规范规则重建编码：同码长按符号升序连续分配码字。
#ifndef VLP_HUFFMAN_H
#define VLP_HUFFMAN_H

#include <stdint.h>
#include <vector>

namespace vlp {

const int VLP_HUFF_MAX_BITS = 15;

// 由符号频率构建受限码长（0 表示符号未使用）。
// n 为符号个数，maxbits 必须 <= VLP_HUFF_MAX_BITS。
void huffman_build_lengths(const uint32_t* freq, int n, int maxbits,
                           uint8_t* lengths);

// 由码长生成规范码字（MSB-first 对齐的整数值）。
void huffman_build_codes(const uint8_t* lengths, int n, uint32_t* codes);

class BitWriter;
class BitReader;

class HuffmanEncoder {
public:
    // lengths 必须已经过 huffman_build_lengths 处理
    void init(const uint8_t* lengths, int n);
    void encode(BitWriter& bw, int symbol) const; // 写码字（MSB-first）
    const uint8_t* lengths() const { return lengths_.data(); }
    int size() const { return n_; }

private:
    std::vector<uint8_t> lengths_;
    std::vector<uint32_t> codes_;
    int n_ = 0;
};

class HuffmanDecoder {
public:
    // 返回 false 表示码长表非法（不满足 Kraft 不等式或为空表）
    bool init(const uint8_t* lengths, int n);
    // 解码一个符号；输入损坏时返回 -1
    int decode(BitReader& br) const;

private:
    // puff 风格：counts[len] 为该码长符号数，symbols 按 (len, symbol) 排序
    uint16_t counts_[VLP_HUFF_MAX_BITS + 1] = {0};
    std::vector<uint16_t> symbols_;
    int n_ = 0;
};

} // namespace vlp

#endif // VLP_HUFFMAN_H
