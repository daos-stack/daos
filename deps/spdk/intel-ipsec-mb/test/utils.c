/*****************************************************************************
 Copyright (c) 2018-2021, Intel Corporation

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

     * Redistributions of source code must retain the above copyright notice,
       this list of conditions and the following disclaimer.
     * Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.
     * Neither the name of Intel Corporation nor the names of its contributors
       may be used to endorse or promote products derived from this software
       without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
 FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*****************************************************************************/

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "utils.h"
#include <intel-ipsec-mb.h>

/**
 * @brief Simplistic memory copy (intentionally not using libc)
 *
 * @param dst destination buffer pointer
 * @param src source buffer pointer
 * @param length length of the buffer to copy in bytes
 */
void memory_copy(void *dst, const void *src, size_t length)
{
        uint8_t *d = (uint8_t *) dst;
        const uint8_t *s = (const uint8_t *) src;

        while (length--)
                *d++ = *s++;
}

/**
 * @brief Simplistic memory set (intentionally not using libc)
 *
 * @param dst destination buffer pointer
 * @param val value to set each byte in destination buffer
 * @param length length of the buffer to copy in bytes
 */
void memory_set(void *dst, const int val, size_t length)
{
        uint8_t *d = (uint8_t *) dst;

        while (length--)
                *d++ = val;
}

/**
 * @brief Dumps fragment of memory in hex and ASCII into `fp`
 *
 * @note It is not multithread safe.
 * @note It works on buffer sizes up to 16,384 bytes.
 *
 * @param fp file stream to print into
 * @param msg optional extra header string message to print
 * @param p start address of data block to be dumped
 * @param len size of the data block to dump in bytes
 * @param start_ptr can be
 *          - pointer to data being dumped then first column of the dump will
 *            display addresses
 *          - NULL pointer then first column witll display indexes
 */
void
hexdump_ex(FILE *fp,
           const char *msg,
           const void *p,
           size_t len,
           const void *start_ptr)
{
        static uint8_t hex_buffer[16 * 1024];
        size_t ofs = 0;
        const unsigned char *data = hex_buffer;
        const char *start = (const char *) start_ptr;

        if (p == NULL)
                return;

        if (len > sizeof(hex_buffer))
                len = sizeof(hex_buffer);

        /*
         * Make copy of the buffer and work on it.
         * This is helping cases where stack area is printed and
         * libc API's put data on the stack
         */
        memory_copy(hex_buffer, p, len);

        if (msg != NULL)
                fprintf(fp, "%s\n", msg);

        while (ofs < len) {
                unsigned int i;

                fprintf(fp, "%p:", &start[ofs]);

                for (i = 0; ((ofs + i) < len) && (i < 16); i++)
                        fprintf(fp, " %02x", (data[ofs + i] & 0xff));

                for (; i <= 16; i++)
                        fprintf(fp, " | ");

                for (i = 0; (ofs < len) && (i < 16); i++, ofs++) {
                        unsigned char c = data[ofs];

                        if (!isprint(c))
                                c = '.';
                        fprintf(fp, "%c", c);
                }
                fprintf(fp, "\n");
        }
}

/**
 * @brief Simpler version of hexdump_ex() displaying data indexes only
 *
 * @param fp file stream to print into
 * @param msg optional extra header string message to print
 * @param p start address of data block to be dumped
 * @param len size of the data block to dump in bytes
 */
void
hexdump(FILE *fp,
        const char *msg,
        const void *p,
        size_t len)
{
        hexdump_ex(fp, msg, p, len, NULL);
}

/**
 * @brief Parse command line arguments and update arch_support
 *        and flags accordingly
 *
 * @param arg command line argument
 * @param arch_support table of supported architectures
 * @param flags MB manager flags to be passed to alloc_mb_mgr()
 *
 * @return Operation status
 * @retval 1 if \a arg was recognised
 * @retval 0 \a arg wasn't recognised
 * @retval -1 argument error
 */
int
update_flags_and_archs(const char *arg,
                       uint8_t arch_support[IMB_ARCH_NUM],
                       uint64_t *flags)
{
        int match = 1;

        if (arch_support == NULL || flags == NULL || arg == NULL) {
                fprintf(stderr, "Inputs not passed correctly\n");
                return -1;
        }

        if (strcmp(arg, "--no-avx512") == 0)
                arch_support[IMB_ARCH_AVX512] = 0;
        else if (strcmp(arg, "--no-avx2") == 0)
                arch_support[IMB_ARCH_AVX2] = 0;
        else if (strcmp(arg, "--no-avx") == 0)
                arch_support[IMB_ARCH_AVX] = 0;
        else if (strcmp(arg, "--no-sse") == 0)
                arch_support[IMB_ARCH_SSE] = 0;
        else if (strcmp(arg, "--aesni-emu") == 0)
                arch_support[IMB_ARCH_NOAESNI] = 1;
        else if (strcmp(arg, "--no-aesni-emu") == 0)
                arch_support[IMB_ARCH_NOAESNI] = 0;
        else if (strcmp(arg, "--shani-on") == 0)
                *flags &= (~IMB_FLAG_SHANI_OFF);
        else if (strcmp(arg, "--shani-off") == 0)
                *flags |= IMB_FLAG_SHANI_OFF;
        else
                match = 0;
        return match;
}

/**
 * @brief fill table of supported architectures
 *
 * @param arch_support table of supported architectures
 *
 * @return  Operation status
 * @retval 0 architectures identified correctly
 * @retval -1 bad input or issues with alloc_mb_mgr()
 */
int
detect_arch(uint8_t arch_support[IMB_ARCH_NUM])
{
        const uint64_t detect_sse =
                IMB_FEATURE_SSE4_2 | IMB_FEATURE_CMOV | IMB_FEATURE_AESNI;
        const uint64_t detect_avx =
                IMB_FEATURE_AVX | IMB_FEATURE_CMOV | IMB_FEATURE_AESNI;
        const uint64_t detect_avx2 = IMB_FEATURE_AVX2 | detect_avx;
        const uint64_t detect_avx512 = IMB_FEATURE_AVX512_SKX | detect_avx2;
        const uint64_t detect_noaesni = IMB_FEATURE_SSE4_2 | IMB_FEATURE_CMOV;

        IMB_MGR *p_mgr = NULL;
        IMB_ARCH arch_id;

        if (arch_support == NULL) {
                fprintf(stderr, "Inputs not passed correctly\n");
                return -1;
        }

        for (arch_id = IMB_ARCH_NOAESNI; arch_id < IMB_ARCH_NUM; arch_id++)
                arch_support[arch_id] = 1;

        p_mgr = alloc_mb_mgr(0);
        if (p_mgr == NULL) {
                fprintf(stderr, "Architecture detect error!\n");
                return -1;
        }

        if ((p_mgr->features & detect_avx512) != detect_avx512)
                arch_support[IMB_ARCH_AVX512] = 0;

        if ((p_mgr->features & detect_avx2) != detect_avx2)
                arch_support[IMB_ARCH_AVX2] = 0;

        if ((p_mgr->features & detect_avx) != detect_avx)
                arch_support[IMB_ARCH_AVX] = 0;

        if ((p_mgr->features & detect_sse) != detect_sse)
                arch_support[IMB_ARCH_SSE] = 0;

        if ((p_mgr->features & detect_noaesni) != detect_noaesni)
                arch_support[IMB_ARCH_NOAESNI] = 0;

        free_mb_mgr(p_mgr);

        if (arch_support[IMB_ARCH_NOAESNI] == 0 &&
            arch_support[IMB_ARCH_SSE] == 0 &&
            arch_support[IMB_ARCH_AVX] == 0 &&
            arch_support[IMB_ARCH_AVX2] == 0 &&
            arch_support[IMB_ARCH_AVX512] == 0) {
                fprintf(stderr, "No available architecture detected!\n");
                return -1;
        }

        return 0;
}

/**
 * @brief Print architecture name
 *
 * @param features value witch bits set for enabled features
 * @param arch architecture
 */
void
print_tested_arch(const uint64_t features, const IMB_ARCH arch)
{
        static const char *arch_str_tab[IMB_ARCH_NUM] = {
                "NONE", "NO-AESNI", "SSE", "AVX", "AVX2", "AVX512"
        };
        const char *feat = "";

        switch (arch) {
        case IMB_ARCH_NOAESNI:
        case IMB_ARCH_AVX2:
        case IMB_ARCH_AVX:
                break;
        case IMB_ARCH_SSE:
                if (features & IMB_FEATURE_SHANI) {
                        if ((features & IMB_FEATURE_GFNI))
                                feat = "-SHANI-GFNI";
                }
                break;
        case IMB_ARCH_AVX512:
                if ((features & IMB_FEATURE_VAES) &&
                    (features & IMB_FEATURE_GFNI) &&
                    (features & IMB_FEATURE_VPCLMULQDQ))
                        feat = "-VAES-GFNI-VCLMUL";
                break;
        default:
                printf("Invalid component\n");
                return;
        }

        printf("[INFO] [ARCH] using %s interface [%s%s]\n",
                arch_str_tab[arch],
                arch_str_tab[arch],
                feat);

}

/* =================================================================== */
/* =================================================================== */
/* BASIC TEST SUITE PASS/FAIL TRACKER API */
/* =================================================================== */
/* =================================================================== */

/**
 * @brief Start of the test suite
 *
 * @param ctx test suite context structure
 * @param alg_name name of the algorithm being tested
 */
void
test_suite_start(struct test_suite_context *ctx,
                 const char *alg_name)
{
        assert(ctx != NULL);
        assert(alg_name != NULL);

        ctx->alg_name = alg_name;
        ctx->pass = ctx->fail = 0;
}

/**
 * @brief Test result update
 *
 * It can be run after each test or after a group of tests.
 *
 * @param ctx test suite context structure
 * @param passed number of tests that passed
 * @param failed number of tests that failed
 */
void
test_suite_update(struct test_suite_context *ctx,
                  const unsigned passed,
                  const unsigned failed)
{
        assert(ctx != NULL);

        ctx->pass += passed;
        ctx->fail += failed;
}

/**
 * @brief Test suite end function
 *
 * Checks gathered stats and prints the message on the console
 *
 * @param ctx test suite context structure
 *
 * @return Operation status
 * @retval 0 all tests passed
 * @retval >0 failed tests detected, returning number of fails
 */
int test_suite_end(struct test_suite_context *ctx)
{
        const char *result = "PASS";
        int ret = 0;

        assert(ctx != NULL);

        if (ctx->fail > 0) {
                result = "FAIL";
                ret = (int) ctx->fail;
        }

        if (ctx->fail == 0 && ctx->pass == 0)
                result = "NOT_EXECUTED";

        printf("[INFO] [ALGO] %s %s\n", ctx->alg_name, result);

        return ret;
}

/** Generate random buffer */
void
generate_random_buf(uint8_t *buf, const uint32_t length)
{
        uint32_t i;

        for (i = 0; i < length; i++)
                buf[i] = (uint8_t) rand();
}

/** Compare two buffers at bit-level */
int membitcmp(const uint8_t *buf1, const uint8_t *buf2,
              const uint32_t bitlength, const uint32_t bitoffset)
{
        uint32_t bitresoffset;
        uint8_t bitresMask = ~((uint8_t)-1 << (8 - (bitoffset % 8)));
        uint32_t res = 0;
        uint32_t bytelengthfl = bitlength / 8;
        const uint8_t *buf1fl = buf1 + bitoffset / 8;
        const uint8_t *buf2fl = buf2 + bitoffset / 8;
        int index = 1;

        if (bitoffset % 8) {
                if ((*buf1fl ^ *buf2fl) & bitresMask) {
                        return 1;
                } else {
                        buf1fl++;
                        buf2fl++;
                }
        }
        bitresoffset = (bitlength + bitoffset) % 8;
        while (bytelengthfl--) {
                res = *buf1fl++ ^ *buf2fl++;
                if (res)
                        break;
                index++;
        }
        if ((bitresoffset) && (0 == bytelengthfl)) {
                res &= (uint8_t)-1 << (8 - bitresoffset);
                if (res)
                        return index;
        }
        return 0;
}
