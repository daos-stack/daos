;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;  Copyright(c) 2021, Intel Corporation All rights reserved.
;
;  Redistribution and use in source and binary forms, with or without
;  modification, are permitted provided that the following conditions
;  are met:
;    * Redistributions of source code must retain the above copyright
;      notice, this list of conditions and the following disclaimer.
;    * Redistributions in binary form must reproduce the above copyright
;      notice, this list of conditions and the following disclaimer in
;      the documentation and/or other materials provided with the
;      distribution.
;    * Neither the name of Intel Corporation nor the names of its
;      contributors may be used to endorse or promote products derived
;      from this software without specific prior written permission.
;
;  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
;  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
;  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
;  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
;  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
;  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
;  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
;  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
;  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
;  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
;  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

%include "avx512/gcm_vaes_avx512.inc"
%include "include/error.inc"

%ifndef GCM_GMAC_API_VAES_AVX512_INC
%define GCM_GMAC_API_VAES_AVX512_INC

mksection .text
default rel

%ifdef GCM128_MODE
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;void   ghash_pre_avx512
;       (const void *key, struct gcm_key_data *key_data)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
MKGLOBAL(ghash_pre_vaes_avx512,function,)
ghash_pre_vaes_avx512:
        endbranch64
;; Parameter is passed through register
%ifdef SAFE_PARAM
        ;; Reset imb_errno
        IMB_ERR_CHECK_RESET

        ;; Check key != NULL
        cmp     arg1, 0
        jz      error_ghash_pre

        ;; Check key_data != NULL
        cmp     arg2, 0
        jz      error_ghash_pre
%endif

        FUNC_SAVE

        vmovdqu xmm6, [arg1]
        vpshufb  xmm6, [rel SHUF_MASK]
        ;;;;;;;;;;;;;;;  PRECOMPUTATION of HashKey<<1 mod poly from the HashKey;;;;;;;;;;;;;;;
        vmovdqa  xmm2, xmm6
        vpsllq   xmm6, xmm6, 1
        vpsrlq   xmm2, xmm2, 63
        vmovdqa  xmm1, xmm2
        vpslldq  xmm2, xmm2, 8
        vpsrldq  xmm1, xmm1, 8
        vpor     xmm6, xmm6, xmm2
        ;reduction
        vpshufd  xmm2, xmm1, 00100100b
        vpcmpeqd xmm2, [rel TWOONE]
        vpand    xmm2, xmm2, [rel POLY]
        vpxor    xmm6, xmm6, xmm2                       ; xmm6 holds the HashKey<<1 mod poly
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        vmovdqu  [arg2 + HashKey], xmm6                 ; store HashKey<<1 mod poly

        PRECOMPUTE arg2, xmm6, xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm7, xmm8

        FUNC_RESTORE
exit_ghash_pre:

        ret

%ifdef SAFE_PARAM
error_ghash_pre:
        ;; Clear reg and imb_errno
        IMB_ERR_CHECK_START rax

        ;; Check key != NULL
        IMB_ERR_CHECK_NULL arg1, rax, IMB_ERR_NULL_KEY

        ;; Check key_data != NULL
        IMB_ERR_CHECK_NULL arg2, rax, IMB_ERR_NULL_EXP_KEY

        ;; Set imb_errno
        IMB_ERR_CHECK_END rax
        jmp     exit_ghash_pre
%endif

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;void   ghash_vaes_avx512
;        const struct gcm_key_data *key_data,
;        const void   *in,
;        const u64    in_len,
;        void         *io_tag,
;        const u64    tag_len);
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
MKGLOBAL(ghash_vaes_avx512,function,)
ghash_vaes_avx512:
        endbranch64
        FUNC_SAVE

%ifdef SAFE_PARAM
        ;; Reset imb_errno
        IMB_ERR_CHECK_RESET

        ;; Check key_data != NULL
        cmp     arg1, 0
        jz      error_ghash

        ;; Check in != NULL
        cmp     arg2, 0
        jz      error_ghash

        ;; Check in_len != 0
        cmp     arg3, 0
        jz      error_ghash

        ;; Check tag != NULL
        cmp     arg4, 0
        jz      error_ghash

        ;; Check tag_len != 0
        cmp     arg5, 0
        jz      error_ghash
%endif

        ;; copy tag to xmm0
        vmovdqu	xmm0, [arg4]
        vpshufb xmm0, [rel SHUF_MASK] ; perform a 16Byte swap

        CALC_AAD_HASH arg2, arg3, xmm0, arg1, zmm1, zmm2, zmm3, zmm4, zmm5, \
                      zmm6, zmm7, zmm8, zmm9, zmm10, zmm11, zmm12, zmm13, \
                      zmm15, zmm16, zmm17, zmm18, zmm19, r10, r11, r12, k1

        vpshufb xmm0, [rel SHUF_MASK] ; perform a 16Byte swap

        simd_store_avx arg4, xmm0, arg5, r12, rax

exit_ghash:
        FUNC_RESTORE
        ret

%ifdef SAFE_PARAM
error_ghash:
        ;; Clear reg and imb_errno
        IMB_ERR_CHECK_START rax

        ;; Check key_data != NULL
        IMB_ERR_CHECK_NULL arg1, rax, IMB_ERR_NULL_EXP_KEY

        ;; Check in != NULL
        IMB_ERR_CHECK_NULL arg2, rax, IMB_ERR_NULL_SRC

        ;; Check in_len != 0
        IMB_ERR_CHECK_ZERO arg3, rax, IMB_ERR_AUTH_LEN

        ;; Check tag != NULL
        IMB_ERR_CHECK_NULL arg4, rax, IMB_ERR_NULL_AUTH

        ;; Check tag_len != 0
        IMB_ERR_CHECK_ZERO arg5, rax, IMB_ERR_AUTH_TAG_LEN

        ;; Set imb_errno
        IMB_ERR_CHECK_END rax

        jmp     exit_ghash
%endif

%endif ;; GCM128_MODE

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;void   imb_aes_gmac_update_128_vaes_avx512 /
;       imb_aes_gmac_update_192_vaes_avx512 /
;       imb_aes_gmac_update_256_vaes_avx512
;        const struct gcm_key_data *key_data,
;        struct gcm_context_data *context_data,
;        const   u8 *in,
;        const   u64 msg_len);
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
MKGLOBAL(GMAC_FN_NAME(update),function,)
GMAC_FN_NAME(update):
        endbranch64
	FUNC_SAVE

%ifdef SAFE_PARAM
        ;; Reset imb_errno
        IMB_ERR_CHECK_RESET
%endif
        ;; Check if msg_len == 0
	cmp	arg4, 0
	je	exit_gmac_update

%ifdef SAFE_PARAM
        ;; Check key_data != NULL
        cmp     arg1, 0
        jz      error_gmac_update

        ;; Check context_data != NULL
        cmp     arg2, 0
        jz      error_gmac_update

        ;; Check in != NULL (msg_len != 0)
        cmp     arg3, 0
        jz      error_gmac_update
%endif

        ; Increment size of "AAD length" for GMAC
        add     [arg2 + AadLen], arg4

        ;; Deal with previous partial block
	xor	r11, r11
	vmovdqu64	xmm8, [arg2 + AadHash]

	PARTIAL_BLOCK_GMAC arg1, arg2, arg3, arg4, r11, xmm8, r10, r12, rax, \
                           zmm0, zmm1, zmm2, zmm3, zmm4, zmm5, zmm6, zmm7, \
                           zmm9, zmm10, k1

        ; CALC_AAD_HASH needs to deal with multiple of 16 bytes
        sub     arg4, r11
        add     arg3, r11

        vmovq   xmm14, arg4 ; Save remaining length
        and     arg4, -16 ; Get multiple of 16 bytes

        or      arg4, arg4
        jz      no_full_blocks

        ;; Calculate GHASH of this segment
        CALC_AAD_HASH arg3, arg4, xmm8, arg1, zmm1, zmm2, zmm3, zmm4, zmm5, \
                      zmm6, zmm7, zmm9, zmm10, zmm11, zmm12, zmm13, zmm15, \
                      zmm16, zmm17, zmm18, zmm19, zmm20, r10, r11, r12, k1
	vmovdqu64	[arg2 + AadHash], xmm8	; ctx_data.aad hash = aad_hash

no_full_blocks:
        add     arg3, arg4 ; Point at partial block

        vmovq   arg4, xmm14 ; Restore original remaining length
        and     arg4, 15
        jz      exit_gmac_update

        ; Save next partial block
        mov	[arg2 + PBlockLen], arg4
        READ_SMALL_DATA_INPUT xmm1, arg3, arg4, r11, r12, k1
        vpshufb xmm1, [rel SHUF_MASK]
        vpxorq   xmm8, xmm1
        vmovdqu64 [arg2 + AadHash], xmm8

exit_gmac_update:
	FUNC_RESTORE

	ret

%ifdef SAFE_PARAM
error_gmac_update:
        ;; Clear reg and imb_errno
        IMB_ERR_CHECK_START rax

        ;; Check key_data != NULL
        IMB_ERR_CHECK_NULL arg1, rax, IMB_ERR_NULL_EXP_KEY

        ;; Check context_data != NULL
        IMB_ERR_CHECK_NULL arg2, rax, IMB_ERR_NULL_CTX

        ;; Check in != NULL (msg_len != 0)
        IMB_ERR_CHECK_NULL arg3, rax, IMB_ERR_NULL_SRC

        ;; Set imb_errno
        IMB_ERR_CHECK_END rax
        jmp     exit_gmac_update
%endif

mksection stack-noexec

%endif ; GCM_GMAC_API_VAES_AVX512_INC
