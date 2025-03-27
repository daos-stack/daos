/*******************************************************************************
  Copyright (c) 2009-2021, Intel Corporation

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

#include <limits.h>

#define CLEAR_SCRATCH_SIMD_REGS clear_scratch_xmms_sse

#include "include/kasumi_internal.h"
#include "include/save_xmms.h"
#include "include/clear_regs_mem.h"
#include "include/error.h"

#define SAVE_XMMS               save_xmms
#define RESTORE_XMMS            restore_xmms

void
kasumi_f8_1_buffer_sse(const kasumi_key_sched_t *pCtx, const uint64_t IV,
                       const void *pBufferIn, void *pBufferOut,
                       const uint32_t cipherLengthInBytes)
{
#ifndef LINUX
        DECLARE_ALIGNED(imb_uint128_t xmm_save[10], 16);

        SAVE_XMMS(xmm_save);
#endif
#ifdef SAFE_PARAM
        /* Check for NULL pointers */
        imb_set_errno(NULL, 0);
        if (pCtx ==  NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
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
        if (cipherLengthInBytes == 0 ||
            cipherLengthInBytes > (KASUMI_MAX_LEN / CHAR_BIT)) {
                imb_set_errno(NULL, IMB_ERR_CIPH_LEN);
                return;
        }
#endif
        kasumi_f8_1_buffer(pCtx, IV, pBufferIn, pBufferOut,
                           cipherLengthInBytes);
#ifdef SAFE_DATA
        /* Clear sensitive data in registers */
        CLEAR_SCRATCH_GPS();
        CLEAR_SCRATCH_SIMD_REGS();
#endif
#ifndef LINUX
        RESTORE_XMMS(xmm_save);
#endif
}

void
kasumi_f8_1_buffer_bit_sse(const kasumi_key_sched_t *pCtx,
                           const uint64_t IV,
                           const void *pBufferIn, void *pBufferOut,
                           const uint32_t cipherLengthInBits,
                           const uint32_t offsetInBits)
{
#ifndef LINUX
        DECLARE_ALIGNED(imb_uint128_t xmm_save[10], 16);

        SAVE_XMMS(xmm_save);
#endif
#ifdef SAFE_PARAM
        /* Check for NULL pointers */
        imb_set_errno(NULL, 0);
        if (pCtx ==  NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
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
        if (cipherLengthInBits == 0 ||
            cipherLengthInBits > KASUMI_MAX_LEN) {
                imb_set_errno(NULL, IMB_ERR_CIPH_LEN);
                return;
        }
#endif
        kasumi_f8_1_buffer_bit(pCtx, IV, pBufferIn, pBufferOut,
                               cipherLengthInBits, offsetInBits);
#ifdef SAFE_DATA
        /* Clear sensitive data in registers */
        CLEAR_SCRATCH_GPS();
        CLEAR_SCRATCH_SIMD_REGS();
#endif
#ifndef LINUX
        RESTORE_XMMS(xmm_save);
#endif
}

void
kasumi_f8_2_buffer_sse(const kasumi_key_sched_t *pCtx, const uint64_t IV1,
                       const uint64_t IV2, const void *pBufferIn1,
                       void *pBufferOut1, const uint32_t lengthInBytes1,
                       const void *pBufferIn2, void *pBufferOut2,
                       const uint32_t lengthInBytes2)
{
#ifndef LINUX
        DECLARE_ALIGNED(imb_uint128_t xmm_save[10], 16);

        SAVE_XMMS(xmm_save);
#endif
#ifdef SAFE_PARAM
        /* Check for NULL pointers */
        imb_set_errno(NULL, 0);
        if (pCtx == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
                return;
        }
        if (pBufferIn1 == NULL || pBufferIn2 == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_SRC);
                return;
        }
        if (pBufferOut1 == NULL || pBufferOut2 == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_DST);
                return;
        }
        /* Check input data is in range of supported length */
        if (lengthInBytes1 == 0 ||
            lengthInBytes1 > (KASUMI_MAX_LEN / CHAR_BIT)) {
                imb_set_errno(NULL, IMB_ERR_CIPH_LEN);
                return;
        }

        if (lengthInBytes2 == 0 ||
            lengthInBytes2 > (KASUMI_MAX_LEN / CHAR_BIT)) {
                imb_set_errno(NULL, IMB_ERR_CIPH_LEN);
                return;
        }
#endif
        kasumi_f8_2_buffer(pCtx, IV1, IV2,
                           pBufferIn1, pBufferOut1, lengthInBytes1,
                           pBufferIn2, pBufferOut2, lengthInBytes2);
#ifdef SAFE_DATA
        /* Clear sensitive data in registers */
        CLEAR_SCRATCH_GPS();
        CLEAR_SCRATCH_SIMD_REGS();
#endif
#ifndef LINUX
        RESTORE_XMMS(xmm_save);
#endif
}

void
kasumi_f8_3_buffer_sse(const kasumi_key_sched_t *pCtx, const uint64_t IV1,
                       const uint64_t IV2, const uint64_t IV3,
                       const void *pBufferIn1, void *pBufferOut1,
                       const void *pBufferIn2, void *pBufferOut2,
                       const void *pBufferIn3, void *pBufferOut3,
                       const uint32_t lengthInBytes)
{
#ifndef LINUX
        DECLARE_ALIGNED(imb_uint128_t xmm_save[10], 16);

        SAVE_XMMS(xmm_save);
#endif
#ifdef SAFE_PARAM
        /* Check for NULL pointers */
        imb_set_errno(NULL, 0);
        if (pCtx == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
                return;
        }
        if (pBufferIn1 == NULL || pBufferIn2 == NULL || pBufferIn3 == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_SRC);
                return;
        }
        if (pBufferOut1 == NULL || pBufferOut2 == NULL || pBufferOut3 == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_DST);
                return;
        }
        /* Check input data is in range of supported length */
        if (lengthInBytes == 0 || lengthInBytes > (KASUMI_MAX_LEN / CHAR_BIT)) {
                imb_set_errno(NULL, IMB_ERR_CIPH_LEN);
                return;
        }
#endif
        kasumi_f8_3_buffer(pCtx, IV1, IV2, IV3,
                           pBufferIn1, pBufferOut1,
                           pBufferIn2, pBufferOut2,
                           pBufferIn3, pBufferOut3, lengthInBytes);
#ifdef SAFE_DATA
        /* Clear sensitive data in registers */
        CLEAR_SCRATCH_GPS();
        CLEAR_SCRATCH_SIMD_REGS();
#endif
#ifndef LINUX
        RESTORE_XMMS(xmm_save);
#endif
}

void
kasumi_f8_4_buffer_sse(const kasumi_key_sched_t *pCtx,
                       const uint64_t IV1, const uint64_t IV2,
                       const uint64_t IV3, const uint64_t IV4,
                       const void *pBufferIn1, void *pBufferOut1,
                       const void *pBufferIn2, void *pBufferOut2,
                       const void *pBufferIn3, void *pBufferOut3,
                       const void *pBufferIn4, void *pBufferOut4,
                       const uint32_t lengthInBytes)
{
#ifndef LINUX
        DECLARE_ALIGNED(imb_uint128_t xmm_save[10], 16);

        SAVE_XMMS(xmm_save);
#endif
#ifdef SAFE_PARAM
        /* Check for NULL pointers */
        imb_set_errno(NULL, 0);
        if (pCtx == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
                return;
        }
        if (pBufferIn1 == NULL || pBufferIn2 == NULL || pBufferIn3 == NULL ||
            pBufferIn4 == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_SRC);
                return;
        }
        if (pBufferOut1 == NULL || pBufferOut2 == NULL || pBufferOut3 == NULL ||
            pBufferOut4 == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_DST);
                return;
        }
        /* Check input data is in range of supported length */
        if (lengthInBytes == 0 || lengthInBytes > (KASUMI_MAX_LEN / CHAR_BIT)) {
                imb_set_errno(NULL, IMB_ERR_CIPH_LEN);
                return;
        }
#endif
        kasumi_f8_4_buffer(pCtx, IV1, IV2, IV3, IV4,
                           pBufferIn1, pBufferOut1,
                           pBufferIn2, pBufferOut2,
                           pBufferIn3, pBufferOut3,
                           pBufferIn4, pBufferOut4,
                           lengthInBytes);
#ifdef SAFE_DATA
        /* Clear sensitive data in registers */
        CLEAR_SCRATCH_GPS();
        CLEAR_SCRATCH_SIMD_REGS();
#endif
#ifndef LINUX
        RESTORE_XMMS(xmm_save);
#endif
}

void
kasumi_f8_n_buffer_sse(const kasumi_key_sched_t *pKeySchedule,
                       const uint64_t IV[],
                       const void * const pDataIn[], void *pDataOut[],
                       const uint32_t dataLen[], const uint32_t dataCount)
{
#ifndef LINUX
        DECLARE_ALIGNED(imb_uint128_t xmm_save[10], 16);

        SAVE_XMMS(xmm_save);
#endif
        uint32_t numLeft = dataCount;
        const uint64_t *IVPtr;
        const void * const *pDataInPtr;
        void **pDataOutPtr;
        const uint32_t *dataLenPtr;
        uint32_t i = 0;
        uint32_t numBuffs;

#ifdef SAFE_PARAM
        /* Check for NULL pointers */
        imb_set_errno(NULL, 0);

        if (pKeySchedule == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
                return;
        }
        if (pDataIn == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_SRC);
                return;
        }
        if (pDataOut == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_DST);
                return;
        }
        if (dataLen == NULL) {
                imb_set_errno(NULL, IMB_ERR_CIPH_LEN);
                return;
        }
        if (IV == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_IV);
                return;
        }

        for (i = 0; i < dataCount; i++) {
                /* Check for NULL pointers */
                if (pDataIn[i] == NULL) {
                        imb_set_errno(NULL, IMB_ERR_NULL_SRC);
                        return;
                }
                if (pDataOut[i] == NULL) {
                        imb_set_errno(NULL, IMB_ERR_NULL_DST);
                        return;
                }
                /* Check input data is in range of supported length */
                if (dataLen[i] == 0 ||
                    dataLen[i] > (KASUMI_MAX_LEN / CHAR_BIT)) {
                        imb_set_errno(NULL, IMB_ERR_CIPH_LEN);
                        return;
                }
        }
#endif

        i = 0;

        /* KASUMI F8 n buffer function can handle up to 16 buffers */
        while (numLeft > 0) {
                IVPtr = &IV[i];
                pDataInPtr = &pDataIn[i];
                pDataOutPtr = &pDataOut[i];
                dataLenPtr = &dataLen[i];
                numBuffs = (numLeft > 16) ? 16 : numLeft;

                kasumi_f8_n_buffer(pKeySchedule, IVPtr, pDataInPtr, pDataOutPtr,
                                   dataLenPtr, numBuffs);
                i += numBuffs;
                numLeft -= numBuffs;
        }
#ifdef SAFE_DATA
        /* Clear sensitive data in registers */
        CLEAR_SCRATCH_GPS();
        CLEAR_SCRATCH_SIMD_REGS();
#endif
#ifndef LINUX
        RESTORE_XMMS(xmm_save);
#endif
}


void
kasumi_f9_1_buffer_sse(const kasumi_key_sched_t *pCtx, const void *pBufferIn,
                       const uint32_t lengthInBytes, void *pDigest)
{
#ifndef LINUX
        DECLARE_ALIGNED(imb_uint128_t xmm_save[10], 16);

        SAVE_XMMS(xmm_save);
#endif
#ifdef SAFE_PARAM
        /* Reset error */
        imb_set_errno(NULL, 0);

        /* Check for NULL pointers */
        if (pCtx == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
                return;
        }
        if (pBufferIn == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_SRC);
                return;
        }
        if (pDigest == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_AUTH);
                return;
        }
        /* Check input data is in range of supported length */
        if (lengthInBytes == 0 || lengthInBytes > (KASUMI_MAX_LEN / CHAR_BIT)) {
                imb_set_errno(NULL, IMB_ERR_AUTH_LEN);
                return;
        }
#endif
        kasumi_f9_1_buffer(pCtx, pBufferIn, lengthInBytes, pDigest);
#ifdef SAFE_DATA
        /* Clear sensitive data in registers */
        CLEAR_SCRATCH_GPS();
        CLEAR_SCRATCH_SIMD_REGS();
#endif
#ifndef LINUX
        RESTORE_XMMS(xmm_save);
#endif
}

void
kasumi_f9_1_buffer_user_sse(const kasumi_key_sched_t *pCtx, const uint64_t IV,
                            const void *pBufferIn, const uint32_t lengthInBits,
                            void *pDigest, const uint32_t direction)
{
#ifndef LINUX
        DECLARE_ALIGNED(imb_uint128_t xmm_save[10], 16);

        SAVE_XMMS(xmm_save);
#endif
#ifdef SAFE_PARAM
        /* Reset error */
        imb_set_errno(NULL, 0);

        /* Check for NULL pointers */
        if (pCtx == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
                return;
        }
        if (pBufferIn == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_SRC);
                return;
        }
        if (pDigest == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_AUTH);
                return;
        }
        /* Check input data is in range of supported length */
        if (lengthInBits == 0 || lengthInBits > KASUMI_MAX_LEN) {
                imb_set_errno(NULL, IMB_ERR_AUTH_LEN);
                return;
        }
#endif
        kasumi_f9_1_buffer_user(pCtx, IV, pBufferIn, lengthInBits,
                                pDigest, direction);
#ifdef SAFE_DATA
        /* Clear sensitive data in registers */
        CLEAR_SCRATCH_GPS();
        CLEAR_SCRATCH_SIMD_REGS();
#endif
#ifndef LINUX
        RESTORE_XMMS(xmm_save);
#endif
}

int
kasumi_init_f8_key_sched_sse(const void *const pKey,
                             kasumi_key_sched_t *pCtx)
{
        return kasumi_init_f8_key_sched(pKey, pCtx);
}

int
kasumi_init_f9_key_sched_sse(const void *const pKey,
                             kasumi_key_sched_t *pCtx)
{
        return kasumi_init_f9_key_sched(pKey, pCtx);
}

size_t
kasumi_key_sched_size_sse(void)
{
        return kasumi_key_sched_size();
}
