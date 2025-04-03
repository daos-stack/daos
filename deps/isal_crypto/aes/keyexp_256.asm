;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;  Copyright(c) 2011-2016 Intel Corporation All rights reserved.
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


; Routine to do AES key expansion

%include "reg_sizes.asm"

[bits 64]
default rel
section .text

; Uses the f() function of the aeskeygenassist result
%macro key_expansion_256_sse 0
	;; Assumes the xmm3 includes all zeros at this point.
        pshufd	xmm2, xmm2, 11111111b
        shufps	xmm3, xmm1, 00010000b
        pxor	xmm1, xmm3
        shufps	xmm3, xmm1, 10001100b
        pxor	xmm1, xmm3
	pxor	xmm1, xmm2
%endmacro

; Uses the SubWord function of the aeskeygenassist result
%macro key_expansion_256_sse_2 0
	;; Assumes the xmm3 includes all zeros at this point.
        pshufd	xmm2, xmm2, 10101010b
        shufps	xmm3, xmm4, 00010000b
        pxor	xmm4, xmm3
        shufps	xmm3, xmm4, 10001100b
        pxor	xmm4, xmm3
	pxor	xmm4, xmm2
%endmacro

; Uses the f() function of the aeskeygenassist result
%macro key_expansion_256_avx 0
	;; Assumes the xmm3 includes all zeros at this point.
        vpshufd	xmm2, xmm2, 11111111b
        vshufps	xmm3, xmm3, xmm1, 00010000b
        vpxor	xmm1, xmm1, xmm3
        vshufps	xmm3, xmm3, xmm1, 10001100b
        vpxor	xmm1, xmm1, xmm3
	vpxor	xmm1, xmm1, xmm2
%endmacro

; Uses the SubWord function of the aeskeygenassist result
%macro key_expansion_256_avx_2 0
	;; Assumes the xmm3 includes all zeros at this point.
        vpshufd	xmm2, xmm2, 10101010b
        vshufps	xmm3, xmm3, xmm4, 00010000b
        vpxor	xmm4, xmm4, xmm3
        vshufps	xmm3, xmm3, xmm4, 10001100b
        vpxor	xmm4, xmm4, xmm3
	vpxor	xmm4, xmm4, xmm2
%endmacro

%ifidn __OUTPUT_FORMAT__, elf64
%define KEY		rdi
%define EXP_ENC_KEYS	rsi
%define EXP_DEC_KEYS	rdx
%else
%define KEY		rcx
%define EXP_ENC_KEYS	rdx
%define EXP_DEC_KEYS	r8
%endif

; void aes_keyexp_256(UINT8 *key,
;                     UINT8 *enc_exp_keys,
;                     UINT8 *dec_exp_keys);
;
; arg 1: rcx: pointer to key
; arg 2: rdx: pointer to expanded key array for encrypt
; arg 3: r8:  pointer to expanded key array for decrypt
;
mk_global aes_keyexp_256_sse, function
aes_keyexp_256_sse:
	endbranch
        movdqu	xmm1, [KEY]			; loading the AES key
	movdqu	[EXP_ENC_KEYS + 16*0], xmm1
        movdqu	[EXP_DEC_KEYS + 16*14], xmm1	; Storing key in memory

        movdqu	xmm4, [KEY+16]			; loading the AES key
	movdqu	[EXP_ENC_KEYS + 16*1], xmm4
        aesimc	xmm0, xmm4
        movdqu	[EXP_DEC_KEYS + 16*13], xmm0	; Storing key in memory

        pxor xmm3, xmm3				; Required for the key_expansion.

        aeskeygenassist xmm2, xmm4, 0x1		; Generating round key 2
        key_expansion_256_sse
	movdqu	[EXP_ENC_KEYS + 16*2], xmm1
	aesimc	xmm5, xmm1
	movdqu	[EXP_DEC_KEYS + 16*12], xmm5

        aeskeygenassist xmm2, xmm1, 0x1		; Generating round key 3
        key_expansion_256_sse_2
	movdqu	[EXP_ENC_KEYS + 16*3], xmm4
        aesimc	xmm0, xmm4
	movdqu	[EXP_DEC_KEYS + 16*11], xmm0

        aeskeygenassist xmm2, xmm4, 0x2		; Generating round key 4
        key_expansion_256_sse
	movdqu	[EXP_ENC_KEYS + 16*4], xmm1
        aesimc	xmm5, xmm1
	movdqu	[EXP_DEC_KEYS + 16*10], xmm5

        aeskeygenassist xmm2, xmm1, 0x2		; Generating round key 5
        key_expansion_256_sse_2
	movdqu	[EXP_ENC_KEYS + 16*5], xmm4
        aesimc	xmm0, xmm4
	movdqu	[EXP_DEC_KEYS + 16*9], xmm0

        aeskeygenassist xmm2, xmm4, 0x4		; Generating round key 6
        key_expansion_256_sse
	movdqu	[EXP_ENC_KEYS + 16*6], xmm1
        aesimc	xmm5, xmm1
	movdqu	[EXP_DEC_KEYS + 16*8], xmm5

        aeskeygenassist xmm2, xmm1, 0x4		; Generating round key 7
        key_expansion_256_sse_2
	movdqu	[EXP_ENC_KEYS + 16*7], xmm4
        aesimc xmm0, xmm4
	movdqu	[EXP_DEC_KEYS + 16*7], xmm0

        aeskeygenassist xmm2, xmm4, 0x8		; Generating round key 8
        key_expansion_256_sse
	movdqu	[EXP_ENC_KEYS + 16*8], xmm1
        aesimc	xmm5, xmm1
	movdqu	[EXP_DEC_KEYS + 16*6], xmm5

        aeskeygenassist xmm2, xmm1, 0x8		; Generating round key 9
        key_expansion_256_sse_2
	movdqu	[EXP_ENC_KEYS + 16*9], xmm4
        aesimc	xmm0, xmm4
	movdqu	[EXP_DEC_KEYS + 16*5], xmm0

        aeskeygenassist xmm2, xmm4, 0x10	; Generating round key 10
        key_expansion_256_sse
	movdqu	[EXP_ENC_KEYS + 16*10], xmm1
        aesimc	xmm5, xmm1
	movdqu	[EXP_DEC_KEYS + 16*4], xmm5

        aeskeygenassist xmm2, xmm1, 0x10	; Generating round key 11
        key_expansion_256_sse_2
	movdqu	[EXP_ENC_KEYS + 16*11], xmm4
        aesimc	xmm0, xmm4
	movdqu	[EXP_DEC_KEYS + 16*3], xmm0

        aeskeygenassist xmm2, xmm4, 0x20	; Generating round key 12
        key_expansion_256_sse
	movdqu	[EXP_ENC_KEYS + 16*12], xmm1
        aesimc	xmm5, xmm1
	movdqu	[EXP_DEC_KEYS + 16*2], xmm5

        aeskeygenassist xmm2, xmm1, 0x20	; Generating round key 13
        key_expansion_256_sse_2
	movdqu	[EXP_ENC_KEYS + 16*13], xmm4
        aesimc	xmm0, xmm4
	movdqu	[EXP_DEC_KEYS + 16*1], xmm0

        aeskeygenassist xmm2, xmm4, 0x40	; Generating round key 14
        key_expansion_256_sse
	movdqu	[EXP_ENC_KEYS + 16*14], xmm1
	movdqu	[EXP_DEC_KEYS + 16*0], xmm1

	ret


mk_global aes_keyexp_256_avx, function
aes_keyexp_256_avx:
	endbranch
        vmovdqu	xmm1, [KEY]			; loading the AES key
	vmovdqu	[EXP_ENC_KEYS + 16*0], xmm1
        vmovdqu	[EXP_DEC_KEYS + 16*14], xmm1	; Storing key in memory

        vmovdqu	xmm4, [KEY+16]			; loading the AES key
	vmovdqu	[EXP_ENC_KEYS + 16*1], xmm4
        vaesimc	xmm0, xmm4
        vmovdqu	[EXP_DEC_KEYS + 16*13], xmm0	; Storing key in memory

        vpxor xmm3, xmm3, xmm3			; Required for the key_expansion.

        vaeskeygenassist xmm2, xmm4, 0x1		; Generating round key 2
        key_expansion_256_avx
	vmovdqu	[EXP_ENC_KEYS + 16*2], xmm1
	vaesimc	xmm5, xmm1
	vmovdqu	[EXP_DEC_KEYS + 16*12], xmm5

        vaeskeygenassist xmm2, xmm1, 0x1		; Generating round key 3
        key_expansion_256_avx_2
	vmovdqu	[EXP_ENC_KEYS + 16*3], xmm4
        vaesimc	xmm0, xmm4
	vmovdqu	[EXP_DEC_KEYS + 16*11], xmm0

        vaeskeygenassist xmm2, xmm4, 0x2		; Generating round key 4
        key_expansion_256_avx
	vmovdqu	[EXP_ENC_KEYS + 16*4], xmm1
        vaesimc	xmm5, xmm1
	vmovdqu	[EXP_DEC_KEYS + 16*10], xmm5

        vaeskeygenassist xmm2, xmm1, 0x2		; Generating round key 5
        key_expansion_256_avx_2
	vmovdqu	[EXP_ENC_KEYS + 16*5], xmm4
        vaesimc	xmm0, xmm4
	vmovdqu	[EXP_DEC_KEYS + 16*9], xmm0

        vaeskeygenassist xmm2, xmm4, 0x4		; Generating round key 6
        key_expansion_256_avx
	vmovdqu	[EXP_ENC_KEYS + 16*6], xmm1
        vaesimc	xmm5, xmm1
	vmovdqu	[EXP_DEC_KEYS + 16*8], xmm5

        vaeskeygenassist xmm2, xmm1, 0x4		; Generating round key 7
        key_expansion_256_avx_2
	vmovdqu	[EXP_ENC_KEYS + 16*7], xmm4
        vaesimc xmm0, xmm4
	vmovdqu	[EXP_DEC_KEYS + 16*7], xmm0

        vaeskeygenassist xmm2, xmm4, 0x8		; Generating round key 8
        key_expansion_256_avx
	vmovdqu	[EXP_ENC_KEYS + 16*8], xmm1
        vaesimc	xmm5, xmm1
	vmovdqu	[EXP_DEC_KEYS + 16*6], xmm5

        vaeskeygenassist xmm2, xmm1, 0x8		; Generating round key 9
        key_expansion_256_avx_2
	vmovdqu	[EXP_ENC_KEYS + 16*9], xmm4
        vaesimc	xmm0, xmm4
	vmovdqu	[EXP_DEC_KEYS + 16*5], xmm0

        vaeskeygenassist xmm2, xmm4, 0x10	; Generating round key 10
        key_expansion_256_avx
	vmovdqu	[EXP_ENC_KEYS + 16*10], xmm1
        vaesimc	xmm5, xmm1
	vmovdqu	[EXP_DEC_KEYS + 16*4], xmm5

        vaeskeygenassist xmm2, xmm1, 0x10	; Generating round key 11
        key_expansion_256_avx_2
	vmovdqu	[EXP_ENC_KEYS + 16*11], xmm4
        vaesimc	xmm0, xmm4
	vmovdqu	[EXP_DEC_KEYS + 16*3], xmm0

        vaeskeygenassist xmm2, xmm4, 0x20	; Generating round key 12
        key_expansion_256_avx
	vmovdqu	[EXP_ENC_KEYS + 16*12], xmm1
        vaesimc	xmm5, xmm1
	vmovdqu	[EXP_DEC_KEYS + 16*2], xmm5

        vaeskeygenassist xmm2, xmm1, 0x20	; Generating round key 13
        key_expansion_256_avx_2
	vmovdqu	[EXP_ENC_KEYS + 16*13], xmm4
        vaesimc	xmm0, xmm4
	vmovdqu	[EXP_DEC_KEYS + 16*1], xmm0

        vaeskeygenassist xmm2, xmm4, 0x40	; Generating round key 14
        key_expansion_256_avx
	vmovdqu	[EXP_ENC_KEYS + 16*14], xmm1
	vmovdqu	[EXP_DEC_KEYS + 16*0], xmm1

	ret
