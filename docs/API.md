# VelaPack C API 参考

## 1. 概述

VelaPack 提供纯 C API，可直接在 C、C++、Python（ctypes/cffi）、Rust（FFI）等语言中调用。

## 2. 错误码

| 常量 | 值 | 说明 |
|------|-----|------|
| `VLP_OK` | 0 | 成功 |
| `VLP_ERR_IO` | 1 | 文件读写错误 |
| `VLP_ERR_FORMAT` | 2 | 非法 .vlp 格式 |
| `VLP_ERR_CRC` | 3 | CRC 校验失败 |
| `VLP_ERR_UNSUPPORTED` | 4 | 版本或编解码器不支持 |
| `VLP_ERR_DATA` | 5 | 压缩数据损坏 |
| `VLP_ERR_ARG` | 6 | 参数错误 |

获取错误描述：

```c
const char* vlp_strerror(int err);
```

## 3. 内存级 API

适用于小块数据或嵌入式场景。

### 3.1 压缩

```c
int vlp_compress(const uint8_t* in, size_t inlen,
                 uint8_t** out, size_t* outlen, int level);
```

- `in` / `inlen`：输入数据，可为 `NULL`/`0`（生成空容器）
- `out`：输出缓冲区，由库 `malloc` 分配，调用者负责 `free`
- `outlen`：输出长度
- `level`：1（最快）~ 9（最高压缩率），默认 6

### 3.2 解压

```c
int vlp_decompress(const uint8_t* in, size_t inlen,
                   uint8_t** out, size_t* outlen);
```

- 自动识别压缩级别与块大小
- 自动执行 CRC 完整性校验
- 返回空输出时 `out` 为 `NULL`、`outlen` 为 0

### 3.3 示例

```c
#include <vlp.h>
#include <stdio.h>
#include <stdlib.h>

int main() {
    const char* text = "Hello VelaPack!";
    uint8_t* comp = NULL;
    size_t clen = 0;

    if (vlp_compress((const uint8_t*)text, strlen(text), &comp, &clen, 6) != VLP_OK) {
        fprintf(stderr, "compress failed\n");
        return 1;
    }

    uint8_t* dec = NULL;
    size_t dlen = 0;
    if (vlp_decompress(comp, clen, &dec, &dlen) != VLP_OK) {
        fprintf(stderr, "decompress failed\n");
        return 1;
    }

    printf("roundtrip: %.*s\n", (int)dlen, dec);
    free(comp);
    free(dec);
    return 0;
}
```

## 4. 文件级 API

### 4.1 压缩文件

```c
int vlp_file_compress(const char* src, const char* dst, int level);
```

### 4.2 带元数据的压缩

```c
int vlp_file_compress_meta(const char* src, const char* dst, int level,
                           const vlp_meta_item_t* meta, size_t meta_count);
```

`vlp_meta_item_t` 定义：

```c
typedef struct {
    const char* key;
    const char* value;
} vlp_meta_item_t;
```

### 4.3 解压文件

```c
int vlp_file_decompress(const char* src, const char* dst);
```

### 4.4 测试与信息

```c
int vlp_file_test(const char* path, vlp_archive_info_t* info);
void vlp_archive_info_free(vlp_archive_info_t* info);
```

`vlp_archive_info_t` 包含：

```c
typedef struct {
    uint32_t block_size;
    uint64_t orig_size;
    uint64_t comp_size;
    uint32_t block_count;
    vlp_meta_item_t* meta;   // 由库分配
    size_t meta_count;
} vlp_archive_info_t;
```

使用完毕后必须调用 `vlp_archive_info_free()`。

### 4.5 示例

```c
vlp_meta_item_t meta[] = {
    {"filename", "report.pdf"},
    {"content_type", "application/pdf"},
};
int r = vlp_file_compress_meta("report.pdf", "report.pdf.vlp", 9,
                               meta, 2);
if (r != VLP_OK) {
    fprintf(stderr, "%s\n", vlp_strerror(r));
}
```

## 5. 链接

编译静态库后链接：

```bash
g++ -std=c++11 your_app.c -I/path/to/vlp/include -L/path/to/vlp/lib -lvlp -o your_app
```

## 6. 线程安全

- `vlp_compress` / `vlp_decompress`：线程安全（无全局状态）
- `vlp_file_*`：线程安全，但不同线程同时操作同一文件需自行同步
- `vlp_archive_info_free`：仅可调用一次，重复调用未定义
