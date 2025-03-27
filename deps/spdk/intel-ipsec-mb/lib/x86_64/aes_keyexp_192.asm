;;
;; Copyright (c) 2012-2021, Intel Corporation
;;
;; Redistribution and use in source and binary forms, with or without
;; modification, are permitted provided that the following conditions are met:
;;
;;     * Redistributions of source code must retain the above copyright notice,
;;       this list of conditions and the following disclaimer.
;;     * Redistributions in binary form must reproduce the above copyright
;;       notice, this list of conditions and the following disclaimer in the
;;       documentation and/or other materials provided with the distribution.
;;     * Neither the name of Intel Corporation nor the names of its contributors
;;       may be used to endorse or promote products derived from this software
;;       without specific prior written permission.
;;
;; THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
;; AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
;; IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
;; DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
;; FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
;; DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
;; SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
;; CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
;; OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
;; OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
;;

%include "include/os.asm"
%define NO_AESNI_RENAME
%include "include/aesni_emu.inc"
%include "include/clear_regs.asm"
%include "include/cet.inc"
%include "include/error.inc"
%ifdef LINUX
%define KEY		rdi
%define EXP_ENC_KEYS	rsi
%define EXP_DEC_KEYS	rdx
%else
%define KEY		rcx
%define EXP_ENC_KEYS	rdx
%define EXP_DEC_KEYS	r8
%endif

%macro key_expansion_1_192_sse 1
	;; Assumes the xmm3 includes all zeros at this point.
        pshufd xmm2, xmm2, 11111111b
        shufps xmm3, xmm1, 00010000b
        pxor xmm1, xmm3
        shufps xmm3, xmm1, 10001100b
        pxor xmm1, xmm3
	pxor xmm1, xmm2
	movdqu [EXP_ENC_KEYS + %1], xmm1
%endmacro

; Calculate w10 and w11 using calculated w9 and known w4-w5
%macro key_expansion_2_192_sse 1
		movdqa xmm5, xmm4
		pslldq xmm5, 4
		shufps xmm6, xmm1, 11110000b
		pxor xmm6, xmm5
		pxor xmm4, xmm6
		pshufd xmm7, xmm4, 00001110b
		movdqu [EXP_ENC_KEYS + %1], xmm7
%endmacro

%macro key_dec_192_sse 1
  	movdqa  xmm0, [EXP_ENC_KEYS + 16 * %1]
	aesimc	xmm1, xmm0
	movdqa [EXP_DEC_KEYS + 16 * (12 - %1)], xmm1
%endmacro

%macro key_dec_192_sse_no_aesni 1
  	movdqa  xmm0, [EXP_ENC_KEYS + 16 * %1]
	EMULATE_AESIMC	xmm1, xmm0
	movdqa [EXP_DEC_KEYS + 16 * (12 - %1)], xmm1
%endmacro

%macro key_expansion_1_192_avx 1
	;; Assumes the xmm3 includes all zeros at this point.
        vpshufd xmm2, xmm2, 11111111b
        vshufps xmm3, xmm3, xmm1, 00010000b
        vpxor xmm1, xmm1, xmm3
        vshufps xmm3, xmm3, xmm1, 10001100b
        vpxor xmm1, xmm1, xmm3
	vpxor xmm1, xmm1, xmm2
	vmovdqu [EXP_ENC_KEYS + %1], xmm1
%endmacro

; Calculate w10 and w11 using calculated w9 and known w4-w5
%macro key_expansion_2_192_avx 1
		vmovdqa xmm5, xmm4
		vpslldq xmm5, xmm5, 4
		vshufps xmm6, xmm6, xmm1, 11110000b
		vpxor xmm6, xmm6, xmm5
		vpxor xmm4, xmm4, xmm6
		vpshufd xmm7, xmm4, 00001110b
		vmovdqu [EXP_ENC_KEYS + %1], xmm7
%endmacro

%macro key_dec_192_avx 1
  	vmovdqa  xmm0, [EXP_ENC_KEYS + 16 * %1]
	vaesimc	xmm1, xmm0
	vmovdqa [EXP_DEC_KEYS + 16 * (12 - %1)], xmm1
%endmacro

mksection .text

; void aes_keyexp_192(UINT128 *key,
;                     UINT128 *enc_exp_keys,
;                     UINT128 *dec_exp_keys);
;
; arg 1: rcx: pointer to key
; arg 2: rdx: pointer to expanded key array for encrypt
; arg 3: r8:  pointer to expanded key array for decrypt
;
MKGLOBAL(aes_keyexp_192_sse,function,)
aes_keyexp_192_sse:
        endbranch64
%ifdef SAFE_PARAM
        IMB_ERR_CHECK_RESET

        cmp     KEY, 0
        jz      error_keyexp_sse
        cmp     EXP_ENC_KEYS, 0
        jz      error_keyexp_sse
        cmp     EXP_DEC_KEYS, 0
        jz      error_keyexp_sse
%endif

%ifndef LINUX
	sub	rsp, 16*2 + 8
	movdqa	[rsp + 0*16], xmm6
	movdqa	[rsp + 1*16], xmm7
%endif

	movq xmm7, [KEY + 16]	; loading the AES key, 64 bits
        movq [EXP_ENC_KEYS + 16], xmm7  ; Storing key in memory where all key expansion
        pshufd xmm4, xmm7, 01001111b
        movdqu xmm1, [KEY]	; loading the AES key, 128 bits
        movdqu [EXP_ENC_KEYS], xmm1  ; Storing key in memory where all key expansion
        movdqa [EXP_DEC_KEYS + 16*0], xmm1
        movdqa [EXP_DEC_KEYS + 16*12], xmm1

        pxor xmm3, xmm3		; Set xmm3 to be all zeros. Required for the key_expansion
        pxor xmm6, xmm6		; Set xmm3 to be all zeros. Required for the key_expansion

        aeskeygenassist xmm2, xmm4, 0x1     ; Complete round key 1 and generate round key 2
        key_expansion_1_192_sse 24
		key_expansion_2_192_sse 40

        aeskeygenassist xmm2, xmm4, 0x2     ; Generate round key 3 and part of round key 4
        key_expansion_1_192_sse 48
		key_expansion_2_192_sse 64

        aeskeygenassist xmm2, xmm4, 0x4     ; Complete round key 4 and generate round key 5
        key_expansion_1_192_sse 72
		key_expansion_2_192_sse 88

        aeskeygenassist xmm2, xmm4, 0x8     ; Generate round key 6 and part of round key 7
        key_expansion_1_192_sse 96
		key_expansion_2_192_sse 112

        aeskeygenassist xmm2, xmm4, 0x10     ; Complete round key 7 and generate round key 8
        key_expansion_1_192_sse 120
		key_expansion_2_192_sse 136

        aeskeygenassist xmm2, xmm4, 0x20     ; Generate round key 9 and part of round key 10
        key_expansion_1_192_sse 144
		key_expansion_2_192_sse 160

        aeskeygenassist xmm2, xmm4, 0x40     ; Complete round key 10 and generate round key 11
        key_expansion_1_192_sse 168
		key_expansion_2_192_sse 184

        aeskeygenassist xmm2, xmm4, 0x80     ; Generate round key 12
        key_expansion_1_192_sse 192

;;;  we have already saved the 12 th key, which is pure input on the
;;;  ENC key path
	movdqa  xmm0, [EXP_ENC_KEYS + 16 * 12]
	movdqa [EXP_DEC_KEYS + 16*0], xmm0
;;;  generate remaining decrypt keys
	key_dec_192_sse 1
	key_dec_192_sse 2
	key_dec_192_sse 3
	key_dec_192_sse 4
	key_dec_192_sse 5
	key_dec_192_sse 6
	key_dec_192_sse 7
	key_dec_192_sse 8
	key_dec_192_sse 9
	key_dec_192_sse 10
	key_dec_192_sse 11

%ifdef SAFE_DATA
        clear_scratch_gps_asm
        clear_scratch_xmms_sse_asm
%endif

%ifndef LINUX
	movdqa	xmm6, [rsp + 0*16]
	movdqa	xmm7, [rsp + 1*16]
	add	rsp, 16*2 + 8
%endif

aes_keyexp_192_sse_return:
	ret

%ifdef SAFE_PARAM
error_keyexp_sse:
        IMB_ERR_CHECK_START rax
        IMB_ERR_CHECK_NULL KEY, rax, IMB_ERR_NULL_KEY
        IMB_ERR_CHECK_NULL EXP_ENC_KEYS, rax, IMB_ERR_NULL_EXP_KEY
        IMB_ERR_CHECK_NULL EXP_DEC_KEYS, rax, IMB_ERR_NULL_EXP_KEY
        IMB_ERR_CHECK_END rax

        jmp aes_keyexp_192_sse_return
%endif

MKGLOBAL(aes_keyexp_192_sse_no_aesni,function,)
aes_keyexp_192_sse_no_aesni:
        endbranch64
%ifdef SAFE_PARAM
        IMB_ERR_CHECK_RESET

        cmp     KEY, 0
        jz      error_keyexp_sse_no_aesni
        cmp     EXP_ENC_KEYS, 0
        jz      error_keyexp_sse_no_aesni
        cmp     EXP_DEC_KEYS, 0
        jz      error_keyexp_sse_no_aesni
%endif

%ifndef LINUX
	sub	rsp, 16*2 + 8
	movdqa	[rsp + 0*16], xmm6
	movdqa	[rsp + 1*16], xmm7
%endif

	movq xmm7, [KEY + 16]	; loading the AES key, 64 bits
        movq [EXP_ENC_KEYS + 16], xmm7  ; Storing key in memory where all key expansion
        pshufd xmm4, xmm7, 01001111b
        movdqu xmm1, [KEY]	; loading the AES key, 128 bits
        movdqu [EXP_ENC_KEYS], xmm1  ; Storing key in memory where all key expansion
        movdqa [EXP_DEC_KEYS + 16*0], xmm1
        movdqa [EXP_DEC_KEYS + 16*12], xmm1

        pxor xmm3, xmm3		; Set xmm3 to be all zeros. Required for the key_expansion
        pxor xmm6, xmm6		; Set xmm3 to be all zeros. Required for the key_expansion

        EMULATE_AESKEYGENASSIST xmm2, xmm4, 0x1     ; Complete round key 1 and generate round key 2
        key_expansion_1_192_sse 24
		key_expansion_2_192_sse 40

        EMULATE_AESKEYGENASSIST xmm2, xmm4, 0x2     ; Generate round key 3 and part of round key 4
        key_expansion_1_192_sse 48
		key_expansion_2_192_sse 64

        EMULATE_AESKEYGENASSIST xmm2, xmm4, 0x4     ; Complete round key 4 and generate round key 5
        key_expansion_1_192_sse 72
		key_expansion_2_192_sse 88

        EMULATE_AESKEYGENASSIST xmm2, xmm4, 0x8     ; Generate round key 6 and part of round key 7
        key_expansion_1_192_sse 96
		key_expansion_2_192_sse 112

        EMULATE_AESKEYGENASSIST xmm2, xmm4, 0x10     ; Complete round key 7 and generate round key 8
        key_expansion_1_192_sse 120
		key_expansion_2_192_sse 136

        EMULATE_AESKEYGENASSIST xmm2, xmm4, 0x20     ; Generate round key 9 and part of round key 10
        key_expansion_1_192_sse 144
		key_expansion_2_192_sse 160

        EMULATE_AESKEYGENASSIST xmm2, xmm4, 0x40     ; Complete round key 10 and generate round key 11
        key_expansion_1_192_sse 168
		key_expansion_2_192_sse 184

        EMULATE_AESKEYGENASSIST xmm2, xmm4, 0x80     ; Generate round key 12
        key_expansion_1_192_sse 192

;;;  we have already saved the 12 th key, which is pure input on the
;;;  ENC key path
	movdqa  xmm0, [EXP_ENC_KEYS + 16 * 12]
	movdqa [EXP_DEC_KEYS + 16*0], xmm0
;;;  generate remaining decrypt keys
	key_dec_192_sse_no_aesni 1
	key_dec_192_sse_no_aesni 2
	key_dec_192_sse_no_aesni 3
	key_dec_192_sse_no_aesni 4
	key_dec_192_sse_no_aesni 5
	key_dec_192_sse_no_aesni 6
	key_dec_192_sse_no_aesni 7
	key_dec_192_sse_no_aesni 8
	key_dec_192_sse_no_aesni 9
	key_dec_192_sse_no_aesni 10
	key_dec_192_sse_no_aesni 11

%ifdef SAFE_DATA
        clear_scratch_gps_asm
        clear_scratch_xmms_sse_asm
%endif

%ifndef LINUX
	movdqa	xmm6, [rsp + 0*16]
	movdqa	xmm7, [rsp + 1*16]
	add	rsp, 16*2 + 8
%endif

aes_keyexp_192_sse_no_aesni_return:
	ret

%ifdef SAFE_PARAM
error_keyexp_sse_no_aesni:
        IMB_ERR_CHECK_START rax
        IMB_ERR_CHECK_NULL KEY, rax, IMB_ERR_NULL_KEY
        IMB_ERR_CHECK_NULL EXP_ENC_KEYS, rax, IMB_ERR_NULL_EXP_KEY
        IMB_ERR_CHECK_NULL EXP_DEC_KEYS, rax, IMB_ERR_NULL_EXP_KEY
        IMB_ERR_CHECK_END rax

        jmp aes_keyexp_192_sse_no_aesni_return
%endif

MKGLOBAL(aes_keyexp_192_avx,function,)
MKGLOBAL(aes_keyexp_192_avx2,function,)
MKGLOBAL(aes_keyexp_192_avx512,function,)
aes_keyexp_192_avx:
aes_keyexp_192_avx2:
aes_keyexp_192_avx512:
        endbranch64
%ifdef SAFE_PARAM
        IMB_ERR_CHECK_RESET

        cmp     KEY, 0
        jz      error_keyexp_avx
        cmp     EXP_ENC_KEYS, 0
        jz      error_keyexp_avx
        cmp     EXP_DEC_KEYS, 0
        jz      error_keyexp_avx
%endif

%ifndef LINUX
	sub	rsp, 16*2 + 8
	vmovdqa	[rsp + 0*16], xmm6
	vmovdqa	[rsp + 1*16], xmm7
%endif
	vmovq xmm7, [KEY + 16]	; loading the AES key, 64 bits
        vmovq [EXP_ENC_KEYS + 16], xmm7  ; Storing key in memory where all key expansion
        vpshufd xmm4, xmm7, 01001111b
        vmovdqu xmm1, [KEY]	; loading the AES key, 128 bits
        vmovdqu [EXP_ENC_KEYS], xmm1  ; Storing key in memory where all key expansion
        vmovdqa [EXP_DEC_KEYS + 16*0], xmm1
        vmovdqa [EXP_DEC_KEYS + 16*12], xmm1

        vpxor xmm3, xmm3, xmm3
        vpxor xmm6, xmm6, xmm6

        vaeskeygenassist xmm2, xmm4, 0x1      ; Complete round key 1 and generate round key 2
        key_expansion_1_192_avx 24
		key_expansion_2_192_avx 40

        vaeskeygenassist xmm2, xmm4, 0x2     ; Generate round key 3 and part of round key 4
        key_expansion_1_192_avx 48
		key_expansion_2_192_avx 64

        vaeskeygenassist xmm2, xmm4, 0x4     ; Complete round key 4 and generate round key 5
        key_expansion_1_192_avx 72
		key_expansion_2_192_avx 88

        vaeskeygenassist xmm2, xmm4, 0x8     ; Generate round key 6 and part of round key 7
        key_expansion_1_192_avx 96
		key_expansion_2_192_avx 112

        vaeskeygenassist xmm2, xmm4, 0x10    ; Complete round key 7 and generate round key 8
        key_expansion_1_192_avx 120
		key_expansion_2_192_avx 136

        vaeskeygenassist xmm2, xmm4, 0x20    ; Generate round key 9 and part of round key 10
        key_expansion_1_192_avx 144
		key_expansion_2_192_avx 160

        vaeskeygenassist xmm2, xmm4, 0x40    ; Complete round key 10 and generate round key 11
        key_expansion_1_192_avx 168
		key_expansion_2_192_avx 184

        vaeskeygenassist xmm2, xmm4, 0x80   ; Generate round key 12
        key_expansion_1_192_avx 192

;;;  we have already saved the 12 th key, which is pure input on the
;;;  ENC key path
	vmovdqa  xmm0, [EXP_ENC_KEYS + 16 * 12]
	vmovdqa [EXP_DEC_KEYS + 16*0], xmm0
;;;  generate remaining decrypt keys
	key_dec_192_avx 1
	key_dec_192_avx 2
	key_dec_192_avx 3
	key_dec_192_avx 4
	key_dec_192_avx 5
	key_dec_192_avx 6
	key_dec_192_avx 7
	key_dec_192_avx 8
	key_dec_192_avx 9
	key_dec_192_avx 10
	key_dec_192_avx 11

%ifdef SAFE_DATA
        clear_scratch_gps_asm
        clear_scratch_xmms_avx_asm
%endif

%ifndef LINUX
	vmovdqa	xmm6, [rsp + 0*16]
	vmovdqa	xmm7, [rsp + 1*16]
	add	rsp, 16*2 + 8
%endif

aes_keyexp_192_avx_return:
	ret

%ifdef SAFE_PARAM
error_keyexp_avx:
        IMB_ERR_CHECK_START rax
        IMB_ERR_CHECK_NULL KEY, rax, IMB_ERR_NULL_KEY
        IMB_ERR_CHECK_NULL EXP_ENC_KEYS, rax, IMB_ERR_NULL_EXP_KEY
        IMB_ERR_CHECK_NULL EXP_DEC_KEYS, rax, IMB_ERR_NULL_EXP_KEY
        IMB_ERR_CHECK_END rax

        jmp aes_keyexp_192_avx_return
%endif

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; void aes_keyexp_192_enc_sse(UINT128 *key,
;                             UINT128 *enc_exp_keys);
;
; arg 1: rcx: pointer to key
; arg 2: rdx: pointer to expanded key array for encrypt
;
MKGLOBAL(aes_keyexp_192_enc_sse,function,)
aes_keyexp_192_enc_sse:
        endbranch64
%ifdef SAFE_PARAM
        IMB_ERR_CHECK_RESET

        cmp     KEY, 0
        jz      error_keyexp_enc_sse
        cmp     EXP_ENC_KEYS, 0
        jz      error_keyexp_enc_sse
%endif

%ifndef LINUX
	sub	rsp, 16*2 + 8
	movdqa	[rsp + 0*16], xmm6
	movdqa	[rsp + 1*16], xmm7
%endif

	movq xmm7, [KEY + 16]	; loading the AES key, 64 bits
        movq [EXP_ENC_KEYS + 16], xmm7  ; Storing key in memory where all key expansion
        pshufd xmm4, xmm7, 01001111b
        movdqu xmm1, [KEY]	; loading the AES key, 128 bits
        movdqu [EXP_ENC_KEYS], xmm1  ; Storing key in memory where all key expansion

        pxor xmm3, xmm3		; Set xmm3 to be all zeros. Required for the key_expansion.
        pxor xmm6, xmm6		; Set xmm3 to be all zeros. Required for the key_expansion.

        aeskeygenassist xmm2, xmm4, 0x1     ; Complete round key 1 and generate round key 2
        key_expansion_1_192_sse 24
		key_expansion_2_192_sse 40

        aeskeygenassist xmm2, xmm4, 0x2     ; Generate round key 3 and part of round key 4
        key_expansion_1_192_sse 48
		key_expansion_2_192_sse 64

        aeskeygenassist xmm2, xmm4, 0x4     ; Complete round key 4 and generate round key 5
        key_expansion_1_192_sse 72
		key_expansion_2_192_sse 88

        aeskeygenassist xmm2, xmm4, 0x8     ; Generate round key 6 and part of round key 7
        key_expansion_1_192_sse 96
		key_expansion_2_192_sse 112

        aeskeygenassist xmm2, xmm4, 0x10     ; Complete round key 7 and generate round key 8
        key_expansion_1_192_sse 120
		key_expansion_2_192_sse 136

        aeskeygenassist xmm2, xmm4, 0x20     ; Generate round key 9 and part of round key 10
        key_expansion_1_192_sse 144
		key_expansion_2_192_sse 160

        aeskeygenassist xmm2, xmm4, 0x40     ; Complete round key 10 and generate round key 11
        key_expansion_1_192_sse 168
		key_expansion_2_192_sse 184

        aeskeygenassist xmm2, xmm4, 0x80     ; Generate round key 12
        key_expansion_1_192_sse 192

%ifdef SAFE_DATA
        clear_scratch_gps_asm
        clear_scratch_xmms_sse_asm
%endif

%ifndef LINUX
	movdqa	xmm6, [rsp + 0*16]
	movdqa	xmm7, [rsp + 1*16]
	add	rsp, 16*2 + 8
%endif

aes_keyexp_192_enc_sse_return:
	ret

%ifdef SAFE_PARAM
error_keyexp_enc_sse:
        IMB_ERR_CHECK_START rax
        IMB_ERR_CHECK_NULL KEY, rax, IMB_ERR_NULL_KEY
        IMB_ERR_CHECK_NULL EXP_ENC_KEYS, rax, IMB_ERR_NULL_EXP_KEY
        IMB_ERR_CHECK_END rax

        jmp aes_keyexp_192_enc_sse_return
%endif

MKGLOBAL(aes_keyexp_192_enc_sse_no_aesni,function,)
aes_keyexp_192_enc_sse_no_aesni:
        endbranch64
%ifdef SAFE_PARAM
        IMB_ERR_CHECK_RESET

        cmp     KEY, 0
        jz      error_keyexp_enc_sse_no_aesni
        cmp     EXP_ENC_KEYS, 0
        jz      error_keyexp_enc_sse_no_aesni
%endif

%ifndef LINUX
	sub	rsp, 16*2 + 8
	movdqa	[rsp + 0*16], xmm6
	movdqa	[rsp + 1*16], xmm7
%endif

	movq xmm7, [KEY + 16]	; loading the AES key, 64 bits
        movq [EXP_ENC_KEYS + 16], xmm7  ; Storing key in memory where all key expansion
        pshufd xmm4, xmm7, 01001111b
        movdqu xmm1, [KEY]	; loading the AES key, 128 bits
        movdqu [EXP_ENC_KEYS], xmm1  ; Storing key in memory where all key expansion

        pxor xmm3, xmm3		; Set xmm3 to be all zeros. Required for the key_expansion.
        pxor xmm6, xmm6		; Set xmm3 to be all zeros. Required for the key_expansion.

        EMULATE_AESKEYGENASSIST xmm2, xmm4, 0x1     ; Complete round key 1 and generate round key 2
        key_expansion_1_192_sse 24
		key_expansion_2_192_sse 40

        EMULATE_AESKEYGENASSIST xmm2, xmm4, 0x2     ; Generate round key 3 and part of round key 4
        key_expansion_1_192_sse 48
		key_expansion_2_192_sse 64

        EMULATE_AESKEYGENASSIST xmm2, xmm4, 0x4     ; Complete round key 4 and generate round key 5
        key_expansion_1_192_sse 72
		key_expansion_2_192_sse 88

        EMULATE_AESKEYGENASSIST xmm2, xmm4, 0x8     ; Generate round key 6 and part of round key 7
        key_expansion_1_192_sse 96
		key_expansion_2_192_sse 112

        EMULATE_AESKEYGENASSIST xmm2, xmm4, 0x10     ; Complete round key 7 and generate round key 8
        key_expansion_1_192_sse 120
		key_expansion_2_192_sse 136

        EMULATE_AESKEYGENASSIST xmm2, xmm4, 0x20     ; Generate round key 9 and part of round key 10
        key_expansion_1_192_sse 144
		key_expansion_2_192_sse 160

        EMULATE_AESKEYGENASSIST xmm2, xmm4, 0x40     ; Complete round key 10 and generate round key 11
        key_expansion_1_192_sse 168
		key_expansion_2_192_sse 184

        EMULATE_AESKEYGENASSIST xmm2, xmm4, 0x80     ; Generate round key 12
        key_expansion_1_192_sse 192

%ifdef SAFE_DATA
        clear_scratch_gps_asm
        clear_scratch_xmms_sse_asm
%endif

%ifndef LINUX
	movdqa	xmm6, [rsp + 0*16]
	movdqa	xmm7, [rsp + 1*16]
	add	rsp, 16*2 + 8
%endif

aes_keyexp_192_enc_sse_no_aesni_return:
	ret

%ifdef SAFE_PARAM
error_keyexp_enc_sse_no_aesni:
        IMB_ERR_CHECK_START rax
        IMB_ERR_CHECK_NULL KEY, rax, IMB_ERR_NULL_KEY
        IMB_ERR_CHECK_NULL EXP_ENC_KEYS, rax, IMB_ERR_NULL_EXP_KEY
        IMB_ERR_CHECK_END rax

        jmp aes_keyexp_192_enc_sse_no_aesni_return
%endif

MKGLOBAL(aes_keyexp_192_enc_avx,function,)
MKGLOBAL(aes_keyexp_192_enc_avx2,function,)
MKGLOBAL(aes_keyexp_192_enc_avx512,function,)
aes_keyexp_192_enc_avx:
aes_keyexp_192_enc_avx2:
aes_keyexp_192_enc_avx512:
        endbranch64
%ifdef SAFE_PARAM
        IMB_ERR_CHECK_RESET

        cmp     KEY, 0
        jz      error_keyexp_enc_avx
        cmp     EXP_ENC_KEYS, 0
        jz      error_keyexp_enc_avx
%endif

%ifndef LINUX
	sub	rsp, 16*2 + 8
	vmovdqa	[rsp + 0*16], xmm6
	vmovdqa	[rsp + 1*16], xmm7
%endif

	vmovq xmm7, [KEY + 16]	; loading the AES key, 64 bits
        vmovq [EXP_ENC_KEYS + 16], xmm7  ; Storing key in memory where all key expansion
        vpshufd xmm4, xmm7, 01001111b
        vmovdqu xmm1, [KEY]	; loading the AES key, 128 bits
        vmovdqu [EXP_ENC_KEYS], xmm1  ; Storing key in memory where all key expansion

        vpxor xmm3, xmm3, xmm3
        vpxor xmm6, xmm6, xmm6

        vaeskeygenassist xmm2, xmm4, 0x1      ; Complete round key 1 and generate round key 2
        key_expansion_1_192_avx 24
		key_expansion_2_192_avx 40

        vaeskeygenassist xmm2, xmm4, 0x2     ; Generate round key 3 and part of round key 4
        key_expansion_1_192_avx 48
		key_expansion_2_192_avx 64

        vaeskeygenassist xmm2, xmm4, 0x4     ; Complete round key 4 and generate round key 5
        key_expansion_1_192_avx 72
		key_expansion_2_192_avx 88

        vaeskeygenassist xmm2, xmm4, 0x8     ; Generate round key 6 and part of round key 7
        key_expansion_1_192_avx 96
		key_expansion_2_192_avx 112

        vaeskeygenassist xmm2, xmm4, 0x10    ; Complete round key 7 and generate round key 8
        key_expansion_1_192_avx 120
		key_expansion_2_192_avx 136

        vaeskeygenassist xmm2, xmm4, 0x20    ; Generate round key 9 and part of round key 10
        key_expansion_1_192_avx 144
		key_expansion_2_192_avx 160

        vaeskeygenassist xmm2, xmm4, 0x40    ; Complete round key 10 and generate round key 11
        key_expansion_1_192_avx 168
		key_expansion_2_192_avx 184

        vaeskeygenassist xmm2, xmm4, 0x80   ; Generate round key 12
        key_expansion_1_192_avx 192

%ifdef SAFE_DATA
        clear_scratch_gps_asm
        clear_scratch_xmms_avx_asm
%endif

%ifndef LINUX
	vmovdqa	xmm6, [rsp + 0*16]
	vmovdqa	xmm7, [rsp + 1*16]
	add	rsp, 16*2 + 8
%endif

aes_keyexp_192_enc_avx_return:
	ret

%ifdef SAFE_PARAM
error_keyexp_enc_avx:
        IMB_ERR_CHECK_START rax
        IMB_ERR_CHECK_NULL KEY, rax, IMB_ERR_NULL_KEY
        IMB_ERR_CHECK_NULL EXP_ENC_KEYS, rax, IMB_ERR_NULL_EXP_KEY
        IMB_ERR_CHECK_END rax

        jmp aes_keyexp_192_enc_avx_return
%endif

mksection stack-noexec
