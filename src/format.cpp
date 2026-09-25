#include "format.h"
#include "crc32.h"
#include "vz1.h"
#include <string.h>

namespace vlp {

const uint8_t VLP_MAGIC[4] = {'V', 'L', 'P', '1'};
const uint8_t VLP_BLOCK_MAGIC[4] = {'V', 'L', 'P', 'B'};
const uint8_t VLP_FOOTER_MAGIC[4] = {'V', 'L', 'P', 'E'};

// ---------- 小端编码辅助 ----------

static void put16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back((uint8_t)(x & 0xFF));
    v.push_back((uint8_t)(x >> 8));
}
static void put32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back((uint8_t)(x >> (8 * i)));
}
static void put64(std::vector<uint8_t>& v, uint64_t x) {
    for (int i = 0; i < 8; ++i) v.push_back((uint8_t)(x >> (8 * i)));
}

class BufReader {
public:
    BufReader(const uint8_t* p, size_t n) : p_(p), n_(n), off_(0) {}
    bool u8(uint8_t& x) {
        if (off_ + 1 > n_) return false;
        x = p_[off_++];
        return true;
    }
    bool u16(uint16_t& x) {
        if (off_ + 2 > n_) return false;
        x = (uint16_t)(p_[off_] | (p_[off_ + 1] << 8));
        off_ += 2;
        return true;
    }
    bool u32(uint32_t& x) {
        if (off_ + 4 > n_) return false;
        x = 0;
        for (int i = 0; i < 4; ++i) x |= (uint32_t)p_[off_ + i] << (8 * i);
        off_ += 4;
        return true;
    }
    bool u64(uint64_t& x) {
        if (off_ + 8 > n_) return false;
        x = 0;
        for (int i = 0; i < 8; ++i) x |= (uint64_t)p_[off_ + i] << (8 * i);
        off_ += 8;
        return true;
    }
    bool bytes(const uint8_t*& p, size_t len) {
        if (off_ + len > n_) return false;
        p = p_ + off_;
        off_ += len;
        return true;
    }
private:
    const uint8_t* p_;
    size_t n_;
    size_t off_;
};

static bool read_exact(Reader& r, uint8_t* buf, size_t len) {
    size_t got = 0;
    while (got < len) {
        size_t n = r.read(buf + got, len - got);
        if (n == 0) return false;
        got += n;
    }
    return true;
}

// ---------- 压缩 ----------

VlpError compress_stream(Reader& in, Writer& out, const CompressOptions& opt) {
    if (opt.block_size == 0 || opt.block_size > VLP_MAX_BLOCK_SIZE)
        return kErrArg;

    // ---- 文件头 ----
    uint16_t flags = opt.metadata.empty() ? 0 : 1;
    std::vector<uint8_t> hdr;
    hdr.insert(hdr.end(), VLP_MAGIC, VLP_MAGIC + 4);
    hdr.push_back(VLP_VERSION_MAJOR);
    hdr.push_back(VLP_VERSION_MINOR);
    put16(hdr, flags);
    put32(hdr, opt.block_size);

    // 元数据序列化
    std::vector<uint8_t> meta;
    if (!opt.metadata.empty()) {
        put32(meta, (uint32_t)opt.metadata.size());
        for (std::map<std::string, std::string>::const_iterator it =
                 opt.metadata.begin(); it != opt.metadata.end(); ++it) {
            if (it->first.size() > 0xFFFF) return kErrArg;
            put16(meta, (uint16_t)it->first.size());
            meta.insert(meta.end(), it->first.begin(), it->first.end());
            put32(meta, (uint32_t)it->second.size());
            meta.insert(meta.end(), it->second.begin(), it->second.end());
        }
    }
    put32(hdr, (uint32_t)meta.size());
    hdr.insert(hdr.end(), meta.begin(), meta.end());
    put32(hdr, Crc32::compute(hdr.data(), hdr.size()));

    if (!out.write(hdr.data(), hdr.size())) return kErrIO;

    // ---- 数据块 ----
    std::vector<uint8_t> raw(opt.block_size);
    std::vector<uint8_t> comp;
    uint64_t orig_total = 0, comp_total = 0;
    uint32_t block_count = 0;

    for (;;) {
        size_t n = 0;
        // 填满一个块
        while (n < opt.block_size) {
            size_t r = in.read(raw.data() + n, opt.block_size - n);
            if (r == 0) break;
            n += r;
        }
        if (n == 0) break;

        uint16_t codec;
        const uint8_t* payload;
        uint32_t stored_size;
        if (!vz1_compress(raw.data(), n, opt.level, comp)) return kErrData;
        if (comp.size() < n) {
            codec = VLP_CODEC_VZ1;
            payload = comp.data();
            stored_size = (uint32_t)comp.size();
        } else {
            codec = VLP_CODEC_STORE; // 压缩无收益，原样存储
            payload = raw.data();
            stored_size = (uint32_t)n;
        }

        // 块头（28B）：
        //   "VLPB", orig_size, stored_size, codec, reserved,
        //   crc_orig, crc_stored, block_hdr_crc（覆盖前 24B，含魔数）
        std::vector<uint8_t> bh;
        bh.insert(bh.end(), VLP_BLOCK_MAGIC, VLP_BLOCK_MAGIC + 4);
        put32(bh, (uint32_t)n);
        put32(bh, stored_size);
        put16(bh, codec);
        put16(bh, 0);
        put32(bh, Crc32::compute(raw.data(), n));
        put32(bh, Crc32::compute(payload, stored_size));
        put32(bh, Crc32::compute(bh.data(), bh.size()));

        if (!out.write(bh.data(), bh.size())) return kErrIO;
        if (!out.write(payload, stored_size)) return kErrIO;

        orig_total += n;
        comp_total += stored_size;
        ++block_count;
    }

    // ---- 页脚 ----
    std::vector<uint8_t> ft;
    ft.insert(ft.end(), VLP_FOOTER_MAGIC, VLP_FOOTER_MAGIC + 4);
    put64(ft, orig_total);
    put64(ft, comp_total);
    put32(ft, block_count);
    put32(ft, Crc32::compute(ft.data(), ft.size()));
    if (!out.write(ft.data(), ft.size())) return kErrIO;

    return kOk;
}

// ---------- 解压 ----------

static VlpError parse_metadata(const uint8_t* p, size_t len,
                               std::map<std::string, std::string>& out_map) {
    BufReader br(p, len);
    uint32_t count;
    if (!br.u32(count)) return kErrFormat;
    for (uint32_t i = 0; i < count; ++i) {
        uint16_t klen;
        uint32_t vlen;
        const uint8_t *kp, *vp;
        if (!br.u16(klen)) return kErrFormat;
        if (!br.bytes(kp, klen)) return kErrFormat;
        if (!br.u32(vlen)) return kErrFormat;
        if (!br.bytes(vp, vlen)) return kErrFormat;
        out_map[std::string((const char*)kp, klen)] =
            std::string((const char*)vp, vlen);
    }
    return kOk;
}

VlpError decompress_stream(Reader& in, Writer* out, ArchiveInfo* info) {
    // ---- 文件头（固定 16 字节 + 元数据 + 4 字节 CRC） ----
    uint8_t fixed[16];
    if (!read_exact(in, fixed, sizeof(fixed))) return kErrFormat;
    BufReader br(fixed, sizeof(fixed));

    uint8_t major, minor;
    uint16_t flags;
    uint32_t block_size, meta_len;
    {
        const uint8_t* magic;
        if (!br.bytes(magic, 4) || memcmp(magic, VLP_MAGIC, 4) != 0)
            return kErrFormat;
        if (!br.u8(major) || !br.u8(minor)) return kErrFormat;
        if (!br.u16(flags) || !br.u32(block_size) || !br.u32(meta_len))
            return kErrFormat;
    }
    if (major != VLP_VERSION_MAJOR) return kErrUnsupported;
    if (block_size == 0 || block_size > VLP_MAX_BLOCK_SIZE) return kErrFormat;
    if (meta_len > (64u << 20)) return kErrFormat; // 64MB 元数据上限

    Crc32 hdr_crc;
    hdr_crc.update(fixed, sizeof(fixed));

    std::vector<uint8_t> meta(meta_len);
    if (meta_len > 0) {
        if (!read_exact(in, meta.data(), meta_len)) return kErrFormat;
        hdr_crc.update(meta.data(), meta_len);
    }
    uint8_t crcbuf[4];
    if (!read_exact(in, crcbuf, 4)) return kErrFormat;
    {
        BufReader cr(crcbuf, 4);
        uint32_t expect;
        cr.u32(expect);
        if (expect != hdr_crc.value()) return kErrCRC;
    }

    ArchiveInfo local_info;
    local_info.version_major = major;
    local_info.version_minor = minor;
    local_info.block_size = block_size;
    if ((flags & 1) && meta_len > 0) {
        VlpError e = parse_metadata(meta.data(), meta.size(), local_info.metadata);
        if (e != kOk) return e;
    }

    // ---- 数据块循环 ----
    // 每条记录以独立 4 字节魔数起始：
    //   "VLPB" -> 数据块头，"VLPE" -> 页脚，其余 -> 格式错误。
    std::vector<uint8_t> payload;
    std::vector<uint8_t> raw;
    uint64_t orig_total = 0, comp_total = 0;
    uint32_t block_count = 0;

    for (;;) {
        uint8_t tag[4];
        if (!read_exact(in, tag, 4)) return kErrFormat; // 意外 EOF

        if (memcmp(tag, VLP_FOOTER_MAGIC, 4) == 0) {
            // ---- 页脚：剩余 20 数据字节 + 4 CRC ----
            uint8_t rest[24];
            if (!read_exact(in, rest, sizeof(rest))) return kErrFormat;

            std::vector<uint8_t> fbody;
            fbody.insert(fbody.end(), tag, tag + 4);
            fbody.insert(fbody.end(), rest, rest + 20);

            BufReader fb(fbody.data(), 24);
            const uint8_t* fm;
            uint64_t f_orig, f_comp;
            uint32_t f_blocks;
            if (!fb.bytes(fm, 4) || !fb.u64(f_orig) || !fb.u64(f_comp) ||
                !fb.u32(f_blocks))
                return kErrFormat;

            BufReader cr(rest + 20, 4);
            uint32_t f_crc;
            cr.u32(f_crc);
            if (f_crc != Crc32::compute(fbody.data(), 24)) return kErrCRC;
            if (f_orig != orig_total || f_comp != comp_total ||
                f_blocks != block_count)
                return kErrCRC;

            local_info.orig_size = f_orig;
            local_info.comp_size = f_comp;
            local_info.block_count = f_blocks;
            if (info) *info = local_info;
            return kOk;
        }

        // 非页脚即必须为数据块：显式校验块魔数
        if (memcmp(tag, VLP_BLOCK_MAGIC, 4) != 0)
            return kErrFormat;

        // ---- 块头：魔数已确认，再读 20 字节字段 + 4 字节 CRC ----
        uint8_t fields[20];
        if (!read_exact(in, fields, sizeof(fields))) return kErrFormat;

        // CRC 覆盖区共 24B（含 4B 魔数）
        uint8_t bh[24];
        memcpy(bh, tag, 4);
        memcpy(bh + 4, fields, 20);

        BufReader bhbr(bh, 24);
        const uint8_t* bm;
        uint32_t orig_size, stored_size, crc_orig, crc_stored;
        uint16_t codec, reserved;
        if (!bhbr.bytes(bm, 4) || !bhbr.u32(orig_size) ||
            !bhbr.u32(stored_size) || !bhbr.u16(codec) ||
            !bhbr.u16(reserved) || !bhbr.u32(crc_orig) ||
            !bhbr.u32(crc_stored))
            return kErrFormat;

        uint8_t bhcrcbuf[4];
        if (!read_exact(in, bhcrcbuf, 4)) return kErrFormat;
        {
            BufReader cr(bhcrcbuf, 4);
            uint32_t expect;
            cr.u32(expect);
            if (expect != Crc32::compute(bh, 24)) return kErrCRC;
        }

        if (codec != VLP_CODEC_STORE && codec != VLP_CODEC_VZ1)
            return kErrUnsupported;
        if (orig_size > block_size || stored_size > block_size + 512)
            return kErrFormat;
        if (codec == VLP_CODEC_STORE && stored_size != orig_size)
            return kErrFormat;

        payload.resize(stored_size);
        if (!read_exact(in, payload.data(), stored_size)) return kErrFormat;
        if (Crc32::compute(payload.data(), stored_size) != crc_stored)
            return kErrCRC;

        raw.resize(orig_size);
        if (codec == VLP_CODEC_VZ1) {
            if (!vz1_decompress(payload.data(), stored_size, raw.data(),
                                orig_size))
                return kErrData;
        } else {
            memcpy(raw.data(), payload.data(), orig_size);
        }
        if (Crc32::compute(raw.data(), orig_size) != crc_orig)
            return kErrCRC;

        if (out && orig_size > 0 && !out->write(raw.data(), orig_size))
            return kErrIO;

        orig_total += orig_size;
        comp_total += stored_size;
        ++block_count;
    }
}

} // namespace vlp
