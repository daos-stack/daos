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

; routine to do AES cbc decrypt on 16n bytes doing AES
; XMM registers are clobbered. Saving/restoring must be done at a higher level

; void aes_cbc_dec_192_sse(void    *in,
;                          uint8_t *IV,
;                          uint8_t  keys[13], // +1 over key length
;                          void    *out,
;                          uint64_t len_bytes);
;
; arg 1: IN:   pointer to input (cipher text)
; arg 2: IV:   pointer to IV
; arg 3: KEYS: pointer to keys
; arg 4: OUT:  pointer to output (plain text)
; arg 5: LEN:  length in bytes (multiple of 16)
;

%include "reg_sizes.asm"

%define MOVDQ	movdqu

%ifidn __OUTPUT_FORMAT__, elf64
%define IN		rdi
%define IV		rsi
%define KEYS		rdx
%define OUT		rcx
%define LEN		r8
%define func(x) x:
%define FUNC_SAVE
%define FUNC_RESTORE
%endif

%ifidn __OUTPUT_FORMAT__, win64
%define IN		rcx
%define IV		rdx
%define KEYS		r8
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

; configuration paramaters for AES-CBC
%define KEY_ROUNDS 13
%define XMM_USAGE    (16)
%define EARLY_BLOCKS (2)
%define PARALLEL_BLOCKS (5)
%define IV_CNT          (1)

; instruction set specific operation definitions
%define MOVDQ         movdqu
%define PXOR          pxor
%define AES_DEC       aesdec
%define AES_DEC_LAST  aesdeclast

%include "cbc_common.asm"

section .text

mk_global aes_cbc_dec_192_sse, function
func(aes_cbc_dec_192_sse)
	endbranch
	FUNC_SAVE

        FILL_KEY_CACHE CKEY_CNT, FIRST_CKEY, KEYS, MOVDQ

        MOVDQ  reg(IV_IDX), [IV]         ; Load IV for next round of block decrypt
        mov IDX, 0
        cmp     LEN, PARALLEL_BLOCKS*16
        jge     main_loop                ; if enough data blocks remain enter main_loop
        jmp  partials

main_loop:
        CBC_DECRYPT_BLOCKS KEY_ROUNDS, PARALLEL_BLOCKS, EARLY_BLOCKS, MOVDQ, PXOR, AES_DEC, AES_DEC_LAST, CKEY_CNT, TMP, TMP_CNT, FIRST_CKEY, KEYS, FIRST_XDATA, IN, OUT, IDX, LEN
	cmp	LEN, PARALLEL_BLOCKS*16
	jge	main_loop                ; enough blocks to do another full parallel set
	jz  done

partials:                                ; fewer than 'PARALLEL_BLOCKS' left do in groups of 4, 2 or 1
	cmp	LEN, 0
	je	done
	cmp	LEN, 4*16
	jge	initial_4
	cmp	LEN, 2*16
	jge	initial_2

initial_1:
        CBC_DECRYPT_BLOCKS KEY_ROUNDS, 1, EARLY_BLOCKS, MOVDQ, PXOR, AES_DEC, AES_DEC_LAST, CKEY_CNT, TMP, TMP_CNT, FIRST_CKEY, KEYS, FIRST_XDATA, IN, OUT, IDX, LEN
	jmp	done

initial_2:
        CBC_DECRYPT_BLOCKS KEY_ROUNDS, 2, EARLY_BLOCKS, MOVDQ, PXOR, AES_DEC, AES_DEC_LAST, CKEY_CNT, TMP, TMP_CNT, FIRST_CKEY, KEYS, FIRST_XDATA, IN, OUT, IDX, LEN
	jz  done
	jmp	partials

initial_4:
        CBC_DECRYPT_BLOCKS KEY_ROUNDS, 4, EARLY_BLOCKS, MOVDQ, PXOR, AES_DEC, AES_DEC_LAST, CKEY_CNT, TMP, TMP_CNT, FIRST_CKEY, KEYS, FIRST_XDATA, IN, OUT, IDX, LEN
	jnz	partials
done:
	FUNC_RESTORE
	ret

endproc_frame
