/*****************************************************************************
 Copyright (c) 2020-2021, Intel Corporation

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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <intel-ipsec-mb.h>

#include "utils.h"

int crc_test(struct IMB_MGR *mb_mgr);

static uint32_t m_lut[256];
static struct IMB_MGR *p_mgr;

/**
 * @brief Reflects selected group of bits in \a v
 *
 * @param v value to be reflected
 * @param n size of the bit field to be reflected
 *
 * @return bit reflected value
 */
static uint64_t
reflect(uint64_t v, const uint32_t n)
{
        uint32_t i;
        uint64_t r = 0;

        for (i = 0; i < n; i++) {
                if (i != 0) {
                        r <<= 1;
                        v >>= 1;
                }
                r |= (v & 1);
        }

        return r;
}

/**
 * @brief Initializes reflected look-up-table (LUT) for given 32 bit polynomial
 *
 * @param poly CRC polynomial
 * @param rlut pointer to reflected 256x32bits look-up-table to be initialized
 */
static void
crc32_ref_init_lut(const uint32_t poly, uint32_t *rlut)
{
        uint_fast32_t i, j;

        if (rlut == NULL)
                return;

        for (i = 0; i < 256; i++) {
                /**
                 * i = reflect_8bits(i);
                 * crc = (i << 24);
                 */
                uint_fast32_t crc = (uint32_t) reflect(i, 32);

                for (j = 0; j < 8; j++) {
                        if (crc & 0x80000000UL)
                                crc = (crc << 1) ^ poly;
                        else
                                crc <<= 1;
                }

                rlut[i] = (uint32_t) reflect(crc, 32);
        }
}

/**
 * @brief Calculates 32 bit reflected CRC using LUT method.
 *
 * @param crc CRC initial value
 * @param data pointer to data block to calculate CRC for
 * @param data_len size of data block
 * @param reflected lut 256x32bits look-up-table pointer
 *
 * @return New CRC value
 */
static uint32_t
crc32_ref_calc_lut(const uint8_t *data,
                   uint64_t data_len,
                   uint32_t crc,
                   const uint32_t *rlut)
{
        if (data == NULL || rlut == NULL)
                return crc;

        while (data_len--)
                crc = rlut[(crc ^ *data++) & 0xffL] ^ (crc >> 8);

        return crc;
}

/**
 * @brief Initializes look-up-table (LUT) for given 32 bit polynomial
 *
 * @param poly CRC polynomial
 * @param lut pointer to 256 x 32bits look-up-table to be initialized
 */
static void
crc32_init_lut(const uint32_t poly, uint32_t *lut)
{
        uint_fast32_t i, j;

        if (lut == NULL)
                return;

        for (i = 0; i < 256; i++) {
                uint_fast32_t crc = (i << 24);

                for (j = 0; j < 8; j++)
                        if (crc & 0x80000000UL)
                                crc = (crc << 1) ^ poly;
                        else
                                crc <<= 1;

                lut[i] = crc;
        }
}

/**
 * @brief Calculates 32 bit CRC using LUT method.
 *
 * @param crc CRC initial value
 * @param data pointer to data block to calculate CRC for
 * @param data_len size of data block
 * @param lut 256x32bits look-up-table pointer
 *
 * @return New CRC value
 */
static uint32_t
crc32_calc_lut(const uint8_t *data,
               uint64_t data_len,
               uint32_t crc,
               const uint32_t *lut)
{
        if (data == NULL || lut == NULL)
                return crc;

        while (data_len--)
                crc = lut[(crc >> 24) ^ *data++] ^ (crc << 8);

        return crc;
}

/**
 * @brief Function randomizing buffer contents
 *
 * @param p pointer to the buffer
 * @param len number of bytes to be randomized
 */
static void
randomize_buffer(void *p, size_t len)
{
        uint8_t *p_byte = (uint8_t *) p;

        while (len--)
                *p_byte++ = (uint8_t) rand();
}

/**
 * @brief 32-bit polynomial CRC test function
 *
 * @param fn_crc_setup function to be called before the test for setup purpose
 * @param fn_crc_calc reference function computing CRC value
 * @param fn_crc tested CRC function (against the reference one)
 * @param title string banner printed on the screen
 *
 * @return test status
 * @retval 0 OK
 * @retval 1 error
 */
static int
test_crc_polynomial(void (*fn_crc_setup)(void),
                    uint32_t (*fn_crc_calc)(const void *, uint64_t),
                    uint32_t (*fn_crc)(const void *, uint64_t, const unsigned),
                    const char *title,
                    struct test_suite_context *ctx)
{
        uint8_t buffer[2048];
        size_t n;
        unsigned job_api;

        if (fn_crc_setup == NULL || fn_crc_calc == NULL ||
            fn_crc == NULL || title == NULL) {
                printf("crc_test: NULL parameter passed!\n");
                test_suite_update(ctx, 0, 1);
                return 1;
        } else {
                test_suite_update(ctx, 1, 0);
        }

        for (job_api = 0; job_api <= 1; job_api++) {
                if (job_api)
                        printf("Starting CRC Test (job API): %s\n", title);
                else
                        printf("Starting CRC Test (direct API): %s\n", title);

                fn_crc_setup();

                for (n = 0; n < sizeof(buffer); n++) {
                        uint32_t reference_crc, received_crc;

                        randomize_buffer(buffer, n);
                        reference_crc = fn_crc_calc(buffer, (uint64_t) n);
                        received_crc = fn_crc(buffer, (uint64_t) n, job_api);

                        if (reference_crc != received_crc) {
                                printf("! CRC mismatch for buffer size %lu, "
                                       "received = 0x%lx, expected = 0x%lx\n",
                                       (unsigned long) n,
                                       (unsigned long) received_crc,
                                       (unsigned long) reference_crc);
                                hexdump(stdout, "buffer content", buffer, n);
                                test_suite_update(ctx, 0, 1);
                                return 1;
                        } else {
                                test_suite_update(ctx, 1, 0);
                        }
                }
        }

        return 0;
}

/**
 * @brief CRC32 Ethernet FCS setup function
 */
static void
crc32_ethernet_fcs_setup(void)
{
        crc32_ref_init_lut(0x04c11db7UL, m_lut);
}

/**
 * @brief CRC32 Ethernet FCS reference calculation function
 *
 * @param p pointer to the buffer to calculate CRC on
 * @param len size of the buffer
 *
 * @return CRC value
 */
static uint32_t
crc32_ethernet_fcs_ref_calc(const void *p, uint64_t len)
{
        return ~crc32_ref_calc_lut(p, len, 0xffffffffUL, m_lut);
}

static uint32_t
crc_job(const void *p, const uint64_t len, IMB_HASH_ALG hash_alg)
{
        uint32_t auth_tag = 0;

        IMB_JOB *job;

        job = IMB_GET_NEXT_JOB(p_mgr);
        if (!job) {
                fprintf(stderr, "failed to get job\n");
                return 0;
        }

        job->cipher_mode                    = IMB_CIPHER_NULL;
        job->hash_alg                       = hash_alg;
        job->src                            = p;
        job->dst                            = NULL;
        job->msg_len_to_hash_in_bytes       = len;
        job->hash_start_src_offset_in_bytes = UINT64_C(0);
        job->auth_tag_output                = (uint8_t *) &auth_tag;
        job->auth_tag_output_len_in_bytes   = sizeof(auth_tag);

        job = IMB_SUBMIT_JOB(p_mgr);
        while (job) {
                if (job->status != IMB_STATUS_COMPLETED)
                        fprintf(stderr, "failed job, status:%d\n", job->status);
                job = IMB_GET_COMPLETED_JOB(p_mgr);
        }
        while ((job = IMB_FLUSH_JOB(p_mgr)) != NULL) {
                if (job->status != IMB_STATUS_COMPLETED)
                        fprintf(stderr, "failed job, status:%d\n", job->status);
        }

        return auth_tag;
}

/**
 * @brief CRC32 Ethernet FCS tested calculation function
 *
 * @param p pointer to the buffer to calculate CRC on
 * @param len size of the buffer
 *
 * @return CRC value
 */
static uint32_t
crc32_ethernet_fcs_tested_calc(const void *p, uint64_t len,
                               const unsigned job_api)
{
        if (job_api)
                return crc_job(p, len, IMB_AUTH_CRC32_ETHERNET_FCS);
        else
                return IMB_CRC32_ETHERNET_FCS(p_mgr, p, len);
}

/**
 * @brief CRC16 X25 setup function
 */
static void
crc16_x25_setup(void)
{
        crc32_ref_init_lut(0x10210000UL, m_lut);
}

/**
 * @brief CRC16 X25 reference calculation function
 *
 * @param p pointer to the buffer to calculate CRC on
 * @param len size of the buffer
 *
 * @return CRC value
 */
static uint32_t
crc16_x25_ref_calc(const void *p, uint64_t len)
{
        return (~crc32_ref_calc_lut(p, len, 0xffffUL, m_lut)) & 0xffff;
}

/**
 * @brief CRC16 X25 tested calculation function
 *
 * @param p pointer to the buffer to calculate CRC on
 * @param len size of the buffer
 *
 * @return CRC value
 */
static uint32_t
crc16_x25_tested_calc(const void *p, const uint64_t len, const unsigned job_api)
{
        if (job_api)
                return crc_job(p, len, IMB_AUTH_CRC16_X25);
        else
                return IMB_CRC16_X25(p_mgr, p, len);
}

/**
 * @brief CRC32 SCTP setup function
 */
static void
crc32_sctp_setup(void)
{
        crc32_init_lut(0x1edc6f41, m_lut);
}

/**
 * @brief CRC32 SCTP reference calculation function
 *
 * @param p pointer to the buffer to calculate CRC on
 * @param len size of the buffer
 *
 * @return CRC value
 */
static uint32_t
crc32_sctp_calc(const void *p, uint64_t len)
{
        return crc32_calc_lut(p, len, 0x0UL, m_lut);
}

/**
 * @brief CRC32 SCTP tested calculation function
 *
 * @param p pointer to the buffer to calculate CRC on
 * @param len size of the buffer
 *
 * @return CRC value
 */
static uint32_t
crc32_sctp_tested_calc(const void *p, uint64_t len, const unsigned job_api)
{
        if (job_api)
                return crc_job(p, len, IMB_AUTH_CRC32_SCTP);
        else
                return IMB_CRC32_SCTP(p_mgr, p, len);
}

/**
 * @brief CRC32 LTE24 A setup function
 *
 * 3GPP TS 36.212-880-Multiplexing and channel coding
 * LTE CRC24A polynomial 0x864CFB
 */
static void
crc32_lte24a_setup(void)
{
        crc32_init_lut(0x864CFBUL << 8, m_lut);
}

/**
 * @brief CRC32 LTE24A reference calculation function
 *
 * @param p pointer to the buffer to calculate CRC on
 * @param len size of the buffer
 *
 * @return CRC value
 */
static uint32_t
crc32_lte24a_calc(const void *p, uint64_t len)
{
        return crc32_calc_lut(p, len, 0x0UL, m_lut) >> 8;
}

/**
 * @brief CRC32 LTE24A tested calculation function
 *
 * @param p pointer to the buffer to calculate CRC on
 * @param len size of the buffer
 *
 * @return CRC value
 */
static uint32_t
crc32_lte24a_tested_calc(const void *p, uint64_t len, const unsigned job_api)
{
        if (job_api)
                return crc_job(p, len, IMB_AUTH_CRC24_LTE_A);
        else
                return IMB_CRC24_LTE_A(p_mgr, p, len);
}

/**
 * @brief CRC32 LTE24B setup function
 *
 * 3GPP TS 36.212-880-Multiplexing and channel coding
 * LTE CRC24A polynomial 0x800063
 */
static void
crc32_lte24b_setup(void)
{
        crc32_init_lut(0x800063UL << 8, m_lut);
}

/**
 * @brief CRC32 LTE24B reference calculation function
 *
 * @param p pointer to the buffer to calculate CRC on
 * @param len size of the buffer
 *
 * @return CRC value
 */
static uint32_t
crc32_lte24b_calc(const void *p, uint64_t len)
{
        return crc32_calc_lut(p, len, 0x0UL, m_lut) >> 8;
}

/**
 * @brief CRC32 LTE24B tested calculation function
 *
 * @param p pointer to the buffer to calculate CRC on
 * @param len size of the buffer
 *
 * @return CRC value
 */
static uint32_t
crc32_lte24b_tested_calc(const void *p, uint64_t len, const unsigned job_api)
{
        if (job_api)
                return crc_job(p, len, IMB_AUTH_CRC24_LTE_B);
        else
               return IMB_CRC24_LTE_B(p_mgr, p, len);
}

/**
 * @brief CRC16 Framing Protocol setup function
 *
 * 3GPP TS 25.435, 3GPP TS 25.427
 * Framing Protocol CRC polynomial
 * CRC16 0x8005 for data
 */
static void
crc16_fp_data_setup(void)
{
        crc32_init_lut(0x8005UL << 16, m_lut);
}

/**
 * @brief CRC16 Framing Protocol reference calculation function
 *
 * @param p pointer to the buffer to calculate CRC on
 * @param len size of the buffer
 *
 * @return CRC value
 */
static uint32_t
crc16_fp_data_calc(const void *p, uint64_t len)
{
        return crc32_calc_lut(p, len, 0x0UL, m_lut) >> 16;
}

/**
 * @brief CRC16 Framing Protocol tested calculation function
 *
 * @param p pointer to the buffer to calculate CRC on
 * @param len size of the buffer
 *
 * @return CRC value
 */
static uint32_t
crc16_fp_data_tested_calc(const void *p, uint64_t len, const unsigned job_api)
{
        if (job_api)
                return crc_job(p, len, IMB_AUTH_CRC16_FP_DATA);
        else
                return IMB_CRC16_FP_DATA(p_mgr, p, len);
}

/**
 * @brief CRC11 Framing Protocol setup function
 *
 * 3GPP TS 25.435, 3GPP TS 25.427
 * Framing Protocol CRC polynomial
 * CRC11 0x307 for EDCH header
 */
static void
crc11_fp_header_setup(void)
{
        crc32_init_lut(0x307UL << 21, m_lut);
}

/**
 * @brief CRC11 Framing Protocol reference calculation function
 *
 * @param p pointer to the buffer to calculate CRC on
 * @param len size of the buffer
 *
 * @return CRC value
 */
static uint32_t
crc11_fp_header_calc(const void *p, uint64_t len)
{
        return crc32_calc_lut(p, len, 0x0UL, m_lut) >> 21;
}

/**
 * @brief CRC11 Framing Protocol tested calculation function
 *
 * @param p pointer to the buffer to calculate CRC on
 * @param len size of the buffer
 *
 * @return CRC value
 */
static uint32_t
crc11_fp_header_tested_calc(const void *p, uint64_t len, const unsigned job_api)
{
        if (job_api)
                return crc_job(p, len, IMB_AUTH_CRC11_FP_HEADER);
        else
                return IMB_CRC11_FP_HEADER(p_mgr, p, len);
}

/**
 * @brief CRC7 Framing Protocol setup function
 *
 * 3GPP TS 25.435, 3GPP TS 25.427
 * Framing Protocol CRC polynomial
 * CRC7 0x45 for header
 */
static void
crc7_fp_header_setup(void)
{
        crc32_init_lut(0x45UL << 25, m_lut);
}

/**
 * @brief CRC7 Framing Protocol reference calculation function
 *
 * @param p pointer to the buffer to calculate CRC on
 * @param len size of the buffer
 *
 * @return CRC value
 */
static uint32_t
crc7_fp_header_calc(const void *p, uint64_t len)
{
        return crc32_calc_lut(p, len, 0x0UL, m_lut) >> 25;
}

/**
 * @brief CRC7 Framing Protocol tested calculation function
 *
 * @param p pointer to the buffer to calculate CRC on
 * @param len size of the buffer
 *
 * @return CRC value
 */
static uint32_t
crc7_fp_header_tested_calc(const void *p, uint64_t len, const unsigned job_api)
{
        if (job_api)
                return crc_job(p, len, IMB_AUTH_CRC7_FP_HEADER);
        else
               return IMB_CRC7_FP_HEADER(p_mgr, p, len);
}

/**
 * @brief CRC10 IUUP setup function
 *
 * 3GPP TS 25.415
 * IuUP CRC polynomial
 * CRC10 0x233 for data
 */
static void
crc10_iuup_data_setup(void)
{
        crc32_init_lut(0x233UL << 22, m_lut);
}

/**
 * @brief CRC10 IUUP reference calculation function
 *
 * @param p pointer to the buffer to calculate CRC on
 * @param len size of the buffer
 *
 * @return CRC value
 */
static uint32_t
crc10_iuup_data_calc(const void *p, uint64_t len)
{
        return crc32_calc_lut(p, len, 0x0UL, m_lut) >> 22;
}

/**
 * @brief CRC10 IUUP tested calculation function
 *
 * @param p pointer to the buffer to calculate CRC on
 * @param len size of the buffer
 *
 * @return CRC value
 */
static uint32_t
crc10_iuup_data_tested_calc(const void *p, uint64_t len, const unsigned job_api)
{
        if (job_api)
                return crc_job(p, len, IMB_AUTH_CRC10_IUUP_DATA);
        else
                return IMB_CRC10_IUUP_DATA(p_mgr, p, len);
}

/**
 * @brief CRC6 IUUP setup function
 *
 * 3GPP TS 25.415
 * IuUP CRC polynomial
 * CRC6 0x2f for header
 */
static void
crc6_iuup_header_setup(void)
{
        crc32_init_lut(0x2fUL << 26, m_lut);
}

/**
 * @brief CRC6 IUUP reference calculation function
 *
 * @param p pointer to the buffer to calculate CRC on
 * @param len size of the buffer
 *
 * @return CRC value
 */
static uint32_t
crc6_iuup_header_calc(const void *p, uint64_t len)
{
        return crc32_calc_lut(p, len, 0x0UL, m_lut) >> 26;
}

/**
 * @brief CRC6 IUUP tested calculation function
 *
 * @param p pointer to the buffer to calculate CRC on
 * @param len size of the buffer
 *
 * @return CRC value
 */
static uint32_t
crc6_iuup_header_tested_calc(const void *p, uint64_t len,
                             const unsigned job_api)
{
        if (job_api)
                return crc_job(p, len, IMB_AUTH_CRC6_IUUP_HEADER);
        else
               return IMB_CRC6_IUUP_HEADER(p_mgr, p, len);
}

/**
 * @brief CRC32 WIMAX OFDMA setup function
 *
 * WIMAX OFDMA DATA CRC32 0x4c11db7 (IEEE 802.16)
 */
static void
crc32_wimax_ofdma_data_setup(void)
{
        crc32_init_lut(0x4c11db7, m_lut);
}

/**
 * @brief CRC32 WIMAX OFDMA reference calculation function
 *
 * @param p pointer to the buffer to calculate CRC on
 * @param len size of the buffer
 *
 * @return CRC value
 */
static uint32_t
crc32_wimax_ofdma_data_calc(const void *p, uint64_t len)
{
        return ~crc32_calc_lut(p, len, 0xffffffffUL, m_lut);
}

/**
 * @brief CRC32 WIMAX OFDMA tested calculation function
 *
 * @param p pointer to the buffer to calculate CRC on
 * @param len size of the buffer
 *
 * @return CRC value
 */
static uint32_t
crc32_wimax_ofdma_data_tested_calc(const void *p, uint64_t len,
                                   const unsigned job_api)
{
        if (job_api)
                return crc_job(p, len, IMB_AUTH_CRC32_WIMAX_OFDMA_DATA);
        else
                return IMB_CRC32_WIMAX_OFDMA_DATA(p_mgr, p, len);
}

/**
 * @brief CRC8 HCS WIMAX OFDMA setup function
 *
 * WIMAX OFDMA CRC8 HCS 0x07 (IEEE 802.16)
 */
static void
crc8_wimax_ofdma_hcs_setup(void)
{
        crc32_init_lut(0x07 << 24, m_lut);
}

/**
 * @brief CRC8 HCS WIMAX OFDMA reference calculation function
 *
 * @param p pointer to the buffer to calculate CRC on
 * @param len size of the buffer
 *
 * @return CRC value
 */
static uint32_t
crc8_wimax_ofdma_hcs_calc(const void *p, uint64_t len)
{
        return crc32_calc_lut(p, len, 0x0UL, m_lut) >> 24;
}

/**
 * @brief CRC8 HCS WIMAX OFDMA tested calculation function
 *
 * @param p pointer to the buffer to calculate CRC on
 * @param len size of the buffer
 *
 * @return CRC value
 */
static uint32_t
crc8_wimax_ofdma_hcs_tested_calc(const void *p, uint64_t len,
                                 const unsigned job_api)
{
        if (job_api)
                return crc_job(p, len, IMB_AUTH_CRC8_WIMAX_OFDMA_HCS);
        else
                return IMB_CRC8_WIMAX_OFDMA_HCS(p_mgr, p, len);
}

int
crc_test(struct IMB_MGR *mb_mgr)
{
        struct test_suite_context ctx;
        int errors = 0;

        p_mgr = mb_mgr;

        srand(0x20200701);

        /* reflected CRC32 functions */

        test_suite_start(&ctx, "ETH-CRC32");
        test_crc_polynomial(crc32_ethernet_fcs_setup,
                            crc32_ethernet_fcs_ref_calc,
                            crc32_ethernet_fcs_tested_calc,
                            "CRC32 ETHERNET FCS 0x04c11db7", &ctx);
        errors += test_suite_end(&ctx);

        test_suite_start(&ctx, "X25-CRC16");
        test_crc_polynomial(crc16_x25_setup,
                            crc16_x25_ref_calc,
                            crc16_x25_tested_calc,
                            "CRC16 X25 0x1021", &ctx);
        errors += test_suite_end(&ctx);

        /* CRC32 functions */

        test_suite_start(&ctx, "SCTP-CRC32");
        test_crc_polynomial(crc32_sctp_setup,
                            crc32_sctp_calc,
                            crc32_sctp_tested_calc,
                            "CRC32 SCTP 0x1edc6f41 (Castagnoli93)", &ctx);
        errors += test_suite_end(&ctx);

        test_suite_start(&ctx, "LTE-A-CRC24");
        test_crc_polynomial(crc32_lte24a_setup,
                            crc32_lte24a_calc,
                            crc32_lte24a_tested_calc,
                            "LTE CRC24A 0x864cFB", &ctx);
        errors += test_suite_end(&ctx);

        test_suite_start(&ctx, "LTE-B-CRC24");
        test_crc_polynomial(crc32_lte24b_setup,
                            crc32_lte24b_calc,
                            crc32_lte24b_tested_calc,
                            "LTE CRC24B 0x800063", &ctx);
        errors += test_suite_end(&ctx);

        test_suite_start(&ctx, "FP-CRC16");
        test_crc_polynomial(crc16_fp_data_setup,
                            crc16_fp_data_calc,
                            crc16_fp_data_tested_calc,
                            "Framing Protocol Data CRC16 0x8005", &ctx);
        errors += test_suite_end(&ctx);

        test_suite_start(&ctx, "FP-CRC11");
        test_crc_polynomial(crc11_fp_header_setup,
                            crc11_fp_header_calc,
                            crc11_fp_header_tested_calc,
                            "Framing Protocol Header CRC11 0x307", &ctx);
        errors += test_suite_end(&ctx);

        test_suite_start(&ctx, "FP-CRC7");
        test_crc_polynomial(crc7_fp_header_setup,
                            crc7_fp_header_calc,
                            crc7_fp_header_tested_calc,
                            "Framing Protocol Header CRC7 0x45", &ctx);
        errors += test_suite_end(&ctx);

        test_suite_start(&ctx, "IUUP-CRC10");
        test_crc_polynomial(crc10_iuup_data_setup,
                            crc10_iuup_data_calc,
                            crc10_iuup_data_tested_calc,
                            "IUUP Data CRC10 0x233", &ctx);
        errors += test_suite_end(&ctx);

        test_suite_start(&ctx, "IUUP-CRC6");
        test_crc_polynomial(crc6_iuup_header_setup,
                            crc6_iuup_header_calc,
                            crc6_iuup_header_tested_calc,
                            "IUUP Header CRC6 0x2f", &ctx);
        errors += test_suite_end(&ctx);

        test_suite_start(&ctx, "WIMAX-OFDMA-CRC32");
        test_crc_polynomial(crc32_wimax_ofdma_data_setup,
                            crc32_wimax_ofdma_data_calc,
                            crc32_wimax_ofdma_data_tested_calc,
                            "WIMAX OFDMA CRC32 0x04c11db7", &ctx);
        errors += test_suite_end(&ctx);

        test_suite_start(&ctx, "WIMAX-OFDMA-CRC8");
        test_crc_polynomial(crc8_wimax_ofdma_hcs_setup,
                            crc8_wimax_ofdma_hcs_calc,
                            crc8_wimax_ofdma_hcs_tested_calc,
                            "WIMAX OFDMA CRC8 HCS 0x07", &ctx);
        errors += test_suite_end(&ctx);

	return errors;
}
