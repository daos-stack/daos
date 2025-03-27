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

#ifndef _WIRELESS_COMMON_H_
#define _WIRELESS_COMMON_H_

#include <string.h>
#ifdef LINUX
#include <x86intrin.h>
#else
#include <intrin.h>
#endif

#define NUM_PACKETS_1 1
#define NUM_PACKETS_2 2
#define NUM_PACKETS_3 3
#define NUM_PACKETS_4 4
#define NUM_PACKETS_8 8
#define NUM_PACKETS_16 16

#ifdef LINUX
#define BSWAP32 __builtin_bswap32
#define BSWAP64 __builtin_bswap64
#else
#define BSWAP32 _byteswap_ulong
#define BSWAP64 _byteswap_uint64
#endif

typedef union _m128_u {
        uint8_t byte[16];
        uint16_t word[8];
        uint32_t dword[4];
        uint64_t qword[2];
        __m128i m;
} m128_t;

typedef union _m64_u {
        uint8_t byte[8];
        uint16_t word[4];
        uint32_t dword[2];
        uint64_t m;
} m64_t;

static inline uint32_t bswap4(const uint32_t val)
{
        return BSWAP32(val);
}

/*************************************************************************
* @description - this function is used to copy the right number of bytes
*                from the source to destination buffer
*
* @param pSrc [IN] - pointer to an input Byte array (at least len bytes
*                    available)
* @param pDst [IN] - pointer to the output buffer (at least len bytes available)
* @param len  [IN] - length in bytes to copy (0 to 4)
*
*************************************************************************/
static inline void memcpy_keystream_32(uint8_t *pDst,
                                       const uint8_t *pSrc,
                                       const uint32_t len)
{
        switch (len) {
        case 4:
                *(uint32_t *)pDst = *(const uint32_t *)pSrc;
                break;
        case 3:
                pDst[2] = pSrc[2];
                /* fall-through */
        case 2:
                pDst[1] = pSrc[1];
                /* fall-through */
        case 1:
                pDst[0] = pSrc[0];
                /* fall-through */
        }
}

/*************************************************************************
* @description - this function is used to XOR the right number of bytes
*                from a keystrea and a source into a destination buffer
*
* @param pSrc [IN] - pointer to an input Byte array (at least 4 bytes available)
* @param pDst [IN] - pointer to the output buffer (at least 4 bytes available)
* @param KS  [IN]  - 4 bytes of keystream number, must be reversed
*                    into network byte order before XOR
*
*************************************************************************/
static inline void xor_keystream_reverse_32(uint8_t *pDst,
                                            const uint8_t *pSrc,
                                            const uint32_t KS)
{
        *(uint32_t *)pDst = (*(const uint32_t *)pSrc) ^ BSWAP32(KS);
}

/******************************************************************************
 * @description - this function is used to do a keystream operation
 * @param pSrc [IN] - pointer to an input Byte array (at least 8 bytes
 *                    available)
 * @param pDst [IN] - pointer to the output buffer (at least 8 bytes available)
 * @param keyStream [IN] -  the Keystream value (8 bytes)
 ******************************************************************************/
static inline const uint8_t *
xor_keystrm_rev(uint8_t *pDst, const uint8_t *pSrc, uint64_t keyStream)
{
        /* default: XOR ONLY, read the input buffer, update the output buffer */
        const uint64_t *pSrc64 = (const uint64_t *)pSrc;
        uint64_t *pDst64 = (uint64_t *)pDst;
        *pDst64 = *pSrc64 ^ BSWAP64(keyStream);
        return (const uint8_t *)(pSrc64 + 1);
}

/******************************************************************************
 * @description - this function is used to copy the right number of bytes
 *                from the source to destination buffer
 * @param pSrc [IN] - pointer to an input Byte array (at least len bytes
 *                    available)
 * @param pDst [IN] - pointer to the output buffer (at least len bytes
 *                    available)
 * @param len  [IN] - length in bytes to copy
 ******************************************************************************/
static inline void
memcpy_keystrm(uint8_t *pDst, const uint8_t *pSrc, const uint32_t len)
{
        switch (len) {
        case 8:
                *(uint64_t *)pDst = *(const uint64_t *)pSrc;
                break;
        case 7:
                pDst[6] = pSrc[6];
                /* fall-through */
        case 6:
                pDst[5] = pSrc[5];
                /* fall-through */
        case 5:
                pDst[4] = pSrc[4];
                /* fall-through */
        case 4:
                *(uint32_t *)pDst = *(const uint32_t *)pSrc;
                break;
        case 3:
                pDst[2] = pSrc[2];
                /* fall-through */
        case 2:
                pDst[1] = pSrc[1];
                /* fall-through */
        case 1:
                pDst[0] = pSrc[0];
                /* fall-through */
        }
}

/**
 ******************************************************************************
 *
 * @description
 *      Definition of the external SSE function that XOR's 16 bytes of input
 *      with 16 bytes of keystream, swapping keystream bytes every 4 bytes.
 *
 * @param[in]  pIn              Pointer to the input buffer
 * @param[out] pOut             Pointer to the output buffer
 * @param[in]  pKey             Pointer to the new 16 byte keystream
 *
 * @pre
 *      None
 *
 *****************************************************************************/
IMB_DLL_LOCAL void asm_XorKeyStream16B_sse(const void *pIn, void *pOut,
                                           const void *pKey);

/**
 ******************************************************************************
 *
 * @description
 *      Definition of the external AVX function that XOR's 16 bytes of input
 *      with 16 bytes of keystream, swapping keystream bytes every 4 bytes.
 *
 * @param[in]  pIn              Pointer to the input buffer
 * @param[out] pOut             Pointer to the output buffer
 * @param[in]  pKey             Pointer to the new 16 byte keystream
 *
 * @pre
 *      None
 *
 *****************************************************************************/
IMB_DLL_LOCAL void asm_XorKeyStream16B_avx(const void *pIn, void *pOut,
                                           const void *pKey);

/**
 ******************************************************************************
 *
 * @description
 *      Definition of the external AVX2 function that XOR's 32 bytes of input
 *      with 32 bytes of keystream, swapping keystream bytes every 4 bytes.
 *
 * @param[in]  pIn              Pointer to the input buffer
 * @param[out] pOut             Pointer to the output buffer
 * @param[in]  pKey             Pointer to the new 32 byte keystream
 *
 * @pre
 *      None
 *
 *****************************************************************************/
IMB_DLL_LOCAL void asm_XorKeyStream32B_avx2(const void *pIn, void *pOut,
                                            const void *pKey);

/**
 ******************************************************************************
 *
 * @description
 *      Definition of the external AVX512 function that XOR's 64 bytes of input
 *      with 64 bytes of keystream, swapping keystream bytes every 4 bytes.
 *
 * @param[in]  pIn              Pointer to the input buffer
 * @param[out] pOut             Pointer to the output buffer
 * @param[in]  pKey             Pointer to the new 64 byte keystream
 *
 * @pre
 *      None
 *
 *****************************************************************************/
IMB_DLL_LOCAL void asm_XorKeyStream64B_avx512(const void *pIn, void *pOut,
                                              const void *pKey);

#endif /* _WIRELESS_COMMON_H_ */
