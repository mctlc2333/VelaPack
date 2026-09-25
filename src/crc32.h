// VelaPack - CRC32 (IEEE 802.3, polynomial 0xEDB88320)
// 用于文件头、数据块、页脚的完整性校验。
#ifndef VLP_CRC32_H
#define VLP_CRC32_H

#include <stdint.h>
#include <stddef.h>

namespace vlp {

class Crc32 {
public:
    Crc32() : crc_(0xFFFFFFFFu) {}

    void update(const uint8_t* data, size_t len) {
        const uint32_t* t = table();
        uint32_t c = crc_;
        for (size_t i = 0; i < len; ++i)
            c = t[(c ^ data[i]) & 0xFF] ^ (c >> 8);
        crc_ = c;
    }

    uint32_t value() const { return crc_ ^ 0xFFFFFFFFu; }

    // 一次性计算
    static uint32_t compute(const uint8_t* data, size_t len) {
        Crc32 c;
        c.update(data, len);
        return c.value();
    }

private:
    uint32_t crc_;

    // C++11 magic static：首次调用时由运行时保证线程安全的一次性建表，
    // 多线程并发构造 Crc32 也不会产生 data race。
    static const uint32_t* table() {
        static const uint32_t* const t = build_table();
        return t;
    }

    static const uint32_t* build_table() {
        static uint32_t tbl[256];
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            tbl[i] = c;
        }
        return tbl;
    }
};

} // namespace vlp

#endif // VLP_CRC32_H
