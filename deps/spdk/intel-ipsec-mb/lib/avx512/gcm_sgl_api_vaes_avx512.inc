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
%include "include/os.asm"

%ifndef GCM_SGL_API_VAES_AVX512_INC
%define GCM_SGL_API_VAES_AVX512_INC

mksection .text
default rel

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;void   aes_gcm_init_128_vaes_avx512 / aes_gcm_init_192_vaes_avx512 / aes_gcm_init_256_vaes_avx512
;       (const struct gcm_key_data *key_data,
;        struct gcm_context_data *context_data,
;        u8       *iv,
;        const u8 *aad,
;        u64      aad_len);
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
MKGLOBAL(FN_NAME(init,_),function,)
FN_NAME(init,_):
        endbranch64
        FUNC_SAVE

%ifdef SAFE_PARAM
        ;; Reset imb_errno
        IMB_ERR_CHECK_RESET

        ;; Check key_data != NULL
        cmp     arg1, 0
        jz      error_init

        ;; Check context_data != NULL
        cmp     arg2, 0
        jz      error_init

        ;; Check IV != NULL
        cmp     arg3, 0
        jz      error_init

        ;; Check if aad_len == 0
        cmp     arg5, 0
        jz      skip_aad_check_init

        ;; Check aad != NULL (aad_len != 0)
        cmp     arg4, 0
        jz      error_init

skip_aad_check_init:
%endif
        GCM_INIT arg1, arg2, arg3, arg4, arg5, r10, r11, r12, k1, xmm14, xmm2, \
                zmm1, zmm3, zmm4, zmm5, zmm6, zmm7, zmm8, zmm9, zmm10, zmm11, \
                zmm12, zmm13, zmm15, zmm16, zmm17, zmm18, zmm19, zmm20, multi_call

exit_init:

        FUNC_RESTORE
        ret

%ifdef SAFE_PARAM
error_init:
        ;; Clear reg and imb_errno
        IMB_ERR_CHECK_START rax

        ;; Check key_data != NULL
        IMB_ERR_CHECK_NULL arg1, rax, IMB_ERR_NULL_EXP_KEY

        ;; Check context_data != NULL
        IMB_ERR_CHECK_NULL arg2, rax, IMB_ERR_NULL_CTX

        ;; Check IV != NULL
        IMB_ERR_CHECK_NULL arg3, rax, IMB_ERR_NULL_IV

        ;; Check if aad_len == 0
        cmp     arg5, 0
        jz      skip_aad_check_error_init

        ;; Check aad != NULL (aad_len != 0)
        IMB_ERR_CHECK_NULL arg4, rax, IMB_ERR_NULL_AAD

skip_aad_check_error_init:

        ;; Set imb_errno
        IMB_ERR_CHECK_END rax
        jmp     exit_init
%endif

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;void   aes_gcm_init_var_iv_128_vaes_avx512 / aes_gcm_init_var_iv_192_vaes_avx512 /
;       aes_gcm_init_var_iv_256_vaes_avx512
;       (const struct gcm_key_data *key_data,
;        struct gcm_context_data *context_data,
;        u8        *iv,
;        const u64 iv_len,
;        const u8  *aad,
;        const u64 aad_len);
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
MKGLOBAL(FN_NAME(init_var_iv,_),function,)
FN_NAME(init_var_iv,_):
        endbranch64
        FUNC_SAVE

%ifdef SAFE_PARAM
        ;; Reset imb_errno
        IMB_ERR_CHECK_RESET

        ;; Check key_data != NULL
        cmp     arg1, 0
        jz      error_init_IV

        ;; Check context_data != NULL
        cmp     arg2, 0
        jz      error_init_IV

        ;; Check IV != NULL
        cmp     arg3, 0
        jz      error_init_IV

        ;; Check iv_len != 0
        cmp     arg4, 0
        jz      error_init_IV

        ;; Check if aad_len == 0
        cmp     arg6, 0
        jz      skip_aad_check_init_IV

        ;; Check aad != NULL (aad_len != 0)
        cmp     arg5, 0
        jz      error_init_IV

skip_aad_check_init_IV:
%endif
        cmp     arg4, 12
        je      iv_len_12_init_IV

        GCM_INIT arg1, arg2, arg3, arg5, arg6, r10, r11, r12, k1, xmm14, xmm2, \
                zmm1, zmm11, zmm3, zmm4, zmm5, zmm6, zmm7, zmm8, zmm9, zmm10, \
                zmm12, zmm13, zmm15, zmm16, zmm17, zmm18, zmm19, zmm20, multi_call, arg4
        jmp     skip_iv_len_12_init_IV

iv_len_12_init_IV:
        GCM_INIT arg1, arg2, arg3, arg5, arg6, r10, r11, r12, k1, xmm14, xmm2, \
                zmm1, zmm11, zmm3, zmm4, zmm5, zmm6, zmm7, zmm8, zmm9, zmm10, \
                zmm12, zmm13, zmm15, zmm16, zmm17, zmm18, zmm19, zmm20, multi_call

skip_iv_len_12_init_IV:
%ifdef SAFE_DATA
        clear_scratch_gps_asm
        clear_scratch_zmms_asm
%endif
exit_init_IV:

        FUNC_RESTORE
        ret

%ifdef SAFE_PARAM
error_init_IV:
        ;; Clear reg and imb_errno
        IMB_ERR_CHECK_START rax

        ;; Check key_data != NULL
        IMB_ERR_CHECK_NULL arg1, rax, IMB_ERR_NULL_EXP_KEY

        ;; Check context_data != NULL
        IMB_ERR_CHECK_NULL arg2, rax, IMB_ERR_NULL_CTX

        ;; Check IV != NULL
        IMB_ERR_CHECK_NULL arg3, rax, IMB_ERR_NULL_IV

        ;; Check iv_len != 0
        IMB_ERR_CHECK_ZERO arg4, rax, IMB_ERR_IV_LEN

        ;; Check if aad_len == 0
        cmp     arg6, 0
        jz      skip_aad_check_error_init_IV

        ;; Check aad != NULL (aad_len != 0)
        IMB_ERR_CHECK_NULL arg5, rax, IMB_ERR_NULL_AAD

skip_aad_check_error_init_IV:

        ;; Set imb_errno
        IMB_ERR_CHECK_END rax
        jmp     exit_init_IV
%endif


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;void   aes_gcm_enc_128_update_vaes_avx512 / aes_gcm_enc_192_update_vaes_avx512 /
;       aes_gcm_enc_256_update_vaes_avx512
;       (const struct gcm_key_data *key_data,
;        struct gcm_context_data *context_data,
;        u8       *out,
;        const u8 *in,
;        u64      msg_len);
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
MKGLOBAL(FN_NAME(enc,_update_),function,)
FN_NAME(enc,_update_):
        endbranch64
        FUNC_SAVE

%ifdef SAFE_PARAM
        ;; Reset imb_errno
        IMB_ERR_CHECK_RESET

        ;; Load max len to reg on windows
        INIT_GCM_MAX_LENGTH

        ;; Check key_data != NULL
        cmp     arg1, 0
        jz      error_update_enc

        ;; Check context_data != NULL
        cmp     arg2, 0
        jz      error_update_enc

        ;; Check if msg_len == 0
        cmp     arg5, 0
        jz      error_update_enc

        ;; Check if msg_len > max_len
        cmp     arg5, GCM_MAX_LENGTH
        ja      error_update_enc

        ;; Check out != NULL (msg_len != 0)
        cmp     arg3, 0
        jz      error_update_enc

        ;; Check in != NULL (msg_len != 0)
        cmp     arg4, 0
        jz      error_update_enc
%endif
        GCM_ENC_DEC arg1, arg2, arg3, arg4, arg5, ENC, multi_call

exit_update_enc:
        FUNC_RESTORE
        ret

%ifdef SAFE_PARAM
error_update_enc:
        ;; Clear reg and imb_errno
        IMB_ERR_CHECK_START rax

        ;; Check key_data != NULL
        IMB_ERR_CHECK_NULL arg1, rax, IMB_ERR_NULL_EXP_KEY

        ;; Check context_data != NULL
        IMB_ERR_CHECK_NULL arg2, rax, IMB_ERR_NULL_CTX

        ;; Check if plaintext_len == 0
        cmp     arg5, 0
        jz      skip_in_out_check_error_update_enc

        ;; Check if msg_len > max_len
        IMB_ERR_CHECK_ABOVE arg5, GCM_MAX_LENGTH, rax, IMB_ERR_CIPH_LEN

        ;; Check out != NULL
        IMB_ERR_CHECK_NULL arg3, rax, IMB_ERR_NULL_DST

        ;; Check in != NULL (plaintext_len != 0)
        IMB_ERR_CHECK_NULL arg4, rax, IMB_ERR_NULL_SRC

skip_in_out_check_error_update_enc:
        ;; Set imb_errno
        IMB_ERR_CHECK_END rax
        jmp     exit_update_enc
%endif


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;void   aes_gcm_dec_128_update_vaes_avx512 / aes_gcm_dec_192_update_vaes_avx512 /
;       aes_gcm_dec_256_update_vaes_avx512
;       (const struct gcm_key_data *key_data,
;        struct gcm_context_data *context_data,
;        u8       *out,
;        const u8 *in,
;        u64      msg_len);
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
MKGLOBAL(FN_NAME(dec,_update_),function,)
FN_NAME(dec,_update_):
        endbranch64
        FUNC_SAVE

%ifdef SAFE_PARAM
        ;; Reset imb_errno
        IMB_ERR_CHECK_RESET

        ;; Load max len to reg on windows
        INIT_GCM_MAX_LENGTH

        ;; Check key_data != NULL
        cmp     arg1, 0
        jz      error_update_dec

        ;; Check context_data != NULL
        cmp     arg2, 0
        jz      error_update_dec

        ;; Check if msg_len == 0
        cmp     arg5, 0
        jz      error_update_dec

        ;; Check if msg_len > max_len
        cmp     arg5, GCM_MAX_LENGTH
        ja      error_update_dec

        ;; Check out != NULL (msg_len != 0)
        cmp     arg3, 0
        jz      error_update_dec

        ;; Check in != NULL (msg_len != 0)
        cmp     arg4, 0
        jz      error_update_dec
%endif

        GCM_ENC_DEC arg1, arg2, arg3, arg4, arg5, DEC, multi_call

exit_update_dec:
        FUNC_RESTORE
        ret

%ifdef SAFE_PARAM
error_update_dec:
        ;; Clear reg and imb_errno
        IMB_ERR_CHECK_START rax

        ;; Check key_data != NULL
        IMB_ERR_CHECK_NULL arg1, rax, IMB_ERR_NULL_EXP_KEY

        ;; Check context_data != NULL
        IMB_ERR_CHECK_NULL arg2, rax, IMB_ERR_NULL_CTX

        ;; Check if plaintext_len == 0
        cmp     arg5, 0
        jz      skip_in_out_check_error_update_dec

        ;; Check if msg_len > max_len
        IMB_ERR_CHECK_ABOVE arg5, GCM_MAX_LENGTH, rax, IMB_ERR_CIPH_LEN

        ;; Check out != NULL
        IMB_ERR_CHECK_NULL arg3, rax, IMB_ERR_NULL_DST

        ;; Check in != NULL (plaintext_len != 0)
        IMB_ERR_CHECK_NULL arg4, rax, IMB_ERR_NULL_SRC

skip_in_out_check_error_update_dec:
        ;; Set imb_errno
        IMB_ERR_CHECK_END rax
        jmp     exit_update_dec
%endif


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;void   aes_gcm_enc_128_finalize_vaes_avx512 / aes_gcm_enc_192_finalize_vaes_avx512 /
;       aes_gcm_enc_256_finalize_vaes_avx512
;       (const struct gcm_key_data *key_data,
;        struct gcm_context_data *context_data,
;        u8       *auth_tag,
;        u64      auth_tag_len);
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
MKGLOBAL(FN_NAME(enc,_finalize_),function,)
FN_NAME(enc,_finalize_):
        endbranch64
;; All parameters are passed through registers
%ifdef SAFE_PARAM
        ;; Reset imb_errno
        IMB_ERR_CHECK_RESET

        ;; Check key_data != NULL
        cmp     arg1, 0
        jz      error_enc_fin

        ;; Check context_data != NULL
        cmp     arg2, 0
        jz      error_enc_fin

        ;; Check auth_tag != NULL
        cmp     arg3, 0
        jz      error_enc_fin

        ;; Check auth_tag_len == 0 or > 16
        cmp     arg4, 0
        jz      error_enc_fin

        cmp     arg4, 16
        ja      error_enc_fin
%endif

        FUNC_SAVE
        GCM_COMPLETE    arg1, arg2, arg3, arg4, multi_call, k1, r10, r11, r12

        FUNC_RESTORE

exit_enc_fin:
        ret

%ifdef SAFE_PARAM
error_enc_fin:
        ;; Clear reg and imb_errno
        IMB_ERR_CHECK_START rax

        ;; Check key_data != NULL
        IMB_ERR_CHECK_NULL arg1, rax, IMB_ERR_NULL_EXP_KEY

        ;; Check context_data != NULL
        IMB_ERR_CHECK_NULL arg2, rax, IMB_ERR_NULL_CTX

        ;; Check auth_tag != NULL
        IMB_ERR_CHECK_NULL arg3, rax, IMB_ERR_NULL_AUTH

        ;; Check auth_tag_len == 0 or > 16
        IMB_ERR_CHECK_ZERO arg4, rax, IMB_ERR_AUTH_TAG_LEN

        IMB_ERR_CHECK_ABOVE arg4, 16, rax, IMB_ERR_AUTH_TAG_LEN

        ;; Set imb_errno
        IMB_ERR_CHECK_END rax
        jmp     exit_enc_fin
%endif


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;void   aes_gcm_dec_128_finalize_vaes_avx512 / aes_gcm_dec_192_finalize_vaes_avx512
;       aes_gcm_dec_256_finalize_vaes_avx512
;       (const struct gcm_key_data *key_data,
;        struct gcm_context_data *context_data,
;        u8       *auth_tag,
;        u64      auth_tag_len);
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
MKGLOBAL(FN_NAME(dec,_finalize_),function,)
FN_NAME(dec,_finalize_):
        endbranch64
;; All parameters are passed through registers
%ifdef SAFE_PARAM
        ;; Reset imb_errno
        IMB_ERR_CHECK_RESET

        ;; Check key_data != NULL
        cmp     arg1, 0
        jz      error_dec_fin

        ;; Check context_data != NULL
        cmp     arg2, 0
        jz      error_dec_fin

        ;; Check auth_tag != NULL
        cmp     arg3, 0
        jz      error_dec_fin

        ;; Check auth_tag_len == 0 or > 16
        cmp     arg4, 0
        jz      error_dec_fin

        cmp     arg4, 16
        ja      error_dec_fin
%endif

        FUNC_SAVE
        GCM_COMPLETE    arg1, arg2, arg3, arg4, multi_call, k1, r10, r11, r12

        FUNC_RESTORE

exit_dec_fin:
        ret

%ifdef SAFE_PARAM
error_dec_fin:
        ;; Clear reg and imb_errno
        IMB_ERR_CHECK_START rax

        ;; Check key_data != NULL
        IMB_ERR_CHECK_NULL arg1, rax, IMB_ERR_NULL_EXP_KEY

        ;; Check context_data != NULL
        IMB_ERR_CHECK_NULL arg2, rax, IMB_ERR_NULL_CTX

        ;; Check auth_tag != NULL
        IMB_ERR_CHECK_NULL arg3, rax, IMB_ERR_NULL_AUTH

        ;; Check auth_tag_len == 0 or > 16
        IMB_ERR_CHECK_ZERO arg4, rax, IMB_ERR_AUTH_TAG_LEN

        IMB_ERR_CHECK_ABOVE arg4, 16, rax, IMB_ERR_AUTH_TAG_LEN

        ;; Set imb_errno
        IMB_ERR_CHECK_END rax
        jmp     exit_dec_fin
%endif

mksection stack-noexec

%endif ; GCM_SGL_API_VAES_AVX512_INC
