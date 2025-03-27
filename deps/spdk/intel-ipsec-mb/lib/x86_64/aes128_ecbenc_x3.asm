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

; Routines to do simple AES ECB Enc on one stream with 3 blocks

;void
; aes128_ecbenc_x3_sse(void *in, void *keys, void *out1, void *out2, void *out3);
;void
; aes128_ecbenc_x3_avx(void *in, void *keys, void *out1, void *out2, void *out3);

%include "include/os.asm"
%define NO_AESNI_RENAME
%include "include/aesni_emu.inc"
%include "include/clear_regs.asm"

%ifdef LINUX
%define IN	rdi	; arg 1
%define KEYS	rsi	; arg 2
%define OUT0	rdx	; arg 3
%define OUT1	rcx	; arg 4
%define OUT2	r8	; arg 5
%else
%define IN	rcx	; arg 1
%define KEYS	rdx	; arg 2
%define OUT0	r8	; arg 3
%define OUT1	r9	; arg 4
%define OUT2	rax	;
%endif

%define XDATA0		xmm0
%define XDATA1		xmm1
%define XDATA2		xmm2

%define XKEYA		xmm3
%define XKEYB		xmm4

mksection .text

MKGLOBAL(aes128_ecbenc_x3_sse,function,internal)
aes128_ecbenc_x3_sse:

%ifndef LINUX
	mov		OUT2, [rsp + 5*8]
%endif

%ifdef SAFE_PARAM
        cmp             IN, 0
        jz              aes128_ecbenc_x3_sse_return
        cmp             KEYS, 0
        jz              aes128_ecbenc_x3_sse_return
        cmp             OUT0, 0
        jz              aes128_ecbenc_x3_sse_return
        cmp             OUT1, 0
        jz              aes128_ecbenc_x3_sse_return
        cmp             OUT2, 0
        jz              aes128_ecbenc_x3_sse_return
%endif

	movdqu		XDATA0, [IN + 0*16]	; load first block of plain text
	movdqu		XDATA1, [IN + 1*16]	; load second block of plain text
	movdqu		XDATA2, [IN + 2*16]	; load third block of plain text

	movdqa		XKEYA, [KEYS + 16*0]

	movdqa		XKEYB, [KEYS + 16*1]
	pxor		XDATA0, XKEYA	; 0. ARK
	pxor		XDATA1, XKEYA	; 0. ARK
	pxor		XDATA2, XKEYA	; 0. ARK

	movdqa		XKEYA, [KEYS + 16*2]
	aesenc		XDATA0, XKEYB	; 1. ENC
	aesenc		XDATA1, XKEYB	; 1. ENC
	aesenc		XDATA2, XKEYB	; 1. ENC

	movdqa		XKEYB, [KEYS + 16*3]
	aesenc		XDATA0, XKEYA	; 2. ENC
	aesenc		XDATA1, XKEYA	; 2. ENC
	aesenc		XDATA2, XKEYA	; 2. ENC

	movdqa		XKEYA, [KEYS + 16*4]
	aesenc		XDATA0, XKEYB	; 3. ENC
	aesenc		XDATA1, XKEYB	; 3. ENC
	aesenc		XDATA2, XKEYB	; 3. ENC

	movdqa		XKEYB, [KEYS + 16*5]
	aesenc		XDATA0, XKEYA	; 4. ENC
	aesenc		XDATA1, XKEYA	; 4. ENC
	aesenc		XDATA2, XKEYA	; 4. ENC

	movdqa		XKEYA, [KEYS + 16*6]
	aesenc		XDATA0, XKEYB	; 5. ENC
	aesenc		XDATA1, XKEYB	; 5. ENC
	aesenc		XDATA2, XKEYB	; 5. ENC

	movdqa		XKEYB, [KEYS + 16*7]
	aesenc		XDATA0, XKEYA	; 6. ENC
	aesenc		XDATA1, XKEYA	; 6. ENC
	aesenc		XDATA2, XKEYA	; 6. ENC

	movdqa		XKEYA, [KEYS + 16*8]
	aesenc		XDATA0, XKEYB	; 7. ENC
	aesenc		XDATA1, XKEYB	; 7. ENC
	aesenc		XDATA2, XKEYB	; 7. ENC

	movdqa		XKEYB, [KEYS + 16*9]
	aesenc		XDATA0, XKEYA	; 8. ENC
	aesenc		XDATA1, XKEYA	; 8. ENC
	aesenc		XDATA2, XKEYA	; 8. ENC

	movdqa		XKEYA, [KEYS + 16*10]
	aesenc		XDATA0, XKEYB	; 9. ENC
	aesenc		XDATA1, XKEYB	; 9. ENC
	aesenc		XDATA2, XKEYB	; 9. ENC

	aesenclast	XDATA0, XKEYA	; 10. ENC
	aesenclast	XDATA1, XKEYA	; 10. ENC
	aesenclast	XDATA2, XKEYA	; 10. ENC

	movdqu		[OUT0], XDATA0	; write back ciphertext
	movdqu		[OUT1], XDATA1	; write back ciphertext
	movdqu		[OUT2], XDATA2	; write back ciphertext

aes128_ecbenc_x3_sse_return:

%ifdef SAFE_DATA
        clear_scratch_gps_asm
        clear_scratch_xmms_sse_asm
%endif
	ret

MKGLOBAL(aes128_ecbenc_x3_sse_no_aesni,function,internal)
aes128_ecbenc_x3_sse_no_aesni:

%ifndef LINUX
	mov		OUT2, [rsp + 5*8]
%endif

%ifdef SAFE_PARAM
        cmp             IN, 0
        jz              aes128_ecbenc_x3_sse_no_aesni_return
        cmp             KEYS, 0
        jz              aes128_ecbenc_x3_sse_no_aesni_return
        cmp             OUT0, 0
        jz              aes128_ecbenc_x3_sse_no_aesni_return
        cmp             OUT1, 0
        jz              aes128_ecbenc_x3_sse_no_aesni_return
        cmp             OUT2, 0
        jz              aes128_ecbenc_x3_sse_no_aesni_return
%endif

	movdqu		XDATA0, [IN + 0*16]	; load first block of plain text
	movdqu		XDATA1, [IN + 1*16]	; load second block of plain text
	movdqu		XDATA2, [IN + 2*16]	; load third block of plain text

	movdqa		XKEYA, [KEYS + 16*0]

	movdqa		XKEYB, [KEYS + 16*1]
	pxor		XDATA0, XKEYA	; 0. ARK
	pxor		XDATA1, XKEYA	; 0. ARK
	pxor		XDATA2, XKEYA	; 0. ARK

	movdqa		XKEYA, [KEYS + 16*2]
	EMULATE_AESENC	XDATA0, XKEYB	; 1. ENC
	EMULATE_AESENC	XDATA1, XKEYB	; 1. ENC
	EMULATE_AESENC	XDATA2, XKEYB	; 1. ENC

	movdqa		XKEYB, [KEYS + 16*3]
	EMULATE_AESENC	XDATA0, XKEYA	; 2. ENC
	EMULATE_AESENC	XDATA1, XKEYA	; 2. ENC
	EMULATE_AESENC	XDATA2, XKEYA	; 2. ENC

	movdqa		XKEYA, [KEYS + 16*4]
	EMULATE_AESENC	XDATA0, XKEYB	; 3. ENC
	EMULATE_AESENC	XDATA1, XKEYB	; 3. ENC
	EMULATE_AESENC	XDATA2, XKEYB	; 3. ENC

	movdqa		XKEYB, [KEYS + 16*5]
	EMULATE_AESENC	XDATA0, XKEYA	; 4. ENC
	EMULATE_AESENC	XDATA1, XKEYA	; 4. ENC
	EMULATE_AESENC	XDATA2, XKEYA	; 4. ENC

	movdqa		XKEYA, [KEYS + 16*6]
	EMULATE_AESENC	XDATA0, XKEYB	; 5. ENC
	EMULATE_AESENC	XDATA1, XKEYB	; 5. ENC
	EMULATE_AESENC	XDATA2, XKEYB	; 5. ENC

	movdqa		XKEYB, [KEYS + 16*7]
	EMULATE_AESENC	XDATA0, XKEYA	; 6. ENC
	EMULATE_AESENC	XDATA1, XKEYA	; 6. ENC
	EMULATE_AESENC	XDATA2, XKEYA	; 6. ENC

	movdqa		XKEYA, [KEYS + 16*8]
	EMULATE_AESENC	XDATA0, XKEYB	; 7. ENC
	EMULATE_AESENC	XDATA1, XKEYB	; 7. ENC
	EMULATE_AESENC	XDATA2, XKEYB	; 7. ENC

	movdqa		XKEYB, [KEYS + 16*9]
	EMULATE_AESENC	XDATA0, XKEYA	; 8. ENC
	EMULATE_AESENC	XDATA1, XKEYA	; 8. ENC
	EMULATE_AESENC	XDATA2, XKEYA	; 8. ENC

	movdqa		XKEYA, [KEYS + 16*10]
	EMULATE_AESENC	XDATA0, XKEYB	; 9. ENC
	EMULATE_AESENC	XDATA1, XKEYB	; 9. ENC
	EMULATE_AESENC	XDATA2, XKEYB	; 9. ENC

	EMULATE_AESENCLAST XDATA0, XKEYA	; 10. ENC
	EMULATE_AESENCLAST XDATA1, XKEYA	; 10. ENC
	EMULATE_AESENCLAST XDATA2, XKEYA	; 10. ENC

	movdqu		[OUT0], XDATA0	; write back ciphertext
	movdqu		[OUT1], XDATA1	; write back ciphertext
	movdqu		[OUT2], XDATA2	; write back ciphertext

aes128_ecbenc_x3_sse_no_aesni_return:

%ifdef SAFE_DATA
        clear_scratch_gps_asm
        clear_scratch_xmms_sse_asm
%endif
	ret

	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

MKGLOBAL(aes128_ecbenc_x3_avx,function,internal)
aes128_ecbenc_x3_avx:

%ifndef LINUX
	mov		OUT2, [rsp + 5*8]
%endif

%ifdef SAFE_PARAM
        cmp             IN, 0
        jz              aes128_ecbenc_x3_avx_return
        cmp             KEYS, 0
        jz              aes128_ecbenc_x3_avx_return
        cmp             OUT0, 0
        jz              aes128_ecbenc_x3_avx_return
        cmp             OUT1, 0
        jz              aes128_ecbenc_x3_avx_return
        cmp             OUT2, 0
        jz              aes128_ecbenc_x3_avx_return
%endif

	vmovdqu		XDATA0, [IN + 0*16]	; load first block of plain text
	vmovdqu		XDATA1, [IN + 1*16]	; load second block of plain text
	vmovdqu		XDATA2, [IN + 2*16]	; load third block of plain text

	vmovdqa		XKEYA, [KEYS + 16*0]

	vmovdqa		XKEYB, [KEYS + 16*1]
	vpxor		XDATA0, XDATA0, XKEYA	; 0. ARK
	vpxor		XDATA1, XDATA1, XKEYA	; 0. ARK
	vpxor		XDATA2, XDATA2, XKEYA	; 0. ARK

	vmovdqa		XKEYA, [KEYS + 16*2]
	vaesenc		XDATA0, XKEYB	; 1. ENC
	vaesenc		XDATA1, XKEYB	; 1. ENC
	vaesenc		XDATA2, XKEYB	; 1. ENC

	vmovdqa		XKEYB, [KEYS + 16*3]
	vaesenc		XDATA0, XKEYA	; 2. ENC
	vaesenc		XDATA1, XKEYA	; 2. ENC
	vaesenc		XDATA2, XKEYA	; 2. ENC

	vmovdqa		XKEYA, [KEYS + 16*4]
	vaesenc		XDATA0, XKEYB	; 3. ENC
	vaesenc		XDATA1, XKEYB	; 3. ENC
	vaesenc		XDATA2, XKEYB	; 3. ENC

	vmovdqa		XKEYB, [KEYS + 16*5]
	vaesenc		XDATA0, XKEYA	; 4. ENC
	vaesenc		XDATA1, XKEYA	; 4. ENC
	vaesenc		XDATA2, XKEYA	; 4. ENC

	vmovdqa		XKEYA, [KEYS + 16*6]
	vaesenc		XDATA0, XKEYB	; 5. ENC
	vaesenc		XDATA1, XKEYB	; 5. ENC
	vaesenc		XDATA2, XKEYB	; 5. ENC

	vmovdqa		XKEYB, [KEYS + 16*7]
	vaesenc		XDATA0, XKEYA	; 6. ENC
	vaesenc		XDATA1, XKEYA	; 6. ENC
	vaesenc		XDATA2, XKEYA	; 6. ENC

	vmovdqa		XKEYA, [KEYS + 16*8]
	vaesenc		XDATA0, XKEYB	; 7. ENC
	vaesenc		XDATA1, XKEYB	; 7. ENC
	vaesenc		XDATA2, XKEYB	; 7. ENC

	vmovdqa		XKEYB, [KEYS + 16*9]
	vaesenc		XDATA0, XKEYA	; 8. ENC
	vaesenc		XDATA1, XKEYA	; 8. ENC
	vaesenc		XDATA2, XKEYA	; 8. ENC

	vmovdqa		XKEYA, [KEYS + 16*10]
	vaesenc		XDATA0, XKEYB	; 9. ENC
	vaesenc		XDATA1, XKEYB	; 9. ENC
	vaesenc		XDATA2, XKEYB	; 9. ENC

	vaesenclast	XDATA0, XKEYA	; 10. ENC
	vaesenclast	XDATA1, XKEYA	; 10. ENC
	vaesenclast	XDATA2, XKEYA	; 10. ENC

	vmovdqu		[OUT0], XDATA0	; write back ciphertext
	vmovdqu		[OUT1], XDATA1	; write back ciphertext
	vmovdqu		[OUT2], XDATA2	; write back ciphertext

aes128_ecbenc_x3_avx_return:

%ifdef SAFE_DATA
        clear_scratch_gps_asm
        clear_scratch_xmms_avx_asm
%endif
	ret

mksection stack-noexec

