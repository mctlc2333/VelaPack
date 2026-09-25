#include "huffman.h"
#include "bitio.h"
#include <assert.h>
#include <string.h>

namespace vlp {

// ---------- 码长构建（堆式 Huffman + zlib 风格限长修正） ----------

namespace {

struct Node {
    uint32_t freq;
    int parent;   // -1 表示无父节点
    int left, right;
};

// 简单二叉堆（按 freq 升序）
struct Heap {
    int n = 0;
    std::vector<int> v; // 存储节点下标
    const Node* nodes = NULL;

    bool less(int a, int b) const { return nodes[a].freq < nodes[b].freq; }

    void push(int x) {
        v.push_back(x);
        int i = n++;
        while (i > 0) {
            int p = (i - 1) / 2;
            if (!less(v[i], v[p])) break;
            int t = v[i]; v[i] = v[p]; v[p] = t;
            i = p;
        }
    }

    int pop() {
        int top = v[0];
        v[0] = v[--n];
        v.pop_back();
        int i = 0;
        for (;;) {
            int l = i * 2 + 1, r = l + 1, m = i;
            if (l < n && less(v[l], v[m])) m = l;
            if (r < n && less(v[r], v[m])) m = r;
            if (m == i) break;
            int t = v[i]; v[i] = v[m]; v[m] = t;
            i = m;
        }
        return top;
    }
};

} // namespace

void huffman_build_lengths(const uint32_t* freq, int n, int maxbits,
                           uint8_t* lengths) {
    memset(lengths, 0, (size_t)n);

    // 统计非零符号
    int used = 0;
    int last = -1;
    for (int i = 0; i < n; ++i) {
        if (freq[i]) { ++used; last = i; }
    }
    if (used == 0) return;
    if (used == 1) { lengths[last] = 1; return; }

    // 建 Huffman 树
    std::vector<Node> nodes((size_t)(2 * n));
    Heap heap;
    heap.nodes = nodes.data();
    for (int i = 0; i < n; ++i) {
        if (freq[i]) {
            nodes[i].freq = freq[i];
            nodes[i].parent = nodes[i].left = nodes[i].right = -1;
            heap.push(i);
        }
    }
    int next = n;
    while (heap.n >= 2) {
        int a = heap.pop();
        int b = heap.pop();
        Node& nd = nodes[next];
        nd.freq = nodes[a].freq + nodes[b].freq;
        nd.parent = -1;
        nd.left = a;
        nd.right = b;
        nodes[a].parent = next;
        nodes[b].parent = next;
        heap.push(next);
        ++next;
    }

    // 限长：先钳制，再按 zlib 方式修复 Kraft 和
    uint16_t bl_count[VLP_HUFF_MAX_BITS + 1];
    memset(bl_count, 0, sizeof(bl_count));
    int overflow = 0;
    for (int i = 0; i < n; ++i) {
        if (!freq[i]) continue;
        int d = 0;
        for (int p = i; nodes[p].parent != -1; p = nodes[p].parent) ++d;
        if (d > maxbits) {
            d = maxbits;
            ++overflow;
        }
        lengths[i] = (uint8_t)d;
        ++bl_count[d];
    }

    // 钳制产生的超长叶子数必为偶数（zlib 同一隐式前提），断言固化
    assert(overflow % 2 == 0);

    while (overflow > 0) {
        int bits = maxbits - 1;
        while (bits > 0 && bl_count[bits] == 0) --bits;
        if (bits == 0) break; // 理论上不会发生
        --bl_count[bits];        // 上移一个叶子
        bl_count[bits + 1] += 2; // 拆成两个
        --bl_count[maxbits];     // 一个超长叶子被固定
        overflow -= 2;
    }

    // 重新按 (freq 升序 -> 码长降序) 分配码长：
    // 简单起见：把所有非零符号按频率升序排序，码长按上面得到的
    // 多重集合降序分配，保证 Kraft 等式成立且前缀码有效。
    // 收集符号（频率升序）
    std::vector<int> syms;
    for (int i = 0; i < n; ++i)
        if (freq[i]) syms.push_back(i);
    // 冒泡/插入排序即可（n 很小）
    for (size_t i = 1; i < syms.size(); ++i) {
        int key = syms[i];
        uint32_t kf = freq[key];
        size_t j = i;
        while (j > 0 && freq[syms[j - 1]] > kf) {
            syms[j] = syms[j - 1];
            --j;
        }
        syms[j] = key;
    }
    // 收集码长（降序）
    std::vector<int> lens;
    for (int b = maxbits; b >= 1; --b)
        for (int k = 0; k < bl_count[b]; ++k) lens.push_back(b);

    memset(lengths, 0, (size_t)n);
    for (size_t i = 0; i < syms.size(); ++i)
        lengths[syms[i]] = (uint8_t)lens[i];
}

void huffman_build_codes(const uint8_t* lengths, int n, uint32_t* codes) {
    uint32_t next_code[VLP_HUFF_MAX_BITS + 1];
    uint32_t code = 0;
    uint16_t bl_count[VLP_HUFF_MAX_BITS + 1];
    memset(bl_count, 0, sizeof(bl_count));
    for (int i = 0; i < n; ++i)
        if (lengths[i]) ++bl_count[lengths[i]];
    bl_count[0] = 0;

    for (int b = 1; b <= VLP_HUFF_MAX_BITS; ++b) {
        code = (code + bl_count[b - 1]) << 1;
        next_code[b] = code;
    }
    for (int i = 0; i < n; ++i) {
        if (lengths[i]) codes[i] = next_code[lengths[i]]++;
        else codes[i] = 0;
    }
}

// ---------- 编码器 / 解码器 ----------

void HuffmanEncoder::init(const uint8_t* lengths, int n) {
    n_ = n;
    lengths_.assign(lengths, lengths + n);
    codes_.resize((size_t)n);
    huffman_build_codes(lengths, n, codes_.data());
}

void HuffmanEncoder::encode(BitWriter& bw, int symbol) const {
    bw.put_bits(codes_[symbol], lengths_[symbol]);
}

bool HuffmanDecoder::init(const uint8_t* lengths, int n) {
    n_ = n;
    memset(counts_, 0, sizeof(counts_));
    int used = 0;
    for (int i = 0; i < n; ++i) {
        if (lengths[i] > VLP_HUFF_MAX_BITS) return false;
        if (lengths[i]) {
            ++counts_[lengths[i]];
            ++used;
        }
    }
    if (used == 0) return false;

    // Kraft 检查：sum(2^-len) <= 1。
    // 放大 2^15 倍做整数比较；使用 uint64 防止移位加法溢出。
    uint64_t kraft = 0;
    for (int b = 1; b <= VLP_HUFF_MAX_BITS; ++b)
        kraft += (uint64_t)counts_[b] << (VLP_HUFF_MAX_BITS - b);
    if (kraft > (1ull << VLP_HUFF_MAX_BITS)) return false;

    // 符号按 (len, symbol) 升序排列
    symbols_.clear();
    symbols_.reserve((size_t)used);
    for (int b = 1; b <= VLP_HUFF_MAX_BITS; ++b)
        for (int s = 0; s < n; ++s)
            if (lengths[s] == b) symbols_.push_back((uint16_t)s);
    return true;
}

int HuffmanDecoder::decode(BitReader& br) const {
    int code = 0;
    int first = 0;
    int index = 0;
    for (int len = 1; len <= VLP_HUFF_MAX_BITS; ++len) {
        code |= (int)br.get_bit();
        int count = counts_[len];
        if (code - first < count)
            return symbols_[(size_t)(index + (code - first))];
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return -1; // 非法码字
}

} // namespace vlp
