# VelaPack (.vlp)

VelaPack 是一个实验性文件压缩格式与命令行工具，兼顾压缩率、速度与数据完整性。纯 C++11 实现，无外部依赖。

## 特性

- **LZ77 + 规范霍夫曼编码**：64 KiB 滑动窗口、哈希链匹配、惰性解析
- **分块流式处理**：默认 1 MiB 块，解压内存占用与文件总大小无关
- **完整 CRC 校验**：文件头、每个数据块、页脚独立 CRC32
- **元数据支持**：可嵌入文件名、MIME 类型、时间戳等键值对
- **跨平台**：Windows / macOS / Linux，MSVC 与 MinGW 均可编译
- **C API**：可直接从 C/C++ 调用，也便于 Python ctypes、Rust FFI 绑定

## 编译

### Linux / macOS

```bash
g++ -std=c++11 -O2 -Wall -Wextra -Iinclude \
    src/huffman.cpp src/vz1.cpp src/format.cpp src/libvlp.cpp src/cli.cpp \
    -o vlp
```

### Windows (MinGW)

```bat
g++ -std=c++11 -O2 -Wall -Wextra -Iinclude ^
    src\huffman.cpp src\vz1.cpp src\format.cpp src\libvlp.cpp src\cli.cpp ^
    -o vlp.exe
```

### Windows (MSVC)

```bat
cl /std:c++14 /O2 /W4 /Iinclude ^
   src\huffman.cpp src\vz1.cpp src\format.cpp src\libvlp.cpp src\cli.cpp ^
   /Fe:vlp.exe
```

> MSVC 2015 及以上均可。`libvlp.cpp` 里用 `portable_strdup` 替代 POSIX `strdup`，无需 `_CRT_NONSTDC_NO_DEPRECATE`。

### 单元测试

```bash
g++ -std=c++11 -O2 -Wall -Wextra -Iinclude \
    src/huffman.cpp src/vz1.cpp src/format.cpp src/libvlp.cpp \
    tests/test_roundtrip.cpp -o test_roundtrip
./test_roundtrip
```

Windows 下把 `./test_roundtrip` 换成 `test_roundtrip.exe`。

## 命令行用法

```bat
:: 压缩（默认级别 6）
vlp c input.txt                 :: → input.txt.vlp
vlp c input.txt output.vlp      :: 显式输出名
vlp c -9 input.txt              :: 最高压缩率
vlp c -1 input.txt              :: 最快

:: 解压（自动去掉 .vlp 后缀）
vlp d input.txt.vlp             :: → input.txt
vlp d input.txt.vlp out.txt     :: 显式输出名

:: 测试完整性（不解压到磁盘）
vlp t input.txt.vlp

:: 查看归档信息（同 t，输出更友好）
vlp i input.txt.vlp
```

命令支持缩写：`c` / `d` / `t` / `i`，大小写均可。没有参数时打印用法。

### 输出示例

```
D:\VelaPack>vlp i repeat.txt.vlp
Test OK: repeat.txt.vlp
  Original:   46000 bytes
  Compressed: 328 bytes
  Blocks:     1 (block_size=1048576)
```

> `Compressed` 是**块负载总和**，不含容器开销。完整文件大小 = 文件头 (20B) + N × (块头 28B + 负载) + 页脚 (28B)。

## 压缩级别

| 级别 | 模式 | 链搜索深度 | 适用场景 |
|------|------|-----------|---------|
| 1 | 贪婪 | 32 | 追求速度 |
| 2–6 | 惰性 | 128 | 平衡（默认 6） |
| 7–9 | 惰性 | 512 | 追求压缩率 |

级别只影响 LZ77 解析，不影响 Huffman 编码。**对高度重复或极度随机的输入，不同级别可能产生完全相同的输出**——因为这两种情况下解析路径不受链搜索深度和惰性匹配影响。

## 实测基准

| 输入 | 原始大小 | -9 输出 | 比例 | 说明 |
|------|---------|--------|------|------|
| `test.txt`（66 字节短句） | 66 B | 66 B | 1.00 | 走 STORE，压缩无收益 |
| `repeat.txt`（1000 行重复文本） | 46000 B | 328 B | ~140:1 | 1/6/9 级别输出逐字节相同 |
| `vz1.cpp`（源码） | ~20 KB | — | ~3:1 | 级别 9 比级别 1 小 2%~5% |

小文件（几百字节）压缩后变大是正常的，每个块固定带 159 字节 Huffman 码长表。对大文件这个开销可忽略。

## C API

```c
#include <vlp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    const char* text = "Hello VelaPack!";
    uint8_t* comp = NULL;
    size_t clen = 0;

    int r = vlp_compress((const uint8_t*)text, strlen(text),
                         &comp, &clen, 6);
    if (r != VLP_OK) {
        fprintf(stderr, "compress: %s\n", vlp_strerror(r));
        return 1;
    }

    uint8_t* dec = NULL;
    size_t dlen = 0;
    r = vlp_decompress(comp, clen, &dec, &dlen);
    if (r != VLP_OK) {
        fprintf(stderr, "decompress: %s\n", vlp_strerror(r));
        free(comp);
        return 1;
    }

    printf("roundtrip: %.*s\n", (int)dlen, dec);
    free(comp);
    free(dec);
    return 0;
}
```

- `vlp_compress` 成功时 `*out` 由库 `malloc` 分配，调用者负责 `free`

## 格式标识

| 项目 | 值 |
|------|-----|
| 扩展名 | `.vlp` |
| MIME 类型 | `application/x-vlp` |
| 文件头魔数 | `56 4C 50 31` ("VLP1") |
| 数据块魔数 | `56 4C 50 42` ("VLPB") |
| 页脚魔数 | `56 4C 50 45` ("VLPE") |
| 版本 | 1.0 |

块头与页脚各自使用独立魔数，解析时不存在歧义。

## 完整性保障

每个 `.vlp` 文件包含 3 层 CRC32 校验：

1. **文件头 CRC**：覆盖魔数、版本、block_size、元数据
2. **块头 CRC + 数据 CRC**：每个数据块包含块头 CRC、原始数据 CRC、存储数据 CRC
3. **页脚 CRC**：覆盖总原始大小、总压缩大小、块计数

解压时任一 CRC 不匹配立即中止，并返回 `VLP_ERR_CRC`。页脚统计值还会与解压累计值比对，防止静默截断。

## 资源限制

| 项目 | 限制 |
|------|------|
| `block_size` | 1 B ~ 64 MiB |
| 元数据区域 | ≤ 64 MiB |
| 单个键长度 | ≤ 65535 字节 |
| 存储负载 | ≤ block_size + 512 B |

解压内存占用约为 `2 × block_size + 64 KiB`，与文件总大小无关。处理不可信来源时，建议应用层把 `block_size` 钳制到 16 MiB 以下。

## 线程安全

- `vlp_compress` / `vlp_decompress`：**线程安全**。CRC 表和长度码表都使用 C++11 magic static 一次性初始化。
- `vlp_file_compress` / `vlp_file_decompress` / `vlp_file_test`：线程安全，但并发写同一文件需调用方自行同步。
- `vlp_archive_info_free`：仅可调用一次。重复调用未定义行为。

## 许可证

MIT License
