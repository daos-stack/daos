/*****************************************************************************
 Copyright (c) 2017-2021, Intel Corporation

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

#ifndef GCM_CTR_VECTORS_TEST_H_
#define GCM_CTR_VECTORS_TEST_H_

#include <stdint.h>

enum arch_type {
        ARCH_SSE = 0,
        ARCH_AVX,
        ARCH_AVX2,
        ARCH_AVX512,
        ARCH_NO_AESNI,
        ARCH_NUMOF
};

#define KBITS(K)    (sizeof(K))

/* struct to hold pointers to the key, plaintext and ciphertext vectors */
struct gcm_ctr_vector {
	const uint8_t *K;       /* AES Key */
	IMB_KEY_SIZE_BYTES Klen;/* length of key in bits */
	const uint8_t *IV;      /* initial value used by GCM */
	uint64_t IVlen;         /* length of IV in bytes */
	const uint8_t *A;       /* additional authenticated data */
	uint64_t Alen;          /* length of AAD in bytes */
	const uint8_t *P;       /* Plain text */
	uint64_t Plen;          /* length of our plaintext */
	/* outputs of encryption */
	const uint8_t *C;       /* same length as PT */
	const uint8_t *T;       /* Authentication tag */
	uint8_t Tlen;           /* AT length can be 0 to 128bits */
};

#define vector(N)                                                       \
        {K##N, (KBITS(K##N)), IV##N, sizeof(IV##N), A##N, A##N##_len,   \
                        P##N, sizeof(P##N), C##N, T##N, sizeof(T##N)}

#define extra_vector(N)                                                 \
        {K##N, (KBITS(K##N)), IV##N, sizeof(IV##N), A##N, A##N##_len,   \
                        P##N, P##N##_len, C##N, T##N, sizeof(T##N)}
#define ghash_vector(N)                                                 \
        {K##N, (KBITS(K##N)), NULL, 0, NULL, 0, P##N, sizeof(P##N),     \
                        NULL, T##N, sizeof(T##N)}
struct MB_MGR;

extern int gcm_test(IMB_MGR *p_mgr);
int ctr_test(struct IMB_MGR *);

#endif /* GCM_CTR_VECTORS_TEST_H_ */
