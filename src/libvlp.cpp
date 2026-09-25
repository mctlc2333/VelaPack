#include "vlp.h"
#include "format.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 可移植字符串复制：strdup 属于 POSIX 而非 ISO C（MSVC 为 _strdup），
// 统一用 malloc + memcpy 实现。
static char* portable_strdup(const char* s) {
    size_t len = strlen(s);
    char* p = (char*)malloc(len + 1);
    if (p) {
        memcpy(p, s, len);
        p[len] = 0;
    }
    return p;
}

namespace vlp {

// 文件 Reader / Writer
struct FileReader : Reader {
    FILE* f;
    FileReader(FILE* fh) : f(fh) {}
    virtual size_t read(uint8_t* buf, size_t len) {
        return fread(buf, 1, len, f);
    }
};

struct FileWriter : Writer {
    FILE* f;
    FileWriter(FILE* fh) : f(fh) {}
    virtual bool write(const uint8_t* buf, size_t len) {
        return fwrite(buf, 1, len, f) == len;
    }
};

struct MemReader : Reader {
    const uint8_t* p;
    size_t len, pos;
    MemReader(const uint8_t* _p, size_t _l) : p(_p), len(_l), pos(0) {}
    virtual size_t read(uint8_t* buf, size_t n) {
        size_t r = len - pos;
        if (n < r) r = n;
        memcpy(buf, p + pos, r);
        pos += r;
        return r;
    }
};

struct MemWriter : Writer {
    uint8_t* p;
    size_t cap, pos;
    MemWriter(uint8_t* _p, size_t _c) : p(_p), cap(_c), pos(0) {}
    virtual bool write(const uint8_t* buf, size_t n) {
        if (pos + n > cap) return false;
        memcpy(p + pos, buf, n);
        pos += n;
        return true;
    }
};

struct VecWriter : Writer {
    std::vector<uint8_t>* v;
    VecWriter(std::vector<uint8_t>* _v) : v(_v) {}
    virtual bool write(const uint8_t* buf, size_t n) {
        v->insert(v->end(), buf, buf + n);
        return true;
    }
};

} // namespace vlp

extern "C" {

int vlp_compress(const uint8_t* in, size_t inlen,
                 uint8_t** out, size_t* outlen, int level) {
    if (!out || !outlen || level < 1 || level > 9)
        return VLP_ERR_ARG;
    if (!in && inlen > 0) return VLP_ERR_ARG;
    using namespace vlp;
    CompressOptions opt;
    opt.level = level;
    opt.block_size = (inlen < VLP_DEFAULT_BLOCK_SIZE)
                         ? (uint32_t)inlen
                         : VLP_DEFAULT_BLOCK_SIZE;
    if (opt.block_size == 0) opt.block_size = 1;

    std::vector<uint8_t> result;
    VecWriter vw(&result);
    MemReader mr(in, inlen);
    VlpError e = compress_stream(mr, vw, opt);
    if (e != VLP_OK) return e;
    if (result.empty()) { *out = NULL; *outlen = 0; return VLP_OK; }

    *out = (uint8_t*)malloc(result.size());
    if (!*out) return VLP_ERR_IO;
    memcpy(*out, result.data(), result.size());
    *outlen = result.size();
    return VLP_OK;
}

int vlp_decompress(const uint8_t* in, size_t inlen,
                   uint8_t** out, size_t* outlen) {
    if (!in || !out || !outlen) return VLP_ERR_ARG;
    using namespace vlp;
    std::vector<uint8_t> result;
    VecWriter vw(&result);
    MemReader mr(in, inlen);
    ArchiveInfo info;
    VlpError e = decompress_stream(mr, &vw, &info);
    if (e != VLP_OK) return e;
    if (result.empty()) { *out = NULL; *outlen = 0; return VLP_OK; }

    *out = (uint8_t*)malloc(result.size());
    if (!*out) return VLP_ERR_IO;
    memcpy(*out, result.data(), result.size());
    *outlen = result.size();
    return VLP_OK;
}

int vlp_file_compress(const char* src, const char* dst, int level) {
    return vlp_file_compress_meta(src, dst, level, NULL, 0);
}

int vlp_file_compress_meta(const char* src, const char* dst, int level,
                           const vlp_meta_item_t* meta, size_t meta_count) {
    if (!src || !dst || level < 1 || level > 9) return VLP_ERR_ARG;
    using namespace vlp;
    FILE* fin = fopen(src, "rb");
    if (!fin) return VLP_ERR_IO;
    FILE* fout = fopen(dst, "wb");
    if (!fout) { fclose(fin); return VLP_ERR_IO; }

    CompressOptions opt;
    opt.level = level;
    if (meta && meta_count > 0) {
        for (size_t i = 0; i < meta_count; ++i) {
            opt.metadata[std::string(meta[i].key)] =
                std::string(meta[i].value);
        }
    }

    FileReader fr(fin);
    FileWriter fw(fout);
    VlpError e = compress_stream(fr, fw, opt);
    fclose(fin);
    fclose(fout);
    if (e != VLP_OK) {
        remove(dst);
        return e;
    }
    return VLP_OK;
}

int vlp_file_decompress(const char* src, const char* dst) {
    if (!src || !dst) return VLP_ERR_ARG;
    using namespace vlp;
    FILE* fin = fopen(src, "rb");
    if (!fin) return VLP_ERR_IO;
    FILE* fout = fopen(dst, "wb");
    if (!fout) { fclose(fin); return VLP_ERR_IO; }

    FileReader fr(fin);
    FileWriter fw(fout);
    ArchiveInfo info;
    VlpError e = decompress_stream(fr, &fw, &info);
    fclose(fin);
    fclose(fout);
    if (e != VLP_OK) {
        remove(dst);
        return e;
    }
    return VLP_OK;
}

int vlp_file_test(const char* path, vlp_archive_info_t* info) {
    if (!path) return VLP_ERR_ARG;
    using namespace vlp;
    FILE* fin = fopen(path, "rb");
    if (!fin) return VLP_ERR_IO;
    FileReader fr(fin);
    ArchiveInfo ainfo;
    VlpError e = decompress_stream(fr, (Writer*)NULL, &ainfo);
    fclose(fin);
    if (e != VLP_OK) return e;

    if (info) {
        memset(info, 0, sizeof(*info));
        info->block_size = ainfo.block_size;
        info->orig_size = ainfo.orig_size;
        info->comp_size = ainfo.comp_size;
        info->block_count = ainfo.block_count;
        if (!ainfo.metadata.empty()) {
            info->meta_count = ainfo.metadata.size();
            info->meta = (vlp_meta_item_t*)malloc(
                sizeof(vlp_meta_item_t) * info->meta_count);
            size_t i = 0;
            for (std::map<std::string, std::string>::const_iterator it =
                     ainfo.metadata.begin();
                 it != ainfo.metadata.end(); ++it, ++i) {
                info->meta[i].key = portable_strdup(it->first.c_str());
                info->meta[i].value = portable_strdup(it->second.c_str());
            }
        }
    }
    return VLP_OK;
}

void vlp_archive_info_free(vlp_archive_info_t* info) {
    if (!info || !info->meta) return;
    for (size_t i = 0; i < info->meta_count; ++i) {
        free((void*)info->meta[i].key);
        free((void*)info->meta[i].value);
    }
    free(info->meta);
    info->meta = NULL;
    info->meta_count = 0;
}

const char* vlp_strerror(int err) {
    switch (err) {
        case VLP_OK: return "OK";
        case VLP_ERR_IO: return "I/O error";
        case VLP_ERR_FORMAT: return "Invalid format";
        case VLP_ERR_CRC: return "CRC mismatch";
        case VLP_ERR_UNSUPPORTED: return "Unsupported version/codec";
        case VLP_ERR_DATA: return "Corrupted compressed data";
        case VLP_ERR_ARG: return "Invalid argument";
        default: return "Unknown error";
    }
}

const char* vlp_version(void) { return "1.0.0"; }

} // extern "C"
