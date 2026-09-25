// VelaPack - 位流读写（MSB-first：每字节从最高位开始填充）
// 规范约定：所有 Huffman 编码与附加位均按 MSB-first 顺序写入；
// 最后一个字节不足 8 位时在低位补 0。
#ifndef VLP_BITIO_H
#define VLP_BITIO_H

#include <stdint.h>
#include <vector>
#include <stddef.h>

namespace vlp {

class BitWriter {
public:
    BitWriter() : cur_(0), nbits_(0) {}

    // 写入 val 的低 n 位，先写高位
    void put_bits(uint32_t val, int n) {
        for (int i = n - 1; i >= 0; --i)
            put_bit((val >> i) & 1u);
    }

    void put_bit(uint32_t b) {
        cur_ = (uint8_t)((cur_ << 1) | (b & 1u));
        if (++nbits_ == 8) {
            out_.push_back(cur_);
            cur_ = 0;
            nbits_ = 0;
        }
    }

    // 收尾：补零对齐到字节边界
    void flush() {
        if (nbits_ > 0) {
            cur_ = (uint8_t)(cur_ << (8 - nbits_));
            out_.push_back(cur_);
            cur_ = 0;
            nbits_ = 0;
        }
    }

    const std::vector<uint8_t>& data() const { return out_; }
    size_t size() const { return out_.size(); }

private:
    std::vector<uint8_t> out_;
    uint8_t cur_;
    int nbits_;
};

class BitReader {
public:
    BitReader(const uint8_t* data, size_t len)
        : data_(data), len_(len), pos_(0), bitpos_(0), overread_(false) {}

    // 读取 1 位；越界时返回 0 并置 overread 标志
    uint32_t get_bit() {
        if (pos_ >= len_) {
            overread_ = true;
            return 0;
        }
        uint32_t b = (data_[pos_] >> (7 - bitpos_)) & 1u;
        if (++bitpos_ == 8) {
            bitpos_ = 0;
            ++pos_;
        }
        return b;
    }

    // 读取 n 位（MSB-first）
    uint32_t get_bits(int n) {
        uint32_t v = 0;
        for (int i = 0; i < n; ++i)
            v = (v << 1) | get_bit();
        return v;
    }

    bool overread() const { return overread_; }

private:
    const uint8_t* data_;
    size_t len_;
    size_t pos_;
    int bitpos_;
    bool overread_;
};

} // namespace vlp

#endif // VLP_BITIO_H
