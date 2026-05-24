/* Mock: mbedtls/sha256.h */
#pragma once
#include <stddef.h>
#include <string.h>

static inline int mbedtls_sha256(const unsigned char *input, size_t ilen,
                                  unsigned char *output, int is224) {
    (void)is224;
    /* Deterministic fake: output = first 32 bytes of input XOR 0x5A */
    memset(output, 0, 32);
    for (size_t i = 0; i < ilen && i < 32; i++)
        output[i] = input[i] ^ 0x5A;
    return 0;
}
