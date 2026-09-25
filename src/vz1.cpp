#include "vz1.h"
#include "huffman.h"
#include "bitio.h"
#include <string.h>

namespace vlp {

// ---------------- 常量与码表 ----------------

static const int MIN_MATCH = 4;
static const int MAX_MATCH = 258;
static const int W_BITS = 16;
static const int W_SIZE = 1 << W_BITS;      // 65536
static const int W_MASK = W_SIZE - 1;
static const int HASH_BITS = 16;
static const int HASH_SIZE = 1 << HASH_BITS;
static const int NIL = -1;

static const int LL_SYMBOLS = 286; // 0..255 字面量, 256 EOB, 257..285 长度码
static const int D_SYMBOLS = 32;
static const int EOB = 256;

// 长度码表：{base, extra_bits}
struct LenDist { uint16_t base; uint8_t extra; };

static const LenDist kLenTable[29] = {
    {4, 0}, {5, 0}, {6, 0}, {7, 0}, {8, 0}, {9, 0}, {10, 0}, {11, 0},
    {12, 1}, {14, 1}, {16, 1}, {18, 1},
    {20, 2}, {24, 2}, {28, 2}, {32, 2},
    {36, 3}, {44, 3}, {52, 3}, {60, 3},
    {68, 4}, {84, 4}, {100, 4}, {116, 4},
    {132, 5}, {164, 5}, {196, 5}, {228, 5},
    {258, 0},
};

// 距离码表：{base(d'), extra_bits}，d' = 距离 - 1
static const LenDist kDistTable[D_SYMBOLS] = {
    {0, 0}, {1, 0}, {2, 0}, {3, 0},
    {4, 1}, {6, 1},
    {8, 2}, {12, 2},
    {16, 3}, {24, 3},
    {32, 4}, {48, 4},
    {64, 5}, {96, 5},
    {128, 6}, {192, 6},
    {256, 7}, {384, 7},
    {512, 8}, {768, 8},
    {1024, 9}, {1536, 9},
    {2048, 10}, {3072, 10},
    {4096, 11}, {6144, 11},
    {8192, 12}, {12288, 12},
    {16384, 13}, {24576, 13},
    {32768, 14}, {49152, 14},
};

// 长度 -> 长度码 的查找表。
// C++11 magic static：首次调用时线程安全建表，不存在全局可变状态。
static const uint8_t* len_code_table() {
    static const std::vector<uint8_t> t = []() {
        std::vector<uint8_t> v(259, 0xFF);
        for (int c = 0; c < 29; ++c) {
            int base = kLenTable[c].base;
            int span = 1 << kLenTable[c].extra;
            for (int l = base; l < base + span && l <= 258; ++l)
                v[(size_t)l] = (uint8_t)c;
        }
        v[258] = 28; // 特殊码
        return v;
    }();
    return t.data();
}

// 距离 -> 距离码
static int dist_code(uint32_t d) {
    if (d <= 4) return (int)d - 1;
    uint32_t dp = d - 1;
    // e = floor(log2(dp)) - 1
    int lg = 0;
    while ((1u << (lg + 1)) <= dp) ++lg;
    int e = lg - 1;
    int sub = (int)((dp - (1u << (e + 1))) >> e);
    return 4 + (e - 1) * 2 + sub;
}

// ---------------- LZ77 解析 ----------------

struct Token {
    uint32_t litlen; // 字面量值或匹配长度
    uint32_t dist;   // 0 表示字面量
};

static inline uint32_t hash4(const uint8_t* p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return (v * 2654435761u) >> (32 - HASH_BITS);
}

class Matcher {
public:
    Matcher(const uint8_t* data, size_t len, int max_chain)
        : data_(data), len_(len), max_chain_(max_chain) {
        head_.assign(HASH_SIZE, NIL);
        prev_.assign(W_SIZE, NIL);
    }

    void insert(size_t pos) {
        if (pos + MIN_MATCH > len_) return;
        uint32_t h = hash4(data_ + pos);
        prev_[pos & W_MASK] = head_[h];
        head_[h] = (int)pos;
    }

    // 在窗口内寻找 pos 处的最长匹配
    int find(size_t pos, int* out_dist) const {
        if (pos + MIN_MATCH > len_) return 0;
        uint32_t h = hash4(data_ + pos);
        int cand = head_[h];
        size_t max_len = len_ - pos;
        if (max_len > MAX_MATCH) max_len = MAX_MATCH;
        int best = 0;
        int best_dist = 0;
        int chain = max_chain_;
        while (cand != NIL && (int)pos - cand <= W_SIZE && chain-- > 0) {
            // 快速校验首字节与当前最好长度的下一字节
            if (best == 0 ||
                (data_[cand + best] == data_[pos + best] &&
                 data_[cand] == data_[pos])) {
                int l = 0;
                while ((size_t)l < max_len && data_[cand + l] == data_[pos + l])
                    ++l;
                if (l > best) {
                    best = l;
                    best_dist = (int)pos - cand;
                    if ((size_t)l >= max_len) break;
                }
            }
            cand = prev_[cand & W_MASK];
        }
        if (best >= MIN_MATCH) {
            *out_dist = best_dist;
            return best;
        }
        return 0;
    }

private:
    const uint8_t* data_;
    size_t len_;
    int max_chain_;
    std::vector<int> head_;
    std::vector<int> prev_;
};

static void lz77_parse(const uint8_t* in, size_t len, int level,
                       std::vector<Token>& tokens) {
    int max_chain = (level <= 1) ? 32 : (level <= 6 ? 128 : 512);
    bool lazy = level > 1;
    Matcher m(in, len, max_chain);

    size_t pos = 0;
    // pending：上一轮惰性探测时在当前 pos 已算好的匹配，
    // 下一轮直接复用，避免对同一位置重复 find。
    int pending_len = 0;
    int pending_dist = 0;

    while (pos < len) {
        int dist = pending_dist;
        int mlen = pending_len;
        pending_len = 0;
        pending_dist = 0;
        if (mlen == 0)
            mlen = m.find(pos, &dist);

        if (lazy && mlen > 0 && pos + 1 < len) {
            m.insert(pos);
            ++pos;
            int dist2 = 0;
            int mlen2 = m.find(pos, &dist2);
            if (mlen2 > mlen) {
                // 下一位置更优：输出上一位置的字面量；
                // 把当前位置的匹配缓存到下一轮（与 zlib 惰性策略一致）。
                Token t;
                t.litlen = in[pos - 1];
                t.dist = 0;
                tokens.push_back(t);
                pending_len = mlen2;
                pending_dist = dist2;
                continue;
            }
            // 用上一位置（pos-1）的匹配
            Token t;
            t.litlen = (uint32_t)mlen;
            t.dist = (uint32_t)dist;
            tokens.push_back(t);
            for (int i = 0; i < mlen; ++i)
                m.insert((size_t)pos - 1 + (size_t)i);
            pos = (size_t)pos - 1 + (size_t)mlen;
        } else if (mlen > 0) {
            Token t;
            t.litlen = (uint32_t)mlen;
            t.dist = (uint32_t)dist;
            tokens.push_back(t);
            for (int i = 0; i < mlen; ++i)
                m.insert(pos + (size_t)i);
            pos += (size_t)mlen;
        } else {
            m.insert(pos);
            Token t;
            t.litlen = in[pos];
            t.dist = 0;
            tokens.push_back(t);
            ++pos;
        }
    }
}

// ---------------- 压缩 ----------------

bool vz1_compress(const uint8_t* in, size_t inlen, int level,
                  std::vector<uint8_t>& out) {
    if (level < 1) level = 1;
    if (level > 9) level = 9;

    // 1) LZ77 解析
    std::vector<Token> tokens;
    lz77_parse(in, inlen, level, tokens);

    // 2) 频率统计（含 EOB）
    uint32_t freq_ll[LL_SYMBOLS] = {0};
    uint32_t freq_d[D_SYMBOLS] = {0};
    for (size_t i = 0; i < tokens.size(); ++i) {
        const Token& t = tokens[i];
        if (t.dist == 0) {
            ++freq_ll[t.litlen];
        } else {
            ++freq_ll[257 + len_code_table()[t.litlen]];
            ++freq_d[dist_code(t.dist)];
        }
    }
    ++freq_ll[EOB];

    // 3) 构建 Huffman 码长
    uint8_t ll_len[LL_SYMBOLS], d_len[D_SYMBOLS];
    huffman_build_lengths(freq_ll, LL_SYMBOLS, VLP_HUFF_MAX_BITS, ll_len);
    huffman_build_lengths(freq_d, D_SYMBOLS, VLP_HUFF_MAX_BITS, d_len);

    // 4) 写出码长表（4bit/符号）
    out.clear();
    out.reserve(inlen / 2 + 256);
    for (int i = 0; i < LL_SYMBOLS; i += 2)
        out.push_back((uint8_t)((ll_len[i] << 4) | ll_len[i + 1]));
    for (int i = 0; i < D_SYMBOLS; i += 2)
        out.push_back((uint8_t)((d_len[i] << 4) | d_len[i + 1]));

    // 5) 编码位流
    HuffmanEncoder ll_enc, d_enc;
    ll_enc.init(ll_len, LL_SYMBOLS);
    d_enc.init(d_len, D_SYMBOLS);

    BitWriter bw;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const Token& t = tokens[i];
        if (t.dist == 0) {
            ll_enc.encode(bw, (int)t.litlen);
        } else {
            int lc = len_code_table()[t.litlen];
            ll_enc.encode(bw, 257 + lc);
            if (kLenTable[lc].extra)
                bw.put_bits(t.litlen - kLenTable[lc].base, kLenTable[lc].extra);
            int dc = dist_code(t.dist);
            d_enc.encode(bw, dc);
            if (kDistTable[dc].extra)
                bw.put_bits((t.dist - 1) - kDistTable[dc].base,
                            kDistTable[dc].extra);
        }
    }
    ll_enc.encode(bw, EOB);
    bw.flush();

    const std::vector<uint8_t>& bits = bw.data();
    out.insert(out.end(), bits.begin(), bits.end());
    return true;
}

// ---------------- 解压 ----------------

bool vz1_decompress(const uint8_t* in, size_t inlen,
                    uint8_t* out, size_t outlen) {
    const size_t LL_TAB = (LL_SYMBOLS + 1) / 2; // 143
    const size_t D_TAB = D_SYMBOLS / 2;         // 16
    if (inlen < LL_TAB + D_TAB) return false;

    uint8_t ll_len[LL_SYMBOLS], d_len[D_SYMBOLS];
    for (int i = 0; i < LL_SYMBOLS; ++i) {
        uint8_t b = in[i / 2];
        ll_len[i] = (i % 2 == 0) ? (b >> 4) : (b & 0x0F);
    }
    for (int i = 0; i < D_SYMBOLS; ++i) {
        uint8_t b = in[LL_TAB + i / 2];
        d_len[i] = (i % 2 == 0) ? (b >> 4) : (b & 0x0F);
    }

    HuffmanDecoder ll_dec, d_dec;
    if (!ll_dec.init(ll_len, LL_SYMBOLS)) return false;
    bool has_d = false;
    for (int i = 0; i < D_SYMBOLS; ++i)
        if (d_len[i]) { has_d = true; break; }
    if (has_d && !d_dec.init(d_len, D_SYMBOLS)) return false;

    BitReader br(in + LL_TAB + D_TAB, inlen - LL_TAB - D_TAB);
    size_t produced = 0;
    for (;;) {
        int sym = ll_dec.decode(br);
        if (sym < 0 || br.overread()) return false;
        if (sym == EOB) break;
        if (sym < 256) {
            if (produced >= outlen) return false;
            out[produced++] = (uint8_t)sym;
        } else {
            int lc = sym - 257;
            if (lc < 0 || lc >= 29 || !has_d) return false;
            uint32_t len = kLenTable[lc].base;
            if (kLenTable[lc].extra)
                len += br.get_bits(kLenTable[lc].extra);
            int dc = d_dec.decode(br);
            if (dc < 0 || dc >= D_SYMBOLS || br.overread()) return false;
            uint32_t dist = (uint32_t)kDistTable[dc].base + 1;
            if (kDistTable[dc].extra)
                dist += br.get_bits(kDistTable[dc].extra);
            if (dist > produced || produced + len > outlen) return false;
            for (uint32_t k = 0; k < len; ++k) {
                out[produced] = out[produced - dist];
                ++produced;
            }
        }
    }
    return produced == outlen;
}

} // namespace vlp
