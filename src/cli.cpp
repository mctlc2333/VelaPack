// VelaPack - 命令行工具 vlp
// 用法：
//   vlp c[ompress] [-1..9] input [output.vlp]
//   vlp d[ecompress] input.vlp [output]
//   vlp t[est] input.vlp
//   vlp i[nfo] input.vlp
#include "vlp.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

// 可移植的 uint64 -> 字符串（老 MSVCRT 不支持 %llu）
static const char* u64str(uint64_t v) {
    static char buf[24];
    char tmp[24];
    int n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    int i = 0;
    while (n > 0) buf[i++] = tmp[--n];
    buf[i] = 0;
    return buf;
}

static void usage(const char* prog) {
    printf("Usage:\n");
    printf("  %s c[ompress]  [-1..9] <input> [output.vlp]\n", prog);
    printf("  %s d[ecompress] <input.vlp> [output]\n", prog);
    printf("  %s t[est]       <input.vlp>\n", prog);
    printf("  %s i[nfo]       <input.vlp>\n", prog);
}

static int do_compress(int argc, char** argv) {
    int level = 6;
    int pos = 2;
    if (pos < argc && argv[pos][0] == '-' && argv[pos][1] >= '1' &&
        argv[pos][1] <= '9' && argv[pos][2] == 0) {
        level = argv[pos][1] - '0';
        ++pos;
    }
    if (pos >= argc) { usage(argv[0]); return 1; }
    const char* input = argv[pos++];
    const char* output = (pos < argc) ? argv[pos] : NULL;
    char auto_out[512];
    if (!output) {
        size_t l = strlen(input);
        if (l > 4 && strcmp(input + l - 4, ".vlp") == 0) {
            fprintf(stderr,
                    "Input already has .vlp extension; "
                    "please specify an output name explicitly.\n");
            return 1;
        }
        snprintf(auto_out, sizeof(auto_out), "%s.vlp", input);
        output = auto_out;
    }

    int r = vlp_file_compress(input, output, level);
    if (r != VLP_OK) {
        fprintf(stderr, "Compress failed: %s\n", vlp_strerror(r));
        return 1;
    }
    printf("Compressed: %s -> %s (level=%d)\n", input, output, level);
    return 0;
}

static int do_decompress(int argc, char** argv) {
    if (argc < 3) { usage(argv[0]); return 1; }
    const char* input = argv[2];
    const char* output = (argc > 3) ? argv[3] : NULL;
    char auto_out[512];
    if (!output) {
        size_t l = strlen(input);
        if (l > 4 && strcmp(input + l - 4, ".vlp") == 0) {
            memcpy(auto_out, input, l - 4);
            auto_out[l - 4] = 0;
        } else {
            snprintf(auto_out, sizeof(auto_out), "%s.dec", input);
        }
        output = auto_out;
    }

    int r = vlp_file_decompress(input, output);
    if (r != VLP_OK) {
        fprintf(stderr, "Decompress failed: %s\n", vlp_strerror(r));
        return 1;
    }
    printf("Decompressed: %s -> %s\n", input, output);
    return 0;
}

static int do_test(int argc, char** argv) {
    if (argc < 3) { usage(argv[0]); return 1; }
    vlp_archive_info_t info;
    int r = vlp_file_test(argv[2], &info);
    if (r != VLP_OK) {
        fprintf(stderr, "Test failed: %s\n", vlp_strerror(r));
        return 1;
    }
    printf("Test OK: %s\n", argv[2]);
    printf("  Original:   %s bytes\n", u64str(info.orig_size));
    printf("  Compressed: %s bytes\n", u64str(info.comp_size));
    printf("  Blocks:     %u (block_size=%u)\n",
           info.block_count, info.block_size);
    if (info.meta_count) {
        printf("  Metadata:\n");
        for (size_t i = 0; i < info.meta_count; ++i)
            printf("    %s = %s\n", info.meta[i].key, info.meta[i].value);
    }
    vlp_archive_info_free(&info);
    return 0;
}

static int do_info(int argc, char** argv) {
    return do_test(argc, argv);
}

int main(int argc, char** argv) {
    if (argc < 2) { usage(argv[0]); return 1; }
    char cmd = argv[1][0];
    if (cmd == 'c' || cmd == 'C') return do_compress(argc, argv);
    if (cmd == 'd' || cmd == 'D') return do_decompress(argc, argv);
    if (cmd == 't' || cmd == 'T') return do_test(argc, argv);
    if (cmd == 'i' || cmd == 'I') return do_info(argc, argv);
    usage(argv[0]);
    return 1;
}
