/*
Copyright 2006 - 2024 Intel Corporation

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <malloc.h>
#include <stdint.h>
#include <openssl/sha.h>

#include "microstack/ILibSimpleDataStore.h"

#ifndef CRYPTO_MEM_CHECK_ON
#define CRYPTO_MEM_CHECK_ON 0
#endif
#ifndef CRYPTO_mem_ctrl
#define CRYPTO_mem_ctrl(x) (0)
#endif

// The cache-only exercise paths do not need compression or CRC helpers, so we
// provide lightweight stubs here to avoid pulling in the full microscript
// implementation when building the probe standalone.
uint32_t crc32c(uint32_t crc, const unsigned char *buf, uint32_t len)
{
        (void)crc;
        (void)buf;
        (void)len;
        return 0;
}

int ILibInflate(char *buffer, size_t bufferLen, char *decompressed, size_t *decompressedLen, uint32_t expectedCrc)
{
        (void)buffer;
        (void)bufferLen;
        (void)decompressed;
        (void)decompressedLen;
        (void)expectedCrc;
        return 0;
}

int ILibDeflate(char *buffer, size_t bufferLen, char *compressed, size_t *compressedLen, uint32_t *computedCrc)
{
        (void)buffer;
        (void)bufferLen;
        (void)compressed;
        (void)compressedLen;
        (void)computedCrc;
        return 1;
}

void util_sha384(char *data, size_t datalen, char *result)
{
        SHA384((unsigned char*)data, datalen, (unsigned char*)result);
}

void util_hexToBuf(char* hex, int hexLen, char* buf)
{
        int outIndex = 0;
        for (int i = 0; i < hexLen; i += 2)
        {
                int hi = toupper((unsigned char)hex[i]);
                int lo = (i + 1 < hexLen) ? toupper((unsigned char)hex[i + 1]) : '0';
                hi = (hi >= 'A') ? (hi - 'A' + 10) : (hi - '0');
                lo = (lo >= 'A') ? (lo - 'A' + 10) : (lo - '0');
                buf[outIndex++] = (char)((hi << 4) | (lo & 0xF));
        }
}

#if defined(__GLIBC__) && ((__GLIBC__ > 2) || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
static size_t get_allocated_bytes()
{
        struct mallinfo2 mi = mallinfo2();
        return (size_t)mi.uordblks;
}
#else
static size_t get_allocated_bytes()
{
        struct mallinfo mi = mallinfo();
        return (size_t)mi.uordblks;
}
#endif

static void cache_webproxy_value(ILibSimpleDataStore db, int index)
{
        char value[64];
        int len = snprintf(value, sizeof(value), "http://proxy-%d:8080", index);
        ILibSimpleDataStore_Cached(db, "WebProxy", 8, value, len + 1);
}

int main(int argc, char **argv)
{
        int iterations = 10000;
        if (argc > 1)
        {
                iterations = atoi(argv[1]);
                if (iterations <= 0) { iterations = 10000; }
        }

        ILibSimpleDataStore db = ILibSimpleDataStore_CreateCachedOnly();
        size_t before = get_allocated_bytes();

        for (int i = 0; i < iterations; ++i)
        {
                cache_webproxy_value(db, i);
        }

        size_t afterCached = get_allocated_bytes();

        ILibSimpleDataStore_DeleteEx(db, "WebProxy", 8);
        size_t afterDelete = get_allocated_bytes();

        ILibSimpleDataStore_Close(db);

        printf("iterations=%d allocated_before=%zu allocated_after_put=%zu allocated_after_delete=%zu\n",
                iterations, before, afterCached, afterDelete);
        printf("delta_put=%zd delta_delete=%zd\n",
                (ssize_t)(afterCached - before), (ssize_t)(afterDelete - before));

        return 0;
}

