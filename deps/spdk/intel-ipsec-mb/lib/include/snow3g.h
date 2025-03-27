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

#ifndef _SNOW3G_H_
#define _SNOW3G_H_

/*******************************************************************************
 * SSE
 ******************************************************************************/
void
snow3g_f8_1_buffer_bit_sse(const snow3g_key_schedule_t *pCtx,
                           const void *pIV,
                           const void *pBufferIn,
                           void *pBufferOut,
                           const uint32_t cipherLengthInBits,
                           const uint32_t offsetInBits);

void
snow3g_f8_1_buffer_sse(const snow3g_key_schedule_t *pCtx,
                       const void *pIV,
                       const void *pBufferIn,
                       void *pBufferOut,
                       const uint32_t lengthInBytes);

void
snow3g_f8_2_buffer_sse(const snow3g_key_schedule_t *pCtx,
                       const void *pIV1,
                       const void *pIV2,
                       const void *pBufferIn1,
                       void *pBufferOut1,
                       const uint32_t lengthInBytes1,
                       const void *pBufferIn2,
                       void *pBufferOut2,
                       const uint32_t lengthInBytes2);

void
snow3g_f8_4_buffer_sse(const snow3g_key_schedule_t *pCtx,
                       const void *pIV1,
                       const void *pIV2,
                       const void *pIV3,
                       const void *pIV4,
                       const void *pBufferIn1,
                       void *pBufferOut1,
                       const uint32_t lengthInBytes1,
                       const void *pBufferIn2,
                       void *pBufferOut2,
                       const uint32_t lengthInBytes2,
                       const void *pBufferIn3,
                       void *pBufferOut3,
                       const uint32_t lengthInBytes3,
                       const void *pBufferIn4,
                       void *pBufferOut4,
                       const uint32_t lengthInBytes4);

void
snow3g_f8_8_buffer_sse(const snow3g_key_schedule_t *pCtx,
                       const void *pIV1,
                       const void *pIV2,
                       const void *pIV3,
                       const void *pIV4,
                       const void *pIV5,
                       const void *pIV6,
                       const void *pIV7,
                       const void *pIV8,
                       const void *pBufferIn1,
                       void *pBufferOut1,
                       const uint32_t lengthInBytes1,
                       const void *pBufferIn2,
                       void *pBufferOut2,
                       const uint32_t lengthInBytes2,
                       const void *pBufferIn3,
                       void *pBufferOut3,
                       const uint32_t lengthInBytes3,
                       const void *pBufferIn4,
                       void *pBufferOut4,
                       const uint32_t lengthInBytes4,
                       const void *pBufferIn5,
                       void *pBufferOut5,
                       const uint32_t lengthInBytes5,
                       const void *pBufferIn6,
                       void *pBufferOut6,
                       const uint32_t lengthInBytes6,
                       const void *pBufferIn7,
                       void *pBufferOut7,
                       const uint32_t lengthInBytes7,
                       const void *pBufferIn8,
                       void *pBufferOut8,
                       const uint32_t lengthInBytes8);

void
snow3g_f8_8_buffer_multikey_sse(const snow3g_key_schedule_t * const pCtx[],
                                const void * const pIV[],
                                const void * const pBufferIn[],
                                void *pBufferOut[],
                                const uint32_t lengthInBytes[]);

void
snow3g_f8_n_buffer_sse(const snow3g_key_schedule_t *pCtx,
                       const void * const IV[],
                       const void * const pBufferIn[],
                       void *pBufferOut[],
                       const uint32_t bufferLenInBytes[],
                       const uint32_t bufferCount);

void
snow3g_f8_n_buffer_multikey_sse(const snow3g_key_schedule_t * const pCtx[],
                                const void * const IV[],
                                const void * const pBufferIn[],
                                void *pBufferOut[],
                                const uint32_t bufferLenInBytes[],
                                const uint32_t bufferCount);

void
snow3g_f9_1_buffer_sse(const snow3g_key_schedule_t *pCtx,
                       const void *pIV,
                       const void *pBufferIn,
                       const uint64_t lengthInBits,
                       void *pDigest);

size_t
snow3g_key_sched_size_sse(void);

int
snow3g_init_key_sched_sse(const void *pKey, snow3g_key_schedule_t *pCtx);

uint32_t
snow3g_f9_1_buffer_internal_sse(const uint64_t *pBufferIn,
                                const uint32_t KS[5],
                                const uint64_t lengthInBits);

/*******************************************************************************
 * SSE NO-AESNI
 ******************************************************************************/
void
snow3g_f8_1_buffer_bit_sse_no_aesni(const snow3g_key_schedule_t *pCtx,
                                    const void *pIV,
                                    const void *pBufferIn,
                                    void *pBufferOut,
                                    const uint32_t cipherLengthInBits,
                                    const uint32_t offsetInBits);

void
snow3g_f8_1_buffer_sse_no_aesni(const snow3g_key_schedule_t *pCtx,
                                const void *pIV,
                                const void *pBufferIn,
                                void *pBufferOut,
                                const uint32_t lengthInBytes);

void
snow3g_f8_2_buffer_sse_no_aesni(const snow3g_key_schedule_t *pCtx,
                                const void *pIV1,
                                const void *pIV2,
                                const void *pBufferIn1,
                                void *pBufferOut1,
                                const uint32_t lengthInBytes1,
                                const void *pBufferIn2,
                                void *pBufferOut2,
                                const uint32_t lengthInBytes2);

void
snow3g_f8_4_buffer_sse_no_aesni(const snow3g_key_schedule_t *pCtx,
                                const void *pIV1,
                                const void *pIV2,
                                const void *pIV3,
                                const void *pIV4,
                                const void *pBufferIn1,
                                void *pBufferOut1,
                                const uint32_t lengthInBytes1,
                                const void *pBufferIn2,
                                void *pBufferOut2,
                                const uint32_t lengthInBytes2,
                                const void *pBufferIn3,
                                void *pBufferOut3,
                                const uint32_t lengthInBytes3,
                                const void *pBufferIn4,
                                void *pBufferOut4,
                                const uint32_t lengthInBytes4);

void
snow3g_f8_8_buffer_sse_no_aesni(const snow3g_key_schedule_t *pCtx,
                                const void *pIV1,
                                const void *pIV2,
                                const void *pIV3,
                                const void *pIV4,
                                const void *pIV5,
                                const void *pIV6,
                                const void *pIV7,
                                const void *pIV8,
                                const void *pBufferIn1,
                                void *pBufferOut1,
                                const uint32_t lengthInBytes1,
                                const void *pBufferIn2,
                                void *pBufferOut2,
                                const uint32_t lengthInBytes2,
                                const void *pBufferIn3,
                                void *pBufferOut3,
                                const uint32_t lengthInBytes3,
                                const void *pBufferIn4,
                                void *pBufferOut4,
                                const uint32_t lengthInBytes4,
                                const void *pBufferIn5,
                                void *pBufferOut5,
                                const uint32_t lengthInBytes5,
                                const void *pBufferIn6,
                                void *pBufferOut6,
                                const uint32_t lengthInBytes6,
                                const void *pBufferIn7,
                                void *pBufferOut7,
                                const uint32_t lengthInBytes7,
                                const void *pBufferIn8,
                                void *pBufferOut8,
                                const uint32_t lengthInBytes8);

void
snow3g_f8_8_buffer_multikey_sse_no_aesni(const snow3g_key_schedule_t * const
                                         pCtx[],
                                         const void * const pIV[],
                                         const void * const pBufferIn[],
                                         void *pBufferOut[],
                                         const uint32_t lengthInBytes[]);

void
snow3g_f8_n_buffer_sse_no_aesni(const snow3g_key_schedule_t *pCtx,
                                const void * const IV[],
                                const void * const pBufferIn[],
                                void *pBufferOut[],
                                const uint32_t bufferLenInBytes[],
                                const uint32_t bufferCount);

void
snow3g_f8_n_buffer_multikey_sse_no_aesni(const snow3g_key_schedule_t * const
                                         pCtx[],
                                         const void * const IV[],
                                         const void * const pBufferIn[],
                                         void *pBufferOut[],
                                         const uint32_t bufferLenInBytes[],
                                         const uint32_t bufferCount);

void
snow3g_f9_1_buffer_sse_no_aesni(const snow3g_key_schedule_t *pCtx,
                                const void *pIV,
                                const void *pBufferIn,
                                const uint64_t lengthInBits,
                                void *pDigest);

size_t
snow3g_key_sched_size_sse_no_aesni(void);

int
snow3g_init_key_sched_sse_no_aesni(const void *pKey,
                                   snow3g_key_schedule_t *pCtx);

uint32_t
snow3g_f9_1_buffer_internal_sse_no_aesni(const uint64_t *pBufferIn,
                                         const uint32_t KS[5],
                                         const uint64_t lengthInBits);

/*******************************************************************************
 * AVX
 ******************************************************************************/
void
snow3g_f8_1_buffer_bit_avx(const snow3g_key_schedule_t *pCtx,
                           const void *pIV,
                           const void *pBufferIn,
                           void *pBufferOut,
                           const uint32_t cipherLengthInBits,
                           const uint32_t offsetInBits);

void
snow3g_f8_1_buffer_avx(const snow3g_key_schedule_t *pCtx,
                       const void *pIV,
                       const void *pBufferIn,
                       void *pBufferOut,
                       const uint32_t lengthInBytes);

void
snow3g_f8_2_buffer_avx(const snow3g_key_schedule_t *pCtx,
                       const void *pIV1,
                       const void *pIV2,
                       const void *pBufferIn1,
                       void *pBufferOut1,
                       const uint32_t lengthInBytes1,
                       const void *pBufferIn2,
                       void *pBufferOut2,
                       const uint32_t lengthInBytes2);

void
snow3g_f8_4_buffer_avx(const snow3g_key_schedule_t *pCtx,
                       const void *pIV1,
                       const void *pIV2,
                       const void *pIV3,
                       const void *pIV4,
                       const void *pBufferIn1,
                       void *pBufferOut1,
                       const uint32_t lengthInBytes1,
                       const void *pBufferIn2,
                       void *pBufferOut2,
                       const uint32_t lengthInBytes2,
                       const void *pBufferIn3,
                       void *pBufferOut3,
                       const uint32_t lengthInBytes3,
                       const void *pBufferIn4,
                       void *pBufferOut4,
                       const uint32_t lengthInBytes4);

void
snow3g_f8_8_buffer_avx(const snow3g_key_schedule_t *pCtx,
                       const void *pIV1,
                       const void *pIV2,
                       const void *pIV3,
                       const void *pIV4,
                       const void *pIV5,
                       const void *pIV6,
                       const void *pIV7,
                       const void *pIV8,
                       const void *pBufferIn1,
                       void *pBufferOut1,
                       const uint32_t lengthInBytes1,
                       const void *pBufferIn2,
                       void *pBufferOut2,
                       const uint32_t lengthInBytes2,
                       const void *pBufferIn3,
                       void *pBufferOut3,
                       const uint32_t lengthInBytes3,
                       const void *pBufferIn4,
                       void *pBufferOut4,
                       const uint32_t lengthInBytes4,
                       const void *pBufferIn5,
                       void *pBufferOut5,
                       const uint32_t lengthInBytes5,
                       const void *pBufferIn6,
                       void *pBufferOut6,
                       const uint32_t lengthInBytes6,
                       const void *pBufferIn7,
                       void *pBufferOut7,
                       const uint32_t lengthInBytes7,
                       const void *pBufferIn8,
                       void *pBufferOut8,
                       const uint32_t lengthInBytes8);

void
snow3g_f8_8_buffer_multikey_avx(const snow3g_key_schedule_t * const pCtx[],
                                const void * const pIV[],
                                const void * const pBufferIn[],
                                void *pBufferOut[],
                                const uint32_t lengthInBytes[]);

void
snow3g_f8_n_buffer_avx(const snow3g_key_schedule_t *pCtx,
                       const void * const IV[],
                       const void * const pBufferIn[],
                       void *pBufferOut[],
                       const uint32_t bufferLenInBytes[],
                       const uint32_t bufferCount);

void
snow3g_f8_n_buffer_multikey_avx(const snow3g_key_schedule_t * const pCtx[],
                                const void * const IV[],
                                const void * const pBufferIn[],
                                void *pBufferOut[],
                                const uint32_t bufferLenInBytes[],
                                const uint32_t bufferCount);

void
snow3g_f9_1_buffer_avx(const snow3g_key_schedule_t *pCtx,
                       const void *pIV,
                       const void *pBufferIn,
                       const uint64_t lengthInBits,
                       void *pDigest);

size_t
snow3g_key_sched_size_avx(void);

int
snow3g_init_key_sched_avx(const void *pKey, snow3g_key_schedule_t *pCtx);

uint32_t
snow3g_f9_1_buffer_internal_avx(const uint64_t *pBufferIn,
                                const uint32_t KS[5],
                                const uint64_t lengthInBits);

/*******************************************************************************
 * AVX2
 ******************************************************************************/

void
snow3g_f8_1_buffer_bit_avx2(const snow3g_key_schedule_t *pCtx,
                            const void *pIV,
                            const void *pBufferIn,
                            void *pBufferOut,
                            const uint32_t cipherLengthInBits,
                            const uint32_t offsetInBits);

void
snow3g_f8_1_buffer_avx2(const snow3g_key_schedule_t *pCtx,
                        const void *pIV,
                        const void *pBufferIn,
                        void *pBufferOut,
                        const uint32_t lengthInBytes);

void
snow3g_f8_2_buffer_avx2(const snow3g_key_schedule_t *pCtx,
                        const void *pIV1,
                        const void *pIV2,
                        const void *pBufferIn1,
                        void *pBufferOut1,
                        const uint32_t lengthInBytes1,
                        const void *pBufferIn2,
                        void *pBufferOut2,
                        const uint32_t lengthInBytes2);

void
snow3g_f8_4_buffer_avx2(const snow3g_key_schedule_t *pCtx,
                        const void *pIV1,
                        const void *pIV2,
                        const void *pIV3,
                        const void *pIV4,
                        const void *pBufferIn1,
                        void *pBufferOut1,
                        const uint32_t lengthInBytes1,
                        const void *pBufferIn2,
                        void *pBufferOut2,
                        const uint32_t lengthInBytes2,
                        const void *pBufferIn3,
                        void *pBufferOut3,
                        const uint32_t lengthInBytes3,
                        const void *pBufferIn4,
                        void *pBufferOut4,
                        const uint32_t lengthInBytes4);

void
snow3g_f8_8_buffer_avx2(const snow3g_key_schedule_t *pCtx,
                        const void *pIV1,
                        const void *pIV2,
                        const void *pIV3,
                        const void *pIV4,
                        const void *pIV5,
                        const void *pIV6,
                        const void *pIV7,
                        const void *pIV8,
                        const void *pBufferIn1,
                        void *pBufferOut1,
                        const uint32_t lengthInBytes1,
                        const void *pBufferIn2,
                        void *pBufferOut2,
                        const uint32_t lengthInBytes2,
                        const void *pBufferIn3,
                        void *pBufferOut3,
                        const uint32_t lengthInBytes3,
                        const void *pBufferIn4,
                        void *pBufferOut4,
                        const uint32_t lengthInBytes4,
                        const void *pBufferIn5,
                        void *pBufferOut5,
                        const uint32_t lengthInBytes5,
                        const void *pBufferIn6,
                        void *pBufferOut6,
                        const uint32_t lengthInBytes6,
                        const void *pBufferIn7,
                        void *pBufferOut7,
                        const uint32_t lengthInBytes7,
                        const void *pBufferIn8,
                        void *pBufferOut8,
                        const uint32_t lengthInBytes8);

void
snow3g_f8_8_buffer_multikey_avx2(const snow3g_key_schedule_t * const pCtx[],
                                 const void * const pIV[],
                                 const void * const pBufferIn[],
                                 void *pBufferOut[],
                                 const uint32_t lengthInBytes[]);

void
snow3g_f8_n_buffer_avx2(const snow3g_key_schedule_t *pCtx,
                        const void * const IV[],
                        const void * const pBufferIn[],
                        void *pBufferOut[],
                        const uint32_t bufferLenInBytes[],
                        const uint32_t bufferCount);

void
snow3g_f8_n_buffer_multikey_avx2(const snow3g_key_schedule_t * const pCtx[],
                                 const void * const IV[],
                                 const void * const pBufferIn[],
                                 void *pBufferOut[],
                                 const uint32_t bufferLenInBytes[],
                                 const uint32_t bufferCount);

void
snow3g_f9_1_buffer_avx2(const snow3g_key_schedule_t *pCtx,
                        const void *pIV,
                        const void *pBufferIn,
                        const uint64_t lengthInBits,
                        void *pDigest);

size_t
snow3g_key_sched_size_avx2(void);

int
snow3g_init_key_sched_avx2(const void *pKey, snow3g_key_schedule_t *pCtx);

/*******************************************************************************
 * AVX512
 ******************************************************************************/

void
snow3g_f8_1_buffer_bit_avx512(const snow3g_key_schedule_t *pCtx,
                              const void *pIV,
                              const void *pBufferIn,
                              void *pBufferOut,
                              const uint32_t cipherLengthInBits,
                              const uint32_t offsetInBits);

void
snow3g_f8_1_buffer_avx512(const snow3g_key_schedule_t *pCtx,
                          const void *pIV,
                          const void *pBufferIn,
                          void *pBufferOut,
                          const uint32_t lengthInBytes);

void
snow3g_f8_2_buffer_avx512(const snow3g_key_schedule_t *pCtx,
                          const void *pIV1,
                          const void *pIV2,
                          const void *pBufferIn1,
                          void *pBufferOut1,
                          const uint32_t lengthInBytes1,
                          const void *pBufferIn2,
                          void *pBufferOut2,
                          const uint32_t lengthInBytes2);

void
snow3g_f8_4_buffer_avx512(const snow3g_key_schedule_t *pCtx,
                          const void *pIV1,
                          const void *pIV2,
                          const void *pIV3,
                          const void *pIV4,
                          const void *pBufferIn1,
                          void *pBufferOut1,
                          const uint32_t lengthInBytes1,
                          const void *pBufferIn2,
                          void *pBufferOut2,
                          const uint32_t lengthInBytes2,
                          const void *pBufferIn3,
                          void *pBufferOut3,
                          const uint32_t lengthInBytes3,
                          const void *pBufferIn4,
                          void *pBufferOut4,
                          const uint32_t lengthInBytes4);

void
snow3g_f8_8_buffer_avx512(const snow3g_key_schedule_t *pCtx,
                          const void *pIV1,
                          const void *pIV2,
                          const void *pIV3,
                          const void *pIV4,
                          const void *pIV5,
                          const void *pIV6,
                          const void *pIV7,
                          const void *pIV8,
                          const void *pBufferIn1,
                          void *pBufferOut1,
                          const uint32_t lengthInBytes1,
                          const void *pBufferIn2,
                          void *pBufferOut2,
                          const uint32_t lengthInBytes2,
                          const void *pBufferIn3,
                          void *pBufferOut3,
                          const uint32_t lengthInBytes3,
                          const void *pBufferIn4,
                          void *pBufferOut4,
                          const uint32_t lengthInBytes4,
                          const void *pBufferIn5,
                          void *pBufferOut5,
                          const uint32_t lengthInBytes5,
                          const void *pBufferIn6,
                          void *pBufferOut6,
                          const uint32_t lengthInBytes6,
                          const void *pBufferIn7,
                          void *pBufferOut7,
                          const uint32_t lengthInBytes7,
                          const void *pBufferIn8,
                          void *pBufferOut8,
                          const uint32_t lengthInBytes8);

void
snow3g_f8_8_buffer_multikey_avx512(const snow3g_key_schedule_t * const pCtx[],
                                   const void * const pIV[],
                                   const void * const pBufferIn[],
                                   void *pBufferOut[],
                                   const uint32_t lengthInBytes[]);

void
snow3g_f8_n_buffer_avx512(const snow3g_key_schedule_t *pCtx,
                          const void * const IV[],
                          const void * const pBufferIn[],
                          void *pBufferOut[],
                          const uint32_t bufferLenInBytes[],
                          const uint32_t bufferCount);

void
snow3g_f8_n_buffer_multikey_avx512(const snow3g_key_schedule_t * const pCtx[],
                                   const void * const IV[],
                                   const void * const pBufferIn[],
                                   void *pBufferOut[],
                                   const uint32_t bufferLenInBytes[],
                                   const uint32_t bufferCount);

void
snow3g_f9_1_buffer_avx512(const snow3g_key_schedule_t *pCtx,
                          const void *pIV,
                          const void *pBufferIn,
                          const uint64_t lengthInBits,
                          void *pDigest);

size_t
snow3g_key_sched_size_avx512(void);

int
snow3g_init_key_sched_avx512(const void *pKey, snow3g_key_schedule_t *pCtx);

void
snow3g_f9_1_buffer_vaes_avx512(const snow3g_key_schedule_t *pHandle,
                               const void *pIV,
                               const void *pBufferIn,
                               const uint64_t lengthInBits,
                               void *pDigest);

uint32_t
snow3g_f9_1_buffer_internal_vaes_avx512(const uint64_t *pBufferIn,
                                        const uint32_t KS[5],
                                        const uint64_t lengthInBits);

#endif /* _SNOW3G_H_ */
