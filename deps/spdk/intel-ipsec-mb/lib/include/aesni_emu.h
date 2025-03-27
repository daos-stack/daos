/*******************************************************************************
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
*******************************************************************************/

#ifndef _AESNI_EMU_H_
#define _AESNI_EMU_H_
#include <stdint.h>

/* Interface to AESNI emulation routines */

/* XMM type definitions and constants */

#define MAX_BYTES_PER_XMM   16
#define MAX_WORDS_PER_XMM   8
#define MAX_DWORDS_PER_XMM  4
#define MAX_QWORDS_PER_XMM  2

union xmm_reg {
        uint8_t  byte[MAX_BYTES_PER_XMM];
        uint16_t word[MAX_WORDS_PER_XMM];
        uint32_t dword[MAX_DWORDS_PER_XMM];
        uint64_t qword[MAX_QWORDS_PER_XMM];
};

/* AESNI emulation API */

/**
 * @brief AESKEYGENASIST instruction emulation function
 *
 * Assist in AES round key generation using an 8 bits Round Constant
 * (RCON) specified in \a imm8, operating on 128 bits of data
 *
 * @param dst pointer to 128 bit buffer to store generated key
 * @param src pointer to 128 bit src key
 * @param imm8 round constant used to generate key
 */
IMB_DLL_LOCAL void emulate_AESKEYGENASSIST(union xmm_reg *dst,
                                           const union xmm_reg *src,
                                           const uint32_t imm8);

/**
 * @brief AESENC instruction emulation function
 *
 * Perform one round of an AES encryption flow
 *
 * @param dst pointer to 128 bit data (state) to operate on
 * @param src pointer to 128 bit round key
 */
IMB_DLL_LOCAL void emulate_AESENC(union xmm_reg *dst,
                                  const union xmm_reg *src);

/**
 * @brief AESENCLAST instruction emulation function
 *
 * Perform last round of an AES encryption flow
 *
 * @param dst pointer to 128 bit data (state) to operate on
 * @param src pointer to 128 bit round key
 */
IMB_DLL_LOCAL void emulate_AESENCLAST(union xmm_reg *dst,
                                      const union xmm_reg *src);

/**
 * @brief AESDEC instruction emulation function
 *
 * Perform one round of an AES decryption flow
 *
 * @param dst pointer to 128 bit data (state) to operate on
 * @param src pointer to 128 bit round key
 */
IMB_DLL_LOCAL void emulate_AESDEC(union xmm_reg *dst,
                                  const union xmm_reg *src);

/**
 * @brief AESDECLAST instruction emulation function
 *
 * Perform last round of an AES decryption flow
 *
 * @param dst pointer to 128 bit data (state) to operate on
 * @param src pointer to 128 bit round key
 */
IMB_DLL_LOCAL void emulate_AESDECLAST(union xmm_reg *dst,
                                      const union xmm_reg *src);

/**
 * @brief AESIMC instruction emulation function
 *
 * Perform the InvMixColumn transformation on
 * a 128 bit round key
 *
 * @param dst pointer to 128 bit buffer to store result
 * @param src pointer to 128 bit round key
 */
IMB_DLL_LOCAL void emulate_AESIMC(union xmm_reg *dst,
                                  const union xmm_reg *src);

/**
 * @brief PCLMULQDQ instruction emulation function
 *
 * Performs carry-less multiplication of two 64-bit numbers and
 * returns 128-bit product.
 *
 * @param src1_dst pointer to 128 bit input/output buffer
 * @param src2     pointer to 128 bit input number
 * @param imm8     constant for selecting quadword
 */
IMB_DLL_LOCAL void emulate_PCLMULQDQ(union xmm_reg *src1_dst,
                                     const union xmm_reg *src2,
                                     const uint32_t imm8);

#endif /* _AESNI_EMU_H_ */
