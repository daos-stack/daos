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

;;; routine to do a 256 bit CBC AES encrypt

;; clobbers all registers except for ARG1 and rbp

%include "include/os.asm"
%include "include/mb_mgr_datastruct.asm"
%include "include/clear_regs.asm"

%macro PXOR2 2
	movdqu	XTMP, %2
	pxor	%1, XTMP
%endm

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; struct AES_ARGS {
;;     void*    in[8];
;;     void*    out[8];
;;     UINT128* keys[8];
;;     UINT128  IV[8];
;; }
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; void aes_cbc_enc_256_x8(AES_ARGS *args, UINT64 len);
;; arg 1: ARG : addr of AES_ARGS structure
;; arg 2: LEN : len (in units of bytes)

struc STACK
_gpr_save:	resq	8
_len:		resq	1
endstruc

%define GPR_SAVE_AREA	rsp + _gpr_save
%define LEN_AREA	rsp + _len

%ifdef LINUX
%define ARG	rdi
%define LEN	rsi
%define REG3	rcx
%define REG4	rdx
%else
%define ARG	rcx
%define LEN	rdx
%define REG3	rsi
%define REG4	rdi
%endif

%define IDX	rax
%define TMP	rbx

%define KEYS0	REG3
%define KEYS1	REG4
%define KEYS2	rbp
%define KEYS3	r8
%define KEYS4	r9
%define KEYS5	r10
%define KEYS6	r11
%define KEYS7	r12

%define IN0	r13
%define IN2	r14
%define IN4	r15
%define IN6	LEN

%define XDATA0		xmm0
%define XDATA1		xmm1
%define XDATA2		xmm2
%define XDATA3		xmm3
%define XDATA4		xmm4
%define XDATA5		xmm5
%define XDATA6		xmm6
%define XDATA7		xmm7

%define XKEY0_3		xmm8
%define XKEY1_4		xmm9
%define XKEY2_5		xmm10
%define XKEY3_6		xmm11
%define XKEY4_7		xmm12
%define XKEY5_8		xmm13
%define XKEY6_9		xmm14
%define XTMP		xmm15

mksection .text

align 32

%ifdef CBC_MAC
MKGLOBAL(aes256_cbc_mac_x8_sse,function,internal)
aes256_cbc_mac_x8_sse:
%else
MKGLOBAL(aes_cbc_enc_256_x8_sse,function,internal)
aes_cbc_enc_256_x8_sse:
%endif
	sub	rsp, STACK_size
	mov	[GPR_SAVE_AREA + 8*0], rbp
%ifdef CBC_MAC
	mov	[GPR_SAVE_AREA + 8*1], rbx
	mov	[GPR_SAVE_AREA + 8*2], r12
	mov	[GPR_SAVE_AREA + 8*3], r13
	mov	[GPR_SAVE_AREA + 8*4], r14
	mov	[GPR_SAVE_AREA + 8*5], r15
%ifndef LINUX
	mov	[GPR_SAVE_AREA + 8*6], rsi
	mov	[GPR_SAVE_AREA + 8*7], rdi
%endif
%endif

	mov	IDX, 16
	mov	[LEN_AREA], LEN

	mov	IN0,	[ARG + _aesarg_in + 8*0]
	mov	IN2,	[ARG + _aesarg_in + 8*2]
	mov	IN4,	[ARG + _aesarg_in + 8*4]
	mov	IN6,	[ARG + _aesarg_in + 8*6]

	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

	mov		TMP, [ARG + _aesarg_in + 8*1]
	movdqu		XDATA0, [IN0]		; load first block of plain text
	movdqu		XDATA1, [TMP]		; load first block of plain text
	mov		TMP, [ARG + _aesarg_in + 8*3]
	movdqu		XDATA2, [IN2]		; load first block of plain text
	movdqu		XDATA3, [TMP]		; load first block of plain text
	mov		TMP, [ARG + _aesarg_in + 8*5]
	movdqu		XDATA4, [IN4]		; load first block of plain text
	movdqu		XDATA5, [TMP]		; load first block of plain text
	mov		TMP, [ARG + _aesarg_in + 8*7]
	movdqu		XDATA6, [IN6]		; load first block of plain text
	movdqu		XDATA7, [TMP]		; load first block of plain text

	pxor		XDATA0, [ARG + _aesarg_IV + 16*0]  ; plaintext XOR IV
	pxor		XDATA1, [ARG + _aesarg_IV + 16*1]  ; plaintext XOR IV
	pxor		XDATA2, [ARG + _aesarg_IV + 16*2]  ; plaintext XOR IV
	pxor		XDATA3, [ARG + _aesarg_IV + 16*3]  ; plaintext XOR IV
	pxor		XDATA4, [ARG + _aesarg_IV + 16*4]  ; plaintext XOR IV
	pxor		XDATA5, [ARG + _aesarg_IV + 16*5]  ; plaintext XOR IV
	pxor		XDATA6, [ARG + _aesarg_IV + 16*6]  ; plaintext XOR IV
	pxor		XDATA7, [ARG + _aesarg_IV + 16*7]  ; plaintext XOR IV

	mov		KEYS0,	[ARG + _aesarg_keys + 8*0]
	mov		KEYS1,	[ARG + _aesarg_keys + 8*1]
	mov		KEYS2,	[ARG + _aesarg_keys + 8*2]
	mov		KEYS3,	[ARG + _aesarg_keys + 8*3]
	mov		KEYS4,	[ARG + _aesarg_keys + 8*4]
	mov		KEYS5,	[ARG + _aesarg_keys + 8*5]
	mov		KEYS6,	[ARG + _aesarg_keys + 8*6]
	mov		KEYS7,	[ARG + _aesarg_keys + 8*7]

	pxor		XDATA0, [KEYS0 + 16*0]	; 0. ARK
	pxor		XDATA1, [KEYS1 + 16*0]	; 0. ARK
	pxor		XDATA2, [KEYS2 + 16*0]	; 0. ARK
	pxor		XDATA3, [KEYS3 + 16*0]	; 0. ARK
	pxor		XDATA4, [KEYS4 + 16*0]	; 0. ARK
	pxor		XDATA5, [KEYS5 + 16*0]	; 0. ARK
	pxor		XDATA6, [KEYS6 + 16*0]	; 0. ARK
	pxor		XDATA7, [KEYS7 + 16*0]	; 0. ARK

	aesenc		XDATA0, [KEYS0 + 16*1]	; 1. ENC
	aesenc		XDATA1, [KEYS1 + 16*1]	; 1. ENC
	aesenc		XDATA2, [KEYS2 + 16*1]	; 1. ENC
	aesenc		XDATA3, [KEYS3 + 16*1]	; 1. ENC
	aesenc		XDATA4, [KEYS4 + 16*1]	; 1. ENC
	aesenc		XDATA5, [KEYS5 + 16*1]	; 1. ENC
	aesenc		XDATA6, [KEYS6 + 16*1]	; 1. ENC
	aesenc		XDATA7, [KEYS7 + 16*1]	; 1. ENC

	movdqa		XKEY0_3, [KEYS0 + 16*3]	; load round 3 key

	aesenc		XDATA0, [KEYS0 + 16*2]	; 2. ENC
	aesenc		XDATA1, [KEYS1 + 16*2]	; 2. ENC
	aesenc		XDATA2, [KEYS2 + 16*2]	; 2. ENC
	aesenc		XDATA3, [KEYS3 + 16*2]	; 2. ENC
	aesenc		XDATA4, [KEYS4 + 16*2]	; 2. ENC
	aesenc		XDATA5, [KEYS5 + 16*2]	; 2. ENC
	aesenc		XDATA6, [KEYS6 + 16*2]	; 2. ENC
	aesenc		XDATA7, [KEYS7 + 16*2]	; 2. ENC

	movdqa		XKEY1_4, [KEYS1 + 16*4]	; load round 4 key

	aesenc		XDATA0, XKEY0_3       	; 3. ENC
	aesenc		XDATA1, [KEYS1 + 16*3]	; 3. ENC
	aesenc		XDATA2, [KEYS2 + 16*3]	; 3. ENC
	aesenc		XDATA3, [KEYS3 + 16*3]	; 3. ENC
	aesenc		XDATA4, [KEYS4 + 16*3]	; 3. ENC
	aesenc		XDATA5, [KEYS5 + 16*3]	; 3. ENC
	aesenc		XDATA6, [KEYS6 + 16*3]	; 3. ENC
	aesenc		XDATA7, [KEYS7 + 16*3]	; 3. ENC

	aesenc		XDATA0, [KEYS0 + 16*4]	; 4. ENC
	movdqa		XKEY2_5, [KEYS2 + 16*5]	; load round 5 key
	aesenc		XDATA1, XKEY1_4       	; 4. ENC
	aesenc		XDATA2, [KEYS2 + 16*4]	; 4. ENC
	aesenc		XDATA3, [KEYS3 + 16*4]	; 4. ENC
	aesenc		XDATA4, [KEYS4 + 16*4]	; 4. ENC
	aesenc		XDATA5, [KEYS5 + 16*4]	; 4. ENC
	aesenc		XDATA6, [KEYS6 + 16*4]	; 4. ENC
	aesenc		XDATA7, [KEYS7 + 16*4]	; 4. ENC

	aesenc		XDATA0, [KEYS0 + 16*5]	; 5. ENC
	aesenc		XDATA1, [KEYS1 + 16*5]	; 5. ENC
	movdqa		XKEY3_6, [KEYS3 + 16*6]	; load round 6 key
	aesenc		XDATA2, XKEY2_5       	; 5. ENC
	aesenc		XDATA3, [KEYS3 + 16*5]	; 5. ENC
	aesenc		XDATA4, [KEYS4 + 16*5]	; 5. ENC
	aesenc		XDATA5, [KEYS5 + 16*5]	; 5. ENC
	aesenc		XDATA6, [KEYS6 + 16*5]	; 5. ENC
	aesenc		XDATA7, [KEYS7 + 16*5]	; 5. ENC

	aesenc		XDATA0, [KEYS0 + 16*6]	; 6. ENC
	aesenc		XDATA1, [KEYS1 + 16*6]	; 6. ENC
	aesenc		XDATA2, [KEYS2 + 16*6]	; 6. ENC
	movdqa		XKEY4_7, [KEYS4 + 16*7]	; load round 7 key
	aesenc		XDATA3, XKEY3_6       	; 6. ENC
	aesenc		XDATA4, [KEYS4 + 16*6]	; 6. ENC
	aesenc		XDATA5, [KEYS5 + 16*6]	; 6. ENC
	aesenc		XDATA6, [KEYS6 + 16*6]	; 6. ENC
	aesenc		XDATA7, [KEYS7 + 16*6]	; 6. ENC

	aesenc		XDATA0, [KEYS0 + 16*7]	; 7. ENC
	aesenc		XDATA1, [KEYS1 + 16*7]	; 7. ENC
	aesenc		XDATA2, [KEYS2 + 16*7]	; 7. ENC
	aesenc		XDATA3, [KEYS3 + 16*7]	; 7. ENC
	movdqa		XKEY5_8, [KEYS5 + 16*8]	; load round 8 key
	aesenc		XDATA4, XKEY4_7       	; 7. ENC
	aesenc		XDATA5, [KEYS5 + 16*7]	; 7. ENC
	aesenc		XDATA6, [KEYS6 + 16*7]	; 7. ENC
	aesenc		XDATA7, [KEYS7 + 16*7]	; 7. ENC

	aesenc		XDATA0, [KEYS0 + 16*8]	; 8. ENC
	aesenc		XDATA1, [KEYS1 + 16*8]	; 8. ENC
	aesenc		XDATA2, [KEYS2 + 16*8]	; 8. ENC
	aesenc		XDATA3, [KEYS3 + 16*8]	; 8. ENC
	aesenc		XDATA4, [KEYS4 + 16*8]	; 8. ENC
	movdqa		XKEY6_9, [KEYS6 + 16*9]	; load round 9 key
	aesenc		XDATA5, XKEY5_8       	; 8. ENC
	aesenc		XDATA6, [KEYS6 + 16*8]	; 8. ENC
	aesenc		XDATA7, [KEYS7 + 16*8]	; 8. ENC

	aesenc		XDATA0, [KEYS0 + 16*9]	; 9. ENC
	aesenc		XDATA1, [KEYS1 + 16*9]	; 9. ENC
	aesenc		XDATA2, [KEYS2 + 16*9]	; 9. ENC
	aesenc		XDATA3, [KEYS3 + 16*9]	; 9. ENC
	aesenc		XDATA4, [KEYS4 + 16*9]	; 9. ENC
	aesenc		XDATA5, [KEYS5 + 16*9]	; 9. ENC
%ifndef CBC_MAC
	mov		TMP, [ARG + _aesarg_out + 8*0]
%endif
	aesenc		XDATA6, XKEY6_9       	; 9. ENC
	aesenc		XDATA7, [KEYS7 + 16*9]	; 9. ENC

	aesenc		XDATA0, [KEYS0 + 16*10]	; 10. ENC
	aesenc		XDATA1, [KEYS1 + 16*10]	; 10. ENC
	aesenc		XDATA2, [KEYS2 + 16*10]	; 10. ENC
	aesenc		XDATA3, [KEYS3 + 16*10]	; 10. ENC
	aesenc		XDATA4, [KEYS4 + 16*10]	; 10. ENC
	aesenc		XDATA5, [KEYS5 + 16*10]	; 10. ENC
	aesenc		XDATA6, [KEYS6 + 16*10]	; 10. ENC
	aesenc		XDATA7, [KEYS7 + 16*10]	; 10. ENC

	aesenc		XDATA0, [KEYS0 + 16*11]	; 11. ENC
	aesenc		XDATA1, [KEYS1 + 16*11]	; 11. ENC
	aesenc		XDATA2, [KEYS2 + 16*11]	; 11. ENC
	aesenc		XDATA3, [KEYS3 + 16*11]	; 11. ENC
	aesenc		XDATA4, [KEYS4 + 16*11]	; 11. ENC
	aesenc		XDATA5, [KEYS5 + 16*11]	; 11. ENC
	aesenc		XDATA6, [KEYS6 + 16*11]	; 11. ENC
	aesenc		XDATA7, [KEYS7 + 16*11]	; 11. ENC

	aesenc		XDATA0, [KEYS0 + 16*12]	; 12. ENC
	aesenc		XDATA1, [KEYS1 + 16*12]	; 12. ENC
	aesenc		XDATA2, [KEYS2 + 16*12]	; 12. ENC
	aesenc		XDATA3, [KEYS3 + 16*12]	; 12. ENC
	aesenc		XDATA4, [KEYS4 + 16*12]	; 12. ENC
	aesenc		XDATA5, [KEYS5 + 16*12]	; 12. ENC
	aesenc		XDATA6, [KEYS6 + 16*12]	; 12. ENC
	aesenc		XDATA7, [KEYS7 + 16*12]	; 12. ENC

	aesenc		XDATA0, [KEYS0 + 16*13]	; 13. ENC
	aesenc		XDATA1, [KEYS1 + 16*13]	; 13. ENC
	aesenc		XDATA2, [KEYS2 + 16*13]	; 13. ENC
	aesenc		XDATA3, [KEYS3 + 16*13]	; 13. ENC
	aesenc		XDATA4, [KEYS4 + 16*13]	; 13. ENC
	aesenc		XDATA5, [KEYS5 + 16*13]	; 13. ENC
	aesenc		XDATA6, [KEYS6 + 16*13]	; 13. ENC
	aesenc		XDATA7, [KEYS7 + 16*13]	; 13. ENC

	aesenclast	XDATA0, [KEYS0 + 16*14]	; 14. ENC
	aesenclast	XDATA1, [KEYS1 + 16*14]	; 14. ENC
	aesenclast	XDATA2, [KEYS2 + 16*14]	; 14. ENC
	aesenclast	XDATA3, [KEYS3 + 16*14]	; 14. ENC
	aesenclast	XDATA4, [KEYS4 + 16*14]	; 14. ENC
	aesenclast	XDATA5, [KEYS5 + 16*14]	; 14. ENC
	aesenclast	XDATA6, [KEYS6 + 16*14]	; 14. ENC
	aesenclast	XDATA7, [KEYS7 + 16*14]	; 14. ENC

%ifndef CBC_MAC
	movdqu		[TMP], XDATA0		; write back ciphertext
	mov		TMP, [ARG + _aesarg_out + 8*1]
	movdqu		[TMP], XDATA1		; write back ciphertext
	mov		TMP, [ARG + _aesarg_out + 8*2]
	movdqu		[TMP], XDATA2		; write back ciphertext
	mov		TMP, [ARG + _aesarg_out + 8*3]
	movdqu		[TMP], XDATA3		; write back ciphertext
	mov		TMP, [ARG + _aesarg_out + 8*4]
	movdqu		[TMP], XDATA4		; write back ciphertext
	mov		TMP, [ARG + _aesarg_out + 8*5]
	movdqu		[TMP], XDATA5		; write back ciphertext
	mov		TMP, [ARG + _aesarg_out + 8*6]
	movdqu		[TMP], XDATA6		; write back ciphertext
	mov		TMP, [ARG + _aesarg_out + 8*7]
	movdqu		[TMP], XDATA7		; write back ciphertext
%endif
	cmp		[LEN_AREA], IDX
	je		done

main_loop:
	mov		TMP, [ARG + _aesarg_in + 8*1]
	PXOR2		XDATA0, [IN0 + IDX]	; load next block of plain text
	PXOR2		XDATA1, [TMP + IDX]	; load next block of plain text
	mov		TMP, [ARG + _aesarg_in + 8*3]
	PXOR2		XDATA2, [IN2 + IDX]	; load next block of plain text
	PXOR2		XDATA3, [TMP + IDX]	; load next block of plain text
	mov		TMP, [ARG + _aesarg_in + 8*5]
	PXOR2		XDATA4, [IN4 + IDX]	; load next block of plain text
	PXOR2		XDATA5, [TMP + IDX]	; load next block of plain text
	mov		TMP, [ARG + _aesarg_in + 8*7]
	PXOR2		XDATA6, [IN6 + IDX]	; load next block of plain text
	PXOR2		XDATA7, [TMP + IDX]	; load next block of plain text

	pxor		XDATA0, [KEYS0 + 16*0]		; 0. ARK
	pxor		XDATA1, [KEYS1 + 16*0]		; 0. ARK
	pxor		XDATA2, [KEYS2 + 16*0]		; 0. ARK
	pxor		XDATA3, [KEYS3 + 16*0]		; 0. ARK
	pxor		XDATA4, [KEYS4 + 16*0]		; 0. ARK
	pxor		XDATA5, [KEYS5 + 16*0]		; 0. ARK
	pxor		XDATA6, [KEYS6 + 16*0]		; 0. ARK
	pxor		XDATA7, [KEYS7 + 16*0]		; 0. ARK

	aesenc		XDATA0, [KEYS0 + 16*1]	; 1. ENC
	aesenc		XDATA1, [KEYS1 + 16*1]	; 1. ENC
	aesenc		XDATA2, [KEYS2 + 16*1]	; 1. ENC
	aesenc		XDATA3, [KEYS3 + 16*1]	; 1. ENC
	aesenc		XDATA4, [KEYS4 + 16*1]	; 1. ENC
	aesenc		XDATA5, [KEYS5 + 16*1]	; 1. ENC
	aesenc		XDATA6, [KEYS6 + 16*1]	; 1. ENC
	aesenc		XDATA7, [KEYS7 + 16*1]	; 1. ENC

	aesenc		XDATA0, [KEYS0 + 16*2]	; 2. ENC
	aesenc		XDATA1, [KEYS1 + 16*2]	; 2. ENC
	aesenc		XDATA2, [KEYS2 + 16*2]	; 2. ENC
	aesenc		XDATA3, [KEYS3 + 16*2]	; 2. ENC
	aesenc		XDATA4, [KEYS4 + 16*2]	; 2. ENC
	aesenc		XDATA5, [KEYS5 + 16*2]	; 2. ENC
	aesenc		XDATA6, [KEYS6 + 16*2]	; 2. ENC
	aesenc		XDATA7, [KEYS7 + 16*2]	; 2. ENC

	aesenc		XDATA0, XKEY0_3       	; 3. ENC
	aesenc		XDATA1, [KEYS1 + 16*3]	; 3. ENC
	aesenc		XDATA2, [KEYS2 + 16*3]	; 3. ENC
	aesenc		XDATA3, [KEYS3 + 16*3]	; 3. ENC
	aesenc		XDATA4, [KEYS4 + 16*3]	; 3. ENC
	aesenc		XDATA5, [KEYS5 + 16*3]	; 3. ENC
	aesenc		XDATA6, [KEYS6 + 16*3]	; 3. ENC
	aesenc		XDATA7, [KEYS7 + 16*3]	; 3. ENC

	aesenc		XDATA0, [KEYS0 + 16*4]	; 4. ENC
	aesenc		XDATA1, XKEY1_4       	; 4. ENC
	aesenc		XDATA2, [KEYS2 + 16*4]	; 4. ENC
	aesenc		XDATA3, [KEYS3 + 16*4]	; 4. ENC
	aesenc		XDATA4, [KEYS4 + 16*4]	; 4. ENC
	aesenc		XDATA5, [KEYS5 + 16*4]	; 4. ENC
	aesenc		XDATA6, [KEYS6 + 16*4]	; 4. ENC
	aesenc		XDATA7, [KEYS7 + 16*4]	; 4. ENC

	aesenc		XDATA0, [KEYS0 + 16*5]	; 5. ENC
	aesenc		XDATA1, [KEYS1 + 16*5]	; 5. ENC
	aesenc		XDATA2, XKEY2_5       	; 5. ENC
	aesenc		XDATA3, [KEYS3 + 16*5]	; 5. ENC
	aesenc		XDATA4, [KEYS4 + 16*5]	; 5. ENC
	aesenc		XDATA5, [KEYS5 + 16*5]	; 5. ENC
	aesenc		XDATA6, [KEYS6 + 16*5]	; 5. ENC
	aesenc		XDATA7, [KEYS7 + 16*5]	; 5. ENC

	aesenc		XDATA0, [KEYS0 + 16*6]	; 6. ENC
	aesenc		XDATA1, [KEYS1 + 16*6]	; 6. ENC
	aesenc		XDATA2, [KEYS2 + 16*6]	; 6. ENC
	aesenc		XDATA3, XKEY3_6       	; 6. ENC
	aesenc		XDATA4, [KEYS4 + 16*6]	; 6. ENC
	aesenc		XDATA5, [KEYS5 + 16*6]	; 6. ENC
	aesenc		XDATA6, [KEYS6 + 16*6]	; 6. ENC
	aesenc		XDATA7, [KEYS7 + 16*6]	; 6. ENC

	aesenc		XDATA0, [KEYS0 + 16*7]	; 7. ENC
	aesenc		XDATA1, [KEYS1 + 16*7]	; 7. ENC
	aesenc		XDATA2, [KEYS2 + 16*7]	; 7. ENC
	aesenc		XDATA3, [KEYS3 + 16*7]	; 7. ENC
	aesenc		XDATA4, XKEY4_7       	; 7. ENC
	aesenc		XDATA5, [KEYS5 + 16*7]	; 7. ENC
	aesenc		XDATA6, [KEYS6 + 16*7]	; 7. ENC
	aesenc		XDATA7, [KEYS7 + 16*7]	; 7. ENC

	aesenc		XDATA0, [KEYS0 + 16*8]	; 8. ENC
	aesenc		XDATA1, [KEYS1 + 16*8]	; 8. ENC
	aesenc		XDATA2, [KEYS2 + 16*8]	; 8. ENC
	aesenc		XDATA3, [KEYS3 + 16*8]	; 8. ENC
	aesenc		XDATA4, [KEYS4 + 16*8]	; 8. ENC
	aesenc		XDATA5, XKEY5_8       	; 8. ENC
	aesenc		XDATA6, [KEYS6 + 16*8]	; 8. ENC
	aesenc		XDATA7, [KEYS7 + 16*8]	; 8. ENC

	aesenc		XDATA0, [KEYS0 + 16*9]	; 9. ENC
	aesenc		XDATA1, [KEYS1 + 16*9]	; 9. ENC
	aesenc		XDATA2, [KEYS2 + 16*9]	; 9. ENC
	aesenc		XDATA3, [KEYS3 + 16*9]	; 9. ENC
	aesenc		XDATA4, [KEYS4 + 16*9]	; 9. ENC
	aesenc		XDATA5, [KEYS5 + 16*9]	; 9. ENC
%ifndef CBC_MAC
	mov		TMP, [ARG + _aesarg_out + 8*0]
%endif
	aesenc		XDATA6, XKEY6_9       	; 9. ENC
	aesenc		XDATA7, [KEYS7 + 16*9]	; 9. ENC

	aesenc		XDATA0, [KEYS0 + 16*10]	; 10. ENC
	aesenc		XDATA1, [KEYS1 + 16*10]	; 10. ENC
	aesenc		XDATA2, [KEYS2 + 16*10]	; 10. ENC
	aesenc		XDATA3, [KEYS3 + 16*10]	; 10. ENC
	aesenc		XDATA4, [KEYS4 + 16*10]	; 10. ENC
	aesenc		XDATA5, [KEYS5 + 16*10]	; 10. ENC
	aesenc		XDATA6, [KEYS6 + 16*10]	; 10. ENC
	aesenc		XDATA7, [KEYS7 + 16*10]	; 10. ENC

	aesenc		XDATA0, [KEYS0 + 16*11]	; 11. ENC
	aesenc		XDATA1, [KEYS1 + 16*11]	; 11. ENC
	aesenc		XDATA2, [KEYS2 + 16*11]	; 11. ENC
	aesenc		XDATA3, [KEYS3 + 16*11]	; 11. ENC
	aesenc		XDATA4, [KEYS4 + 16*11]	; 11. ENC
	aesenc		XDATA5, [KEYS5 + 16*11]	; 11. ENC
	aesenc		XDATA6, [KEYS6 + 16*11]	; 11. ENC
	aesenc		XDATA7, [KEYS7 + 16*11]	; 11. ENC

	aesenc		XDATA0, [KEYS0 + 16*12]	; 12. ENC
	aesenc		XDATA1, [KEYS1 + 16*12]	; 12. ENC
	aesenc		XDATA2, [KEYS2 + 16*12]	; 12. ENC
	aesenc		XDATA3, [KEYS3 + 16*12]	; 12. ENC
	aesenc		XDATA4, [KEYS4 + 16*12]	; 12. ENC
	aesenc		XDATA5, [KEYS5 + 16*12]	; 12. ENC
	aesenc		XDATA6, [KEYS6 + 16*12]	; 12. ENC
	aesenc		XDATA7, [KEYS7 + 16*12]	; 12. ENC

	aesenc		XDATA0, [KEYS0 + 16*13]	; 13. ENC
	aesenc		XDATA1, [KEYS1 + 16*13]	; 13. ENC
	aesenc		XDATA2, [KEYS2 + 16*13]	; 13. ENC
	aesenc		XDATA3, [KEYS3 + 16*13]	; 13. ENC
	aesenc		XDATA4, [KEYS4 + 16*13]	; 13. ENC
	aesenc		XDATA5, [KEYS5 + 16*13]	; 13. ENC
	aesenc		XDATA6, [KEYS6 + 16*13]	; 13. ENC
	aesenc		XDATA7, [KEYS7 + 16*13]	; 13. ENC

	aesenclast	XDATA0, [KEYS0 + 16*14]	; 14. ENC
	aesenclast	XDATA1, [KEYS1 + 16*14]	; 14. ENC
	aesenclast	XDATA2, [KEYS2 + 16*14]	; 14. ENC
	aesenclast	XDATA3, [KEYS3 + 16*14]	; 14. ENC
	aesenclast	XDATA4, [KEYS4 + 16*14]	; 14. ENC
	aesenclast	XDATA5, [KEYS5 + 16*14]	; 14. ENC
	aesenclast	XDATA6, [KEYS6 + 16*14]	; 14. ENC
	aesenclast	XDATA7, [KEYS7 + 16*14]	; 14. ENC

%ifndef CBC_MAC
	movdqu		[TMP + IDX], XDATA0		; write back ciphertext
	mov		TMP, [ARG + _aesarg_out + 8*1]
	movdqu		[TMP + IDX], XDATA1		; write back ciphertext
	mov		TMP, [ARG + _aesarg_out + 8*2]
	movdqu		[TMP + IDX], XDATA2		; write back ciphertext
	mov		TMP, [ARG + _aesarg_out + 8*3]
	movdqu		[TMP + IDX], XDATA3		; write back ciphertext
	mov		TMP, [ARG + _aesarg_out + 8*4]
	movdqu		[TMP + IDX], XDATA4		; write back ciphertext
	mov		TMP, [ARG + _aesarg_out + 8*5]
	movdqu		[TMP + IDX], XDATA5		; write back ciphertext
	mov		TMP, [ARG + _aesarg_out + 8*6]
	movdqu		[TMP + IDX], XDATA6		; write back ciphertext
	mov		TMP, [ARG + _aesarg_out + 8*7]
	movdqu		[TMP + IDX], XDATA7		; write back ciphertext
%endif
	add	IDX, 16
	cmp	[LEN_AREA], IDX
	jne	main_loop

done:
	;; update IV
	movdqa	[ARG + _aesarg_IV + 16*0], XDATA0
	movdqa	[ARG + _aesarg_IV + 16*1], XDATA1
	movdqa	[ARG + _aesarg_IV + 16*2], XDATA2
	movdqa	[ARG + _aesarg_IV + 16*3], XDATA3
	movdqa	[ARG + _aesarg_IV + 16*4], XDATA4
	movdqa	[ARG + _aesarg_IV + 16*5], XDATA5
	movdqa	[ARG + _aesarg_IV + 16*6], XDATA6
	movdqa	[ARG + _aesarg_IV + 16*7], XDATA7

	;; update IN and OUT
	movd	xmm0, [LEN_AREA]
	pshufd	xmm0, xmm0, 0x44
        movdqa  xmm1, xmm0
	paddq	xmm1, [ARG + _aesarg_in + 16*0]
        movdqa  xmm2, xmm0
	paddq	xmm2, [ARG + _aesarg_in + 16*1]
        movdqa  xmm3, xmm0
	paddq	xmm3, [ARG + _aesarg_in + 16*2]
        movdqa  xmm4, xmm0
	paddq	xmm4, [ARG + _aesarg_in + 16*3]
	movdqa	[ARG + _aesarg_in + 16*0], xmm1
	movdqa	[ARG + _aesarg_in + 16*1], xmm2
	movdqa	[ARG + _aesarg_in + 16*2], xmm3
	movdqa	[ARG + _aesarg_in + 16*3], xmm4

%ifndef CBC_MAC
        movdqa  xmm5, xmm0
	paddq	xmm5, [ARG + _aesarg_out + 16*0]
        movdqa  xmm6, xmm0
	paddq	xmm6, [ARG + _aesarg_out + 16*1]
        movdqa  xmm7, xmm0
	paddq	xmm7, [ARG + _aesarg_out + 16*2]
        movdqa  xmm8, xmm0
	paddq	xmm8, [ARG + _aesarg_out + 16*3]
	movdqa	[ARG + _aesarg_out + 16*0], xmm5
	movdqa	[ARG + _aesarg_out + 16*1], xmm6
	movdqa	[ARG + _aesarg_out + 16*2], xmm7
	movdqa	[ARG + _aesarg_out + 16*3], xmm8
%endif

;; XMMs are saved at a higher level
	mov	rbp, [GPR_SAVE_AREA + 8*0]
%ifdef CBC_MAC
	mov	rbx, [GPR_SAVE_AREA + 8*1]
	mov	r12, [GPR_SAVE_AREA + 8*2]
	mov	r13, [GPR_SAVE_AREA + 8*3]
	mov	r14, [GPR_SAVE_AREA + 8*4]
	mov	r15, [GPR_SAVE_AREA + 8*5]
%ifndef LINUX
	mov	rsi, [GPR_SAVE_AREA + 8*6]
	mov	rdi, [GPR_SAVE_AREA + 8*7]
%endif
%endif

        add	rsp, STACK_size

%ifdef SAFE_DATA
	clear_all_xmms_sse_asm
%endif ;; SAFE_DATA

	ret

mksection stack-noexec
