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

;;; routine to do a 256 bit CBC AES encrypt
;;; Updates In and Out pointers at end

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; Updates In and Out pointers at end
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;void aes_cbc_enc_256_x4(void      *in,
;;                        uint8_t   *IV,
;;                        uint8_t    keys,
;;                        void      *out,
;;                        uint64_t   len_bytes);
; arg 1: IN:   pointer to input (cipher text)
; arg 2: IV:   pointer to IV
; arg 3: KEYS: pointer to keys
; arg 4: OUT:  pointer to output (plain text)
; arg 5: LEN:  length in bytes (multiple of 16)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

%include "reg_sizes.asm"

%ifidn __OUTPUT_FORMAT__, elf64
%define IN0		rdi
%define IN		rdi
%define IV		rsi
%define KEYS	rdx
%define OUT 	rcx
%define LEN		r8
%define KEYS0	rdx
%define OUT0	rcx
%define func(x) x:
%define FUNC_SAVE
%define FUNC_RESTORE
%endif

%ifidn __OUTPUT_FORMAT__, win64
%define IN0		rcx
%define IN		rcx
%define IV		rdx
%define KEYS0	r8
%define OUT0	r9
%define KEYS	r8
%define OUT		r9
%define LEN		r10
%define PS		8
%define stack_size	10*16 + 1*8	; must be an odd multiple of 8
%define arg(x)		[rsp + stack_size + PS + PS*x]

%define func(x) proc_frame x
%macro FUNC_SAVE 0
	alloc_stack	stack_size
	save_xmm128	xmm6, 0*16
	save_xmm128	xmm7, 1*16
	save_xmm128	xmm8, 2*16
	save_xmm128	xmm9, 3*16
	save_xmm128	xmm10, 4*16
	save_xmm128	xmm11, 5*16
	save_xmm128	xmm12, 6*16
	save_xmm128	xmm13, 7*16
	save_xmm128	xmm14, 8*16
	save_xmm128	xmm15, 9*16
	end_prolog
	mov	LEN, arg(4)
%endmacro

%macro FUNC_RESTORE 0
	movdqa	xmm6, [rsp + 0*16]
	movdqa	xmm7, [rsp + 1*16]
	movdqa	xmm8, [rsp + 2*16]
	movdqa	xmm9, [rsp + 3*16]
	movdqa	xmm10, [rsp + 4*16]
	movdqa	xmm11, [rsp + 5*16]
	movdqa	xmm12, [rsp + 6*16]
	movdqa	xmm13, [rsp + 7*16]
	movdqa	xmm14, [rsp + 8*16]
	movdqa	xmm15, [rsp + 9*16]
	add	rsp, stack_size
%endmacro
%endif

%define KEY_ROUNDS 15
%define XMM_USAGE    (16)
%DEFINE UNROLLED_LOOPS (3)
%define PARALLEL_BLOCKS (UNROLLED_LOOPS)

; instruction set specific operation definitions
%define MOVDQ         movdqu
%define PXOR          pxor
%define AES_ENC       aesenc
%define AES_ENC_LAST  aesenclast

%include "cbc_common.asm"


mk_global aes_cbc_enc_256_x4, function
func(aes_cbc_enc_256_x4)
	endbranch
	FUNC_SAVE

	mov	IDX, 0

	FILL_KEY_CACHE	 CKEY_CNT, FIRST_CKEY, KEYS, MOVDQ
	CBC_ENC_INIT	 FIRST_XDATA, TMP, MOVDQ, PXOR, IV, IN, IDX

main_loop:
	CBC_ENC_SUBLOOP	 KEY_ROUNDS, UNROLLED_LOOPS, FIRST_XDATA, MOVDQ, PXOR, AES_ENC, AES_ENC_LAST, TMP, TMP_CNT,	FIRST_CKEY, CKEY_CNT, KEYS, CACHED_KEYS, IN, OUT, IDX, LEN
	jne	main_loop

done:
	FUNC_RESTORE
	ret

endproc_frame
