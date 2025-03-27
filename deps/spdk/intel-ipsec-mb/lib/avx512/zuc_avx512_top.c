/*******************************************************************************
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
*******************************************************************************/

/*-----------------------------------------------------------------------
* zuc_avx512_top.c
*-----------------------------------------------------------------------
* An implementation of ZUC, the core algorithm for the
* 3GPP Confidentiality and Integrity algorithms.
*
*-----------------------------------------------------------------------*/

#include <string.h>

#include "include/zuc_internal.h"
#include "include/wireless_common.h"
#include "include/save_xmms.h"
#include "include/clear_regs_mem.h"
#include "intel-ipsec-mb.h"
#include "include/error.h"

#define SAVE_XMMS               save_xmms
#define RESTORE_XMMS            restore_xmms
#define CLEAR_SCRATCH_SIMD_REGS clear_scratch_zmms

#define NUM_AVX512_BUFS 16

static inline uint32_t
find_min_length32(const uint32_t length[NUM_AVX512_BUFS])
{
        /* Calculate the minimum input packet size */
        static const uint64_t lo_mask[2] = {
                0x0d0c090805040100UL, 0xFFFFFFFFFFFFFFFFUL
        };
        static const uint64_t hi_mask[2] = {
                0xFFFFFFFFFFFFFFFFUL, 0x0d0c090805040100UL
        };
        const __m128i shuf_hi_mask =
                        _mm_loadu_si128((const __m128i *) hi_mask);
        const __m128i shuf_lo_mask =
                        _mm_loadu_si128((const __m128i *) lo_mask);
        __m128i xmm_lengths1, xmm_lengths2;

        /* Calculate the minimum input packet size from packets 0-7 */
        xmm_lengths1 = _mm_loadu_si128((const __m128i *) length);
        xmm_lengths2 = _mm_loadu_si128((const __m128i *) &length[4]);

        xmm_lengths1 = _mm_shuffle_epi8(xmm_lengths1, shuf_lo_mask);
        xmm_lengths2 = _mm_shuffle_epi8(xmm_lengths2, shuf_hi_mask);

        /* Contains array of 16-bit lengths */
        xmm_lengths1 = _mm_or_si128(xmm_lengths1, xmm_lengths2);

        xmm_lengths1 = _mm_minpos_epu16(xmm_lengths1);

        const uint32_t min_length1 =
                        (const uint32_t) _mm_extract_epi16(xmm_lengths1, 0);

        /* Calculate the minimum input packet size from packets 8-15 */
        xmm_lengths1 = _mm_loadu_si128((const __m128i *) &length[8]);
        xmm_lengths2 = _mm_loadu_si128((const __m128i *) &length[12]);

        xmm_lengths1 = _mm_shuffle_epi8(xmm_lengths1, shuf_lo_mask);
        xmm_lengths2 = _mm_shuffle_epi8(xmm_lengths2, shuf_hi_mask);

        /* Contains array of 16-bit lengths */
        xmm_lengths1 = _mm_or_si128(xmm_lengths1, xmm_lengths2);

        xmm_lengths1 = _mm_minpos_epu16(xmm_lengths1);

        const uint32_t min_length2 =
                        (const uint32_t) _mm_extract_epi16(xmm_lengths1, 0);

        /* Calculate the minimum input packet size from all packets */
        return (min_length1 < min_length2) ? min_length1 : min_length2;
}

static inline void
init_16(ZucKey16_t *keys, const uint8_t *ivs, ZucState16_t *state,
        const uint16_t lane_mask, const unsigned use_gfni)
{
        if (use_gfni)
                asm_ZucInitialization_16_gfni_avx512(keys, ivs, state,
                                                     lane_mask);
        else
                asm_ZucInitialization_16_avx512(keys, ivs, state, lane_mask);
}

static inline void
keystr_64B_gen_16(ZucState16_t *state, uint32_t *pKeyStr,
                  const unsigned key_off, const unsigned use_gfni)
{
        if (use_gfni)
                asm_ZucGenKeystream64B_16_gfni_avx512(state, pKeyStr, key_off);
        else
                asm_ZucGenKeystream64B_16_avx512(state, pKeyStr, key_off);
}

static inline void
keystr_8B_gen_16(ZucState16_t *state, uint32_t *pKeyStr,
                 const unsigned key_off,
                 const unsigned use_gfni)
{
        if (use_gfni)
                asm_ZucGenKeystream8B_16_gfni_avx512(state, pKeyStr, key_off);
        else
                asm_ZucGenKeystream8B_16_avx512(state, pKeyStr, key_off);
}

static inline void
cipher_16(ZucState16_t *pState, const uint64_t *pIn[16], uint64_t *pOut[16],
          const uint16_t lengths[16], const uint64_t minLength,
          const unsigned use_gfni)
{
        if (use_gfni)
                asm_ZucCipher_16_gfni_avx512(pState, pIn, pOut, lengths,
                                             minLength);
        else
                asm_ZucCipher_16_avx512(pState, pIn, pOut, lengths,
                                        minLength);
}

static inline void
round64B_16(uint32_t *T, const uint32_t *ks, const void **data,
            uint16_t *lens, const unsigned use_gfni)
{
        if (use_gfni)
                asm_Eia3Round64B_16_VPCLMUL(T, ks, data, lens);
        else
                asm_Eia3Round64BAVX512_16(T, ks, data, lens);
}

static inline
void _zuc_eea3_1_buffer_avx512(const void *pKey,
                               const void *pIv,
                               const void *pBufferIn,
                               void *pBufferOut,
                               const uint32_t length)
{
        DECLARE_ALIGNED(ZucState_t zucState, 64);
        DECLARE_ALIGNED(uint8_t keyStream[64], 64);

        const uint64_t *pIn64 = NULL;
        uint64_t *pOut64 = NULL, *pKeyStream64 = NULL;
        uint64_t *pTemp64 = NULL, *pdstTemp64 = NULL;

        uint32_t numKeyStreamsPerPkt = length/ ZUC_KEYSTR_LEN;
        uint32_t numBytesLeftOver = length % ZUC_KEYSTR_LEN;

        /* initialize the zuc state */
        asm_ZucInitialization_avx(pKey, pIv, &(zucState));

        /* Loop Over all the Quad-Words in input buffer and XOR with the 64bits
         * of generated keystream */
        pOut64 = (uint64_t *) pBufferOut;
        pIn64 = (const uint64_t *) pBufferIn;

        while (numKeyStreamsPerPkt--) {
                /* Generate the key stream 64 bytes at a time */
                asm_ZucGenKeystream64B_avx((uint32_t *) &keyStream[0],
                                            &zucState);

                /* XOR The Keystream generated with the input buffer here */
                pKeyStream64 = (uint64_t *) keyStream;
                asm_XorKeyStream64B_avx512(pIn64, pOut64, pKeyStream64);
                pIn64 += 8;
                pOut64 += 8;
        }

        /* Check for remaining 0 to 63 bytes */
        if (numBytesLeftOver) {
                /* buffer to store 64 bytes of keystream */
                DECLARE_ALIGNED(uint8_t tempSrc[64], 64);
                DECLARE_ALIGNED(uint8_t tempDst[64], 64);
                const uint8_t *pIn8 = (const uint8_t *) pBufferIn;
                uint8_t *pOut8 = (uint8_t *) pBufferOut;
                const uint64_t num4BRounds = ((numBytesLeftOver - 1) / 4) + 1;

                asm_ZucGenKeystream_avx((uint32_t *) &keyStream[0],
                                        &zucState, num4BRounds);

                /* copy the remaining bytes into temporary buffer and XOR with
                 * the 64-bytes of keystream. Then copy on the valid bytes back
                 * to the output buffer */

                memcpy(&tempSrc[0], &pIn8[length - numBytesLeftOver],
                       numBytesLeftOver);
                pKeyStream64 = (uint64_t *) &keyStream[0];
                pTemp64 = (uint64_t *) &tempSrc[0];
                pdstTemp64 = (uint64_t *) &tempDst[0];

                asm_XorKeyStream64B_avx512(pTemp64, pdstTemp64, pKeyStream64);
                memcpy(&pOut8[length - numBytesLeftOver], &tempDst[0],
                       numBytesLeftOver);

#ifdef SAFE_DATA
                clear_mem(tempSrc, sizeof(tempSrc));
                clear_mem(tempDst, sizeof(tempDst));
#endif
        }
#ifdef SAFE_DATA
        /* Clear sensitive data in stack */
        clear_mem(keyStream, sizeof(keyStream));
        clear_mem(&zucState, sizeof(zucState));
#endif
}

static inline
void _zuc_eea3_16_buffer_avx512(const void * const pKey[NUM_AVX512_BUFS],
                                const void * const pIv[NUM_AVX512_BUFS],
                                const void * const pBufferIn[NUM_AVX512_BUFS],
                                void *pBufferOut[NUM_AVX512_BUFS],
                                const uint32_t length[NUM_AVX512_BUFS],
                                const unsigned use_gfni)
{
        DECLARE_ALIGNED(ZucState16_t state, 64);
        DECLARE_ALIGNED(ZucState_t singlePktState, 64);
        unsigned int i = 0;
        /* Calculate the minimum input packet size from all packets */
        uint16_t bytes = (uint16_t) find_min_length32(length);

        uint32_t numKeyStreamsPerPkt;
        DECLARE_ALIGNED(uint16_t remainBytes[NUM_AVX512_BUFS], 32) = {0};
        DECLARE_ALIGNED(uint8_t keyStr[NUM_AVX512_BUFS][64], 64);
        /* structure to store the 16 keys */
        DECLARE_ALIGNED(ZucKey16_t keys, 64);
        /* structure to store the 16 IV's */
        DECLARE_ALIGNED(uint8_t ivs[NUM_AVX512_BUFS*32], 16);
        uint32_t numBytesLeftOver = 0;
        const uint8_t *pTempBufInPtr = NULL;
        uint8_t *pTempBufOutPtr = NULL;

        DECLARE_ALIGNED(const uint64_t *pIn64[NUM_AVX512_BUFS], 64) = {NULL};
        DECLARE_ALIGNED(uint64_t *pOut64[NUM_AVX512_BUFS], 64) = {NULL};
        uint64_t *pKeyStream64 = NULL;

        /*
         * Calculate the number of bytes left over for each packet,
         * and setup the Keys and IVs
         */
        for (i = 0; i < NUM_AVX512_BUFS; i++) {
                remainBytes[i] = length[i];
                keys.pKeys[i] = pKey[i];
                memcpy(ivs + i*32, pIv[i], 16);
        }

        init_16(&keys, ivs, &state, 0xFFFF, use_gfni);

        for (i = 0; i < NUM_AVX512_BUFS; i++) {
                pOut64[i] = (uint64_t *) pBufferOut[i];
                pIn64[i] = (const uint64_t *) pBufferIn[i];
        }

        cipher_16(&state, pIn64, pOut64, remainBytes, bytes, use_gfni);

        /* process each packet separately for the remaining bytes */
        for (i = 0; i < NUM_AVX512_BUFS; i++) {
                if (remainBytes[i]) {
                        /* need to copy the zuc state to single packet state */
                        singlePktState.lfsrState[0] = state.lfsrState[0][i];
                        singlePktState.lfsrState[1] = state.lfsrState[1][i];
                        singlePktState.lfsrState[2] = state.lfsrState[2][i];
                        singlePktState.lfsrState[3] = state.lfsrState[3][i];
                        singlePktState.lfsrState[4] = state.lfsrState[4][i];
                        singlePktState.lfsrState[5] = state.lfsrState[5][i];
                        singlePktState.lfsrState[6] = state.lfsrState[6][i];
                        singlePktState.lfsrState[7] = state.lfsrState[7][i];
                        singlePktState.lfsrState[8] = state.lfsrState[8][i];
                        singlePktState.lfsrState[9] = state.lfsrState[9][i];
                        singlePktState.lfsrState[10] = state.lfsrState[10][i];
                        singlePktState.lfsrState[11] = state.lfsrState[11][i];
                        singlePktState.lfsrState[12] = state.lfsrState[12][i];
                        singlePktState.lfsrState[13] = state.lfsrState[13][i];
                        singlePktState.lfsrState[14] = state.lfsrState[14][i];
                        singlePktState.lfsrState[15] = state.lfsrState[15][i];

                        singlePktState.fR1 = state.fR1[i];
                        singlePktState.fR2 = state.fR2[i];

                        numKeyStreamsPerPkt = remainBytes[i] / ZUC_KEYSTR_LEN;
                        numBytesLeftOver = remainBytes[i]  % ZUC_KEYSTR_LEN;

                        pTempBufInPtr = pBufferIn[i];
                        pTempBufOutPtr = pBufferOut[i];

                        /* update the output and input pointers here to point
                         * to the i'th buffers */
                        pOut64[0] = (uint64_t *) &pTempBufOutPtr[length[i] -
                                                                remainBytes[i]];
                        pIn64[0] = (const uint64_t *) &pTempBufInPtr[length[i] -
                                                                remainBytes[i]];

                        while (numKeyStreamsPerPkt--) {
                                /* Generate the key stream 64 bytes at a time */
                                asm_ZucGenKeystream64B_avx(
                                                       (uint32_t *) keyStr[0],
                                                       &singlePktState);
                                pKeyStream64 = (uint64_t *) keyStr[0];
                                asm_XorKeyStream64B_avx512(pIn64[0], pOut64[0],
                                                           pKeyStream64);
                                pIn64[0] += 8;
                                pOut64[0] += 8;
                        }


                        /* Check for remaining 0 to 63 bytes */
                        if (numBytesLeftOver) {
                                DECLARE_ALIGNED(uint8_t tempSrc[64], 64);
                                DECLARE_ALIGNED(uint8_t tempDst[64], 64);
                                uint64_t *pTempSrc64;
                                uint64_t *pTempDst64;
                                uint32_t offset = length[i] - numBytesLeftOver;
                                const uint64_t num4BRounds =
                                        ((numBytesLeftOver - 1) / 4) + 1;

                                asm_ZucGenKeystream_avx((uint32_t *)&keyStr[0],
                                                        &singlePktState,
                                                        num4BRounds);
                                /* copy the remaining bytes into temporary
                                 * buffer and XOR with the 64-bytes of
                                 * keystream. Then copy on the valid bytes back
                                 * to the output buffer */
                                memcpy(&tempSrc[0], &pTempBufInPtr[offset],
                                       numBytesLeftOver);
                                memset(&tempSrc[numBytesLeftOver], 0,
                                       64 - numBytesLeftOver);

                                pKeyStream64 = (uint64_t *) &keyStr[0][0];
                                pTempSrc64 = (uint64_t *) &tempSrc[0];
                                pTempDst64 = (uint64_t *) &tempDst[0];
                                asm_XorKeyStream64B_avx512(pTempSrc64,
                                                           pTempDst64,
                                                           pKeyStream64);

                                memcpy(&pTempBufOutPtr[offset],
                                       &tempDst[0], numBytesLeftOver);
#ifdef SAFE_DATA
                                clear_mem(tempSrc, sizeof(tempSrc));
                                clear_mem(tempDst, sizeof(tempDst));
#endif
                        }
                }
        }
#ifdef SAFE_DATA
        /* Clear sensitive data in stack */
        clear_mem(keyStr, sizeof(keyStr));
        clear_mem(&singlePktState, sizeof(singlePktState));
        clear_mem(&state, sizeof(state));
        clear_mem(&keys, sizeof(keys));
#endif
}

void zuc_eea3_1_buffer_avx512(const void *pKey,
                              const void *pIv,
                              const void *pBufferIn,
                              void *pBufferOut,
                              const uint32_t length)
{
#ifndef LINUX
        DECLARE_ALIGNED(imb_uint128_t xmm_save[10], 16);

        SAVE_XMMS(xmm_save);
#endif
#ifdef SAFE_PARAM
        imb_set_errno(NULL, 0);
        /* Check for NULL pointers */
        if (pKey == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_KEY);
                return;
        }

        if (pIv == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_IV);
                return;
        }

        if (pBufferIn == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_SRC);
                return;
        }

        if (pBufferOut == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_DST);
                return;
        }

        /* Check input data is in range of supported length */
        if (length < ZUC_MIN_BYTELEN || length > ZUC_MAX_BYTELEN) {
                imb_set_errno(NULL, IMB_ERR_CIPH_LEN);
                return;
        }
#endif
        _zuc_eea3_1_buffer_avx512(pKey, pIv, pBufferIn, pBufferOut, length);

#ifdef SAFE_DATA
        /* Clear sensitive data in registers */
        CLEAR_SCRATCH_GPS();
        CLEAR_SCRATCH_SIMD_REGS();
#endif
#ifndef LINUX
        RESTORE_XMMS(xmm_save);
#endif
}

static inline
void _zuc_eea3_n_buffer(const void * const pKey[],
                        const void * const pIv[],
                        const void * const pBufferIn[],
                        void *pBufferOut[],
                        const uint32_t length[],
                        const uint32_t numBuffers,
                        const unsigned use_gfni)
{
#ifndef LINUX
        DECLARE_ALIGNED(imb_uint128_t xmm_save[10], 16);

        SAVE_XMMS(xmm_save);
#endif

        unsigned int i;
        unsigned int packetCount = numBuffers;

#ifdef SAFE_PARAM
        imb_set_errno(NULL, 0);

        /* Check for NULL pointers */
        if (pKey == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_KEY);
                return;
        }

        if (pIv == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_IV);
                return;
        }

        if (pBufferIn == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_SRC);
                return;
        }

        if (pBufferOut == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_DST);
                return;
        }

        if (length == NULL) {
                imb_set_errno(NULL, IMB_ERR_CIPH_LEN);
                return;
        }

        /* Check for NULL pointers and lengths for each buffer */
        for (i = 0; i < numBuffers; i++) {
                if (pKey[i] == NULL) {
                        imb_set_errno(NULL, IMB_ERR_NULL_KEY);
                        return;
                }

                if (pIv[i] == NULL) {
                        imb_set_errno(NULL, IMB_ERR_NULL_IV);
                        return;
                }

                if (pBufferIn[i] == NULL) {
                        imb_set_errno(NULL, IMB_ERR_NULL_SRC);
                        return;
                }

                if (pBufferOut[i] == NULL) {
                        imb_set_errno(NULL, IMB_ERR_NULL_DST);
                        return;
                }

                /* Check input data is in range of supported length */
                if (length[i] < ZUC_MIN_BYTELEN ||
                    length[i] > ZUC_MAX_BYTELEN) {
                        imb_set_errno(NULL, IMB_ERR_CIPH_LEN);
                        return;
                }
        }
#endif
        i = 0;

        while(packetCount >= 16) {
                packetCount -= 16;
                _zuc_eea3_16_buffer_avx512(&pKey[i],
                                           &pIv[i],
                                           &pBufferIn[i],
                                           &pBufferOut[i],
                                           &length[i],
                                           use_gfni);
                i += 16;
        }

        while(packetCount >= 8) {
                packetCount -= 8;
                _zuc_eea3_8_buffer_avx2(&pKey[i],
                                        &pIv[i],
                                        &pBufferIn[i],
                                        &pBufferOut[i],
                                        &length[i]);
                i += 8;
        }

        while(packetCount >= 4) {
                packetCount -= 4;
                _zuc_eea3_4_buffer_avx(&pKey[i],
                                       &pIv[i],
                                       &pBufferIn[i],
                                       &pBufferOut[i],
                                       &length[i]);
                i += 4;
        }

        while(packetCount--) {
                _zuc_eea3_1_buffer_avx512(pKey[i],
                                          pIv[i],
                                          pBufferIn[i],
                                          pBufferOut[i],
                                          length[i]);
                i++;
        }
#ifdef SAFE_DATA
        /* Clear sensitive data in registers */
        CLEAR_SCRATCH_GPS();
        CLEAR_SCRATCH_SIMD_REGS();
#endif
        _mm256_zeroupper();
#ifndef LINUX
        RESTORE_XMMS(xmm_save);
#endif
}

void zuc_eea3_n_buffer_avx512(const void * const pKey[],
                              const void * const pIv[],
                              const void * const pBufferIn[],
                              void *pBufferOut[],
                              const uint32_t length[],
                              const uint32_t numBuffers)
{
        _zuc_eea3_n_buffer(pKey, pIv, pBufferIn, pBufferOut,
                           length, numBuffers, 0);
}

void zuc_eea3_n_buffer_gfni_avx512(const void * const pKey[],
                                   const void * const pIv[],
                                   const void * const pBufferIn[],
                                   void *pBufferOut[],
                                   const uint32_t length[],
                                   const uint32_t numBuffers)
{
        _zuc_eea3_n_buffer(pKey, pIv, pBufferIn, pBufferOut,
                           length, numBuffers, 1);
}

static inline
void _zuc_eia3_1_buffer_avx512(const void *pKey,
                               const void *pIv,
                               const void *pBufferIn,
                               const uint32_t lengthInBits,
                               uint32_t *pMacI)
{
        DECLARE_ALIGNED(ZucState_t zucState, 64);
        DECLARE_ALIGNED(uint32_t keyStream[16 * 2], 64);
        const uint32_t keyStreamLengthInBits = ZUC_KEYSTR_LEN * 8;
        /* generate a key-stream 2 words longer than the input message */
        const uint32_t N = lengthInBits + (2 * ZUC_WORD_BITS);
        uint32_t L = (N + 31) / ZUC_WORD_BITS;
        uint32_t *pZuc = (uint32_t *) &keyStream[0];
        uint32_t remainingBits = lengthInBits;
        uint32_t T = 0;
        const uint8_t *pIn8 = (const uint8_t *) pBufferIn;

        asm_ZucInitialization_avx(pKey, pIv, &(zucState));
        asm_ZucGenKeystream64B_avx(pZuc, &zucState);

        /* loop over the message bits */
        while (remainingBits >= keyStreamLengthInBits) {
                remainingBits -=  keyStreamLengthInBits;
                L -= (keyStreamLengthInBits / 32);
                /* Generate the next key stream 8 bytes or 64 bytes */
                if (!remainingBits)
                        asm_ZucGenKeystream8B_avx(&keyStream[16], &zucState);
                else
                        asm_ZucGenKeystream64B_avx(&keyStream[16], &zucState);
                asm_Eia3Round64BAVX512(&T, &keyStream[0], pIn8);
                /* Copy the last keystream generated
                 * to the first 64 bytes */
                memcpy(&keyStream[0], &keyStream[16], 64);
                pIn8 = &pIn8[ZUC_KEYSTR_LEN];
        }

        /*
         * If remaining bits has more than 14 ZUC WORDS (double words),
         * keystream needs to have up to another 2 ZUC WORDS (8B)
         */
        if (remainingBits > (14 * 32))
                asm_ZucGenKeystream8B_avx(&keyStream[16], &zucState);
        asm_Eia3RemainderAVX512(&T, &keyStream[0], pIn8, remainingBits);
        *pMacI = T;
}

/*
 * Returns the offset of where the keystream starts for a specific buffer,
 * in memory. The keystream for each buffer is scattered in memory,
 * interleaving chunks of 16 bytes, with 128 bytes of keystream in total for
 * each buffer.
 * The memory is laid out in the following way:
 * [B_0[15:0] B_4[15:0] B_8[15:0] B_12[15:0]
 *  B_0[31:16] B_4[31:16] B_8[31:16] B_12[31:16]
 *  B_0[47:32] B_4[47:32] B_8[47:32] B_12[47:32]
 *  B_0[63:48] B_4[63:48] B_8[63:48] B_12[63:48]
 *  B_0[79:64] B_4[79:64] B_8[79:64] B_12[79:64]
 *  B_0[95:80] B_4[95:80] B_8[95:80] B_12[95:80]
 *  B_0[111:96] B_4[111:96] B_8[111:96] B_12[111:96]
 *  B_0[127:112] B_4[127:112] B_8[127:112] B_13[127:112]
 *  B_1[15:0] B_5[15:0] B_9[15:0] B_13[15:0]
 *  B_1[31:16] B_5[31:16] B_9[31:16] B_13[31:16]
 *  B_1[47:32] B_5[47:32] B_9[47:32] B_13[47:32]
 *  B_1[63:48] B_5[63:48] B_9[63:48] B_13[63:48]
 *  B_1[79:64] B_5[79:64] B_9[79:64] B_13[79:64]
 *  B_1[95:80] B_5[95:80] B_9[95:80] B_13[95:80]
 *  B_1[111:96] B_5[111:96] B_9[111:96] B_13[111:96]
 *  B_1[127:112] B_5[127:112] B_9[127:112] B_13[127:112]
 * ... ]
 */
static inline
unsigned get_start_key_addr(const unsigned buf_idx)
{
        const unsigned idx_l = buf_idx & 0x3;
        const unsigned idx_h = buf_idx >> 2;

        return idx_l*128 + idx_h*4;
}

static inline
void _zuc_eia3_16_buffer_avx512(const void * const pKey[NUM_AVX512_BUFS],
                                const void * const pIv[NUM_AVX512_BUFS],
                                const void * const pBufferIn[NUM_AVX512_BUFS],
                                const uint32_t lengthInBits[NUM_AVX512_BUFS],
                                uint32_t *pMacI[NUM_AVX512_BUFS],
                                const unsigned use_gfni)
{
        unsigned int i = 0;
        DECLARE_ALIGNED(ZucState16_t state, 64);
        DECLARE_ALIGNED(ZucState_t singlePktState, 64);
        /* Calculate the minimum input packet size from all packets */
        uint32_t commonBits = find_min_length32(lengthInBits);
        DECLARE_ALIGNED(uint32_t keyStr[NUM_AVX512_BUFS*2*16], 64);
        /* structure to store the 16 keys */
        DECLARE_ALIGNED(ZucKey16_t keys, 64);
        /* structure to store the 16 IV's */
        DECLARE_ALIGNED(uint8_t ivs[NUM_AVX512_BUFS*32], 16);
        const uint8_t *pIn8[NUM_AVX512_BUFS] = {NULL};
        uint32_t remainCommonBits = commonBits;
        uint32_t numKeyStr = 0;
        uint32_t T[NUM_AVX512_BUFS] = {0};
        const uint32_t keyStreamLengthInBits = ZUC_KEYSTR_LEN * 8;
        DECLARE_ALIGNED(uint16_t lens[NUM_AVX512_BUFS], 32);

        for (i = 0; i < NUM_AVX512_BUFS; i++) {
                pIn8[i] = (const uint8_t *) pBufferIn[i];
                keys.pKeys[i] = pKey[i];
                memcpy(ivs + i*32, pIv[i], 16);
                lens[i] = (uint16_t) lengthInBits[i];
        }

        init_16(&keys, ivs, &state, 0xFFFF, use_gfni);
        /* Generate 64 bytes at a time */
        keystr_64B_gen_16(&state, keyStr, 0, use_gfni);

        /* loop over the message bits */
        while (remainCommonBits >= keyStreamLengthInBits) {
                remainCommonBits -= keyStreamLengthInBits;
                numKeyStr++;
                /* Generate the next key stream 8 bytes or 64 bytes */
                if (!remainCommonBits)
                        keystr_8B_gen_16(&state, keyStr,
                                     64, use_gfni);
                else
                        keystr_64B_gen_16(&state, keyStr, 64, use_gfni);
                round64B_16(T, keyStr,
                            (const void **)pIn8, lens, use_gfni);
        }

        /* Process each packet separately for the remaining bits */
        for (i = 0; i < NUM_AVX512_BUFS; i++) {
                const uint32_t N = lengthInBits[i] + (2 * ZUC_WORD_BITS);
                uint32_t L = ((N + 31) / ZUC_WORD_BITS) -
                             numKeyStr*(keyStreamLengthInBits / 32);
                uint32_t remainBits = lengthInBits[i] -
                                      numKeyStr*keyStreamLengthInBits;
                uint32_t keyStr32[16*2];
                unsigned j;

                /*
                 * Copy 128 bytes of keystream scattered in chunks of 16 bytes
                 * to be in contiguous memory
                 */
                for (j = 0; j < 8; j++)
                        memcpy(keyStr32 + j*4,
                               &keyStr[get_start_key_addr(i) + j*16], 16);

                /* If remaining bits are more than 56 bytes, we need to generate
                 * at least 8B more of keystream, so we need to copy
                 * the zuc state to single packet state first */
                if (remainBits > (14*32)) {
                        singlePktState.lfsrState[0] = state.lfsrState[0][i];
                        singlePktState.lfsrState[1] = state.lfsrState[1][i];
                        singlePktState.lfsrState[2] = state.lfsrState[2][i];
                        singlePktState.lfsrState[3] = state.lfsrState[3][i];
                        singlePktState.lfsrState[4] = state.lfsrState[4][i];
                        singlePktState.lfsrState[5] = state.lfsrState[5][i];
                        singlePktState.lfsrState[6] = state.lfsrState[6][i];
                        singlePktState.lfsrState[7] = state.lfsrState[7][i];
                        singlePktState.lfsrState[8] = state.lfsrState[8][i];
                        singlePktState.lfsrState[9] = state.lfsrState[9][i];
                        singlePktState.lfsrState[10] = state.lfsrState[10][i];
                        singlePktState.lfsrState[11] = state.lfsrState[11][i];
                        singlePktState.lfsrState[12] = state.lfsrState[12][i];
                        singlePktState.lfsrState[13] = state.lfsrState[13][i];
                        singlePktState.lfsrState[14] = state.lfsrState[14][i];
                        singlePktState.lfsrState[15] = state.lfsrState[15][i];

                        singlePktState.fR1 = state.fR1[i];
                        singlePktState.fR2 = state.fR2[i];
                }

                while (remainBits >= keyStreamLengthInBits) {
                        remainBits -= keyStreamLengthInBits;
                        L -= (keyStreamLengthInBits / 32);

                        /* Generate the next key stream 8 bytes or 64 bytes */
                        if (!remainBits)
                                asm_ZucGenKeystream8B_avx(&keyStr32[16],
                                                          &singlePktState);
                        else
                                asm_ZucGenKeystream64B_avx(&keyStr32[16],
                                                           &singlePktState);
                        asm_Eia3Round64BAVX512(&T[i], &keyStr32[0], pIn8[i]);
                        /* Copy the last keystream generated
                         * to the first 64 bytes */
                        memcpy(keyStr32, &keyStr32[16], 64);
                        pIn8[i] = &pIn8[i][ZUC_KEYSTR_LEN];
                }

                /*
                 * If remaining bits has more than 14 ZUC WORDS (double words),
                 * keystream needs to have up to another 2 ZUC WORDS (8B)
                 */

                if (remainBits > (14 * 32))
                        asm_ZucGenKeystream8B_avx(&keyStr32[16],
                                                  &singlePktState);

                asm_Eia3RemainderAVX512(&T[i], keyStr32, pIn8[i], remainBits);
                *(pMacI[i]) = T[i];
        }

#ifdef SAFE_DATA
        /* Clear sensitive data (in registers and stack) */
        clear_mem(keyStr, sizeof(keyStr));
        clear_mem(&singlePktState, sizeof(singlePktState));
        clear_mem(&state, sizeof(state));
        clear_mem(&keys, sizeof(keys));
#endif
}

void zuc_eia3_1_buffer_avx512(const void *pKey,
                              const void *pIv,
                              const void *pBufferIn,
                              const uint32_t lengthInBits,
                              uint32_t *pMacI)
{
#ifndef LINUX
        DECLARE_ALIGNED(imb_uint128_t xmm_save[10], 16);

        SAVE_XMMS(xmm_save);
#endif
#ifdef SAFE_PARAM
        imb_set_errno(NULL, 0);
        /* Check for NULL pointers */
        if (pKey == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_KEY);
                return;
        }

        if (pIv == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_IV);
                return;
        }

        if (pBufferIn == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_SRC);
                return;
        }

        if (pMacI == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_AUTH);
                return;
        }

        /* Check input data is in range of supported length */
        if (lengthInBits < ZUC_MIN_BITLEN || lengthInBits > ZUC_MAX_BITLEN) {
                imb_set_errno(NULL, IMB_ERR_AUTH_LEN);
                return;
        }
#endif

        _zuc_eia3_1_buffer_avx512(pKey, pIv, pBufferIn, lengthInBits, pMacI);

#ifdef SAFE_DATA
        CLEAR_SCRATCH_GPS();
        CLEAR_SCRATCH_SIMD_REGS();
#endif
#ifndef LINUX
        RESTORE_XMMS(xmm_save);
#endif
}

static inline
void _zuc_eia3_n_buffer(const void * const pKey[],
                        const void * const pIv[],
                        const void * const pBufferIn[],
                        const uint32_t lengthInBits[],
                        uint32_t *pMacI[],
                        const uint32_t numBuffers,
                        const unsigned use_gfni)
{
#ifndef LINUX
        DECLARE_ALIGNED(imb_uint128_t xmm_save[10], 16);

        SAVE_XMMS(xmm_save);
#endif

        unsigned int i;
        unsigned int packetCount = numBuffers;

#ifdef SAFE_PARAM
        imb_set_errno(NULL, 0);

        /* Check for NULL pointers */
        if (pKey == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_KEY);
                return;
        }

        if (pIv == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_IV);
                return;
        }

        if (pBufferIn == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_SRC);
                return;
        }

        if (pMacI == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_AUTH);
                return;
        }

        if (lengthInBits == NULL) {
                imb_set_errno(NULL, IMB_ERR_AUTH_LEN);
                return;
        }

        /* Check for NULL pointers and lengths for each buffer */
        for (i = 0; i < numBuffers; i++) {
                if (pKey[i] == NULL) {
                        imb_set_errno(NULL, IMB_ERR_NULL_KEY);
                        return;
                }

                if (pIv[i] == NULL) {
                        imb_set_errno(NULL, IMB_ERR_NULL_IV);
                        return;
                }

                if (pBufferIn[i] == NULL) {
                        imb_set_errno(NULL, IMB_ERR_NULL_SRC);
                        return;
                }

                if (pMacI[i] == NULL) {
                        imb_set_errno(NULL, IMB_ERR_NULL_AUTH);
                        return;
                }

                /* Check input data is in range of supported length */
                if (lengthInBits[i] < ZUC_MIN_BITLEN ||
                    lengthInBits[i] > ZUC_MAX_BITLEN) {
                        imb_set_errno(NULL, IMB_ERR_AUTH_LEN);
                        return;
                }
        }
#endif
        i = 0;

        while(packetCount >= 16) {
                packetCount -= 16;
                _zuc_eia3_16_buffer_avx512(&pKey[i],
                                           &pIv[i],
                                           &pBufferIn[i],
                                           &lengthInBits[i],
                                           &pMacI[i],
                                           use_gfni);
                i += 16;
        }

        if (packetCount >= 8) {
                packetCount -= 8;
                _zuc_eia3_8_buffer_avx2(&pKey[i],
                                        &pIv[i],
                                        &pBufferIn[i],
                                        &lengthInBits[i],
                                        &pMacI[i]);
                i += 8;
        }

        if (packetCount >= 4) {
                packetCount -= 4;
                _zuc_eia3_4_buffer_avx(&pKey[i],
                                       &pIv[i],
                                       &pBufferIn[i],
                                       &lengthInBits[i],
                                       &pMacI[i]);
                i += 4;
        }
        while(packetCount--) {
                _zuc_eia3_1_buffer_avx512(pKey[i],
                                          pIv[i],
                                          pBufferIn[i],
                                          lengthInBits[i],
                                          pMacI[i]);
                i++;
        }

        _mm256_zeroupper();

#ifdef SAFE_DATA
        /* Clear sensitive data in registers */
        CLEAR_SCRATCH_GPS();
        CLEAR_SCRATCH_SIMD_REGS();
#endif
#ifndef LINUX
        RESTORE_XMMS(xmm_save);
#endif
}

void zuc_eia3_n_buffer_avx512(const void * const pKey[],
                              const void * const pIv[],
                              const void * const pBufferIn[],
                              const uint32_t lengthInBits[],
                              uint32_t *pMacI[],
                              const uint32_t numBuffers)
{
        _zuc_eia3_n_buffer(pKey, pIv, pBufferIn, lengthInBits,
                           pMacI, numBuffers, 0);
}

void zuc_eia3_n_buffer_gfni_avx512(const void * const pKey[],
                                   const void * const pIv[],
                                   const void * const pBufferIn[],
                                   const uint32_t lengthInBits[],
                                   uint32_t *pMacI[],
                                   const uint32_t numBuffers)
{
        _zuc_eia3_n_buffer(pKey, pIv, pBufferIn, lengthInBits,
                           pMacI, numBuffers, 1);
}
