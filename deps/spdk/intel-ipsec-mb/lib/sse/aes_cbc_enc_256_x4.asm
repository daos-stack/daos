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
;;; process 4 buffers at a time, single data structure as input
;;; Updates In and Out pointers at end

%include "include/os.asm"
%include "include/mb_mgr_datastruct.asm"
%include "include/clear_regs.asm"

%define	MOVDQ movdqu ;; assume buffers not aligned
%macro pxor2 2
	MOVDQ	XTMP, %2
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
;; void aes_cbc_enc_256_x4(AES_ARGS *args, UINT64 len);
;; arg 1: ARG : addr of AES_ARGS structure
;; arg 2: LEN : len (in units of bytes)

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

%define IN0	r8
%define KEYS0	rbx

%define IN1	r10
%define KEYS1	REG3

%define IN2	r12
%define KEYS2	REG4

%define IN3	r14
%define KEYS3	rbp

%ifndef CBC_MAC
;; No cipher text write back for CBC-MAC
%define OUT0	r9
%define OUT1	r11
%define OUT2	r13
%define OUT3	r15
%endif

%define XDATA0		xmm0
%define XDATA1		xmm1
%define XDATA2		xmm2
%define XDATA3		xmm3

%define XKEY0_3		xmm4
%define XKEY0_6		[KEYS0 + 16*6]
%define XTMP		xmm5
%define XKEY0_9		xmm6

%define XKEY1_3		xmm7
%define XKEY1_6		xmm8
%define XKEY1_9		xmm9

%define XKEY2_3		xmm10
%define XKEY2_6		xmm11
%define XKEY2_9		xmm12

%define XKEY3_3		xmm13
%define XKEY3_6		xmm14
%define XKEY3_9		xmm15

mksection .text

%ifndef AES_CBC_ENC_X4
%ifdef CBC_MAC
MKGLOBAL(aes256_cbc_mac_x4,function,internal)
aes256_cbc_mac_x4:
%else
MKGLOBAL(aes_cbc_enc_256_x4,function,internal)
aes_cbc_enc_256_x4:
%endif

%else ;; NO-AESNI

%ifdef CBC_MAC
MKGLOBAL(aes256_cbc_mac_x4_no_aesni,function,internal)
aes256_cbc_mac_x4_no_aesni:
%else
MKGLOBAL(aes_cbc_enc_256_x4_no_aesni,function,internal)
aes_cbc_enc_256_x4_no_aesni:
%endif
%endif ;; AES_CBC_ENC_X4

	push	rbp

	mov	IDX, 16

	mov	IN0,	[ARG + _aesarg_in + 8*0]
	mov	IN1,	[ARG + _aesarg_in + 8*1]
	mov	IN2,	[ARG + _aesarg_in + 8*2]
	mov	IN3,	[ARG + _aesarg_in + 8*3]

	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

	MOVDQ		XDATA0, [IN0]		; load first block of plain text
	MOVDQ		XDATA1, [IN1]		; load first block of plain text
	MOVDQ		XDATA2, [IN2]		; load first block of plain text
	MOVDQ		XDATA3, [IN3]		; load first block of plain text

	mov		KEYS0,	[ARG + _aesarg_keys + 8*0]
	mov		KEYS1,	[ARG + _aesarg_keys + 8*1]
	mov		KEYS2,	[ARG + _aesarg_keys + 8*2]
	mov		KEYS3,	[ARG + _aesarg_keys + 8*3]

	pxor		XDATA0, [ARG + _aesarg_IV + 16*0] ; plaintext XOR IV
	pxor		XDATA1, [ARG + _aesarg_IV + 16*1] ; plaintext XOR IV
	pxor		XDATA2, [ARG + _aesarg_IV + 16*2] ; plaintext XOR IV
	pxor		XDATA3, [ARG + _aesarg_IV + 16*3] ; plaintext XOR IV

%ifndef CBC_MAC
	mov		OUT0,	[ARG + _aesarg_out + 8*0]
	mov		OUT1,	[ARG + _aesarg_out + 8*1]
	mov		OUT2,	[ARG + _aesarg_out + 8*2]
	mov		OUT3,	[ARG + _aesarg_out + 8*3]
%endif
	pxor		XDATA0, [KEYS0 + 16*0]		; 0. ARK
	pxor		XDATA1, [KEYS1 + 16*0]		; 0. ARK
	pxor		XDATA2, [KEYS2 + 16*0]		; 0. ARK
	pxor		XDATA3, [KEYS3 + 16*0]		; 0. ARK

	aesenc		XDATA0, [KEYS0 + 16*1]	; 1. ENC
	aesenc		XDATA1, [KEYS1 + 16*1]	; 1. ENC
	aesenc		XDATA2, [KEYS2 + 16*1]	; 1. ENC
	aesenc		XDATA3, [KEYS3 + 16*1]	; 1. ENC

	aesenc		XDATA0, [KEYS0 + 16*2]	; 2. ENC
	aesenc		XDATA1, [KEYS1 + 16*2]	; 2. ENC
	aesenc		XDATA2, [KEYS2 + 16*2]	; 2. ENC
	aesenc		XDATA3, [KEYS3 + 16*2]	; 2. ENC

	movdqa		XKEY0_3, [KEYS0 + 16*3]	; load round 3 key
	movdqa		XKEY1_3, [KEYS1 + 16*3]	; load round 3 key
	movdqa		XKEY2_3, [KEYS2 + 16*3]	; load round 3 key
	movdqa		XKEY3_3, [KEYS3 + 16*3]	; load round 3 key

	aesenc		XDATA0, XKEY0_3		; 3. ENC
	aesenc		XDATA1, XKEY1_3		; 3. ENC
	aesenc		XDATA2, XKEY2_3		; 3. ENC
	aesenc		XDATA3, XKEY3_3		; 3. ENC

	aesenc		XDATA0, [KEYS0 + 16*4]	; 4. ENC
	aesenc		XDATA1, [KEYS1 + 16*4]	; 4. ENC
	aesenc		XDATA2, [KEYS2 + 16*4]	; 4. ENC
	aesenc		XDATA3, [KEYS3 + 16*4]	; 4. ENC

	aesenc		XDATA0, [KEYS0 + 16*5]	; 5. ENC
	aesenc		XDATA1, [KEYS1 + 16*5]	; 5. ENC
	aesenc		XDATA2, [KEYS2 + 16*5]	; 5. ENC
	aesenc		XDATA3, [KEYS3 + 16*5]	; 5. ENC

	movdqa		XKEY1_6, [KEYS1 + 16*6]	; load round 6 key
	movdqa		XKEY2_6, [KEYS2 + 16*6]	; load round 6 key
	movdqa		XKEY3_6, [KEYS3 + 16*6]	; load round 6 key

	aesenc		XDATA0, XKEY0_6		; 6. ENC
	aesenc		XDATA1, XKEY1_6		; 6. ENC
	aesenc		XDATA2, XKEY2_6		; 6. ENC
	aesenc		XDATA3, XKEY3_6		; 6. ENC

	aesenc		XDATA0, [KEYS0 + 16*7]	; 7. ENC
	aesenc		XDATA1, [KEYS1 + 16*7]	; 7. ENC
	aesenc		XDATA2, [KEYS2 + 16*7]	; 7. ENC
	aesenc		XDATA3, [KEYS3 + 16*7]	; 7. ENC

	aesenc		XDATA0, [KEYS0 + 16*8]	; 8. ENC
	aesenc		XDATA1, [KEYS1 + 16*8]	; 8. ENC
	aesenc		XDATA2, [KEYS2 + 16*8]	; 8. ENC
	aesenc		XDATA3, [KEYS3 + 16*8]	; 8. ENC

	movdqa		XKEY0_9, [KEYS0 + 16*9]	; load round 9 key
	movdqa		XKEY1_9, [KEYS1 + 16*9]	; load round 9 key
	movdqa		XKEY2_9, [KEYS2 + 16*9]	; load round 9 key
	movdqa		XKEY3_9, [KEYS3 + 16*9]	; load round 9 key

	aesenc		XDATA0, XKEY0_9		; 9. ENC
	aesenc		XDATA1, XKEY1_9		; 9. ENC
	aesenc		XDATA2, XKEY2_9		; 9. ENC
	aesenc		XDATA3, XKEY3_9		; 9. ENC

	aesenc    	XDATA0, [KEYS0 + 16*10]	; 10. ENC
	aesenc    	XDATA1, [KEYS1 + 16*10]	; 10. ENC
	aesenc    	XDATA2, [KEYS2 + 16*10]	; 10. ENC
	aesenc    	XDATA3, [KEYS3 + 16*10]	; 10. ENC

	aesenc		XDATA0, [KEYS0 + 16*11]	; 11. ENC
	aesenc		XDATA1, [KEYS1 + 16*11]	; 11. ENC
	aesenc		XDATA2, [KEYS2 + 16*11]	; 11. ENC
	aesenc		XDATA3, [KEYS3 + 16*11]	; 11. ENC

	aesenc		XDATA0, [KEYS0 + 16*12]	; 12. ENC
	aesenc		XDATA1, [KEYS1 + 16*12]	; 12. ENC
	aesenc		XDATA2, [KEYS2 + 16*12]	; 12. ENC
	aesenc		XDATA3, [KEYS3 + 16*12]	; 12. ENC

	aesenc		XDATA0, [KEYS0 + 16*13]	; 13. ENC
	aesenc		XDATA1, [KEYS1 + 16*13]	; 13. ENC
	aesenc		XDATA2, [KEYS2 + 16*13]	; 13. ENC
	aesenc		XDATA3, [KEYS3 + 16*13]	; 13. ENC

	aesenclast	XDATA0, [KEYS0 + 16*14]	; 14. ENC
	aesenclast	XDATA1, [KEYS1 + 16*14]	; 14. ENC
	aesenclast	XDATA2, [KEYS2 + 16*14]	; 14. ENC
	aesenclast	XDATA3, [KEYS3 + 16*14]	; 14. ENC

%ifndef CBC_MAC
	MOVDQ		[OUT0], XDATA0		; write back ciphertext
	MOVDQ		[OUT1], XDATA1		; write back ciphertext
	MOVDQ		[OUT2], XDATA2		; write back ciphertext
	MOVDQ		[OUT3], XDATA3		; write back ciphertext
%endif
	cmp		LEN, IDX
	je		done

main_loop:
	pxor2		XDATA0, [IN0 + IDX]	; plaintext XOR IV
	pxor2		XDATA1, [IN1 + IDX]	; plaintext XOR IV
	pxor2		XDATA2, [IN2 + IDX]	; plaintext XOR IV
	pxor2		XDATA3, [IN3 + IDX]	; plaintext XOR IV

	pxor		XDATA0, [KEYS0 + 16*0] 	; 0. ARK
	pxor		XDATA1, [KEYS1 + 16*0] 	; 0. ARK
	pxor		XDATA2, [KEYS2 + 16*0] 	; 0. ARK
	pxor		XDATA3, [KEYS3 + 16*0] 	; 0. ARK

	aesenc		XDATA0, [KEYS0 + 16*1]	; 1. ENC
	aesenc		XDATA1, [KEYS1 + 16*1]	; 1. ENC
	aesenc		XDATA2, [KEYS2 + 16*1]	; 1. ENC
	aesenc		XDATA3, [KEYS3 + 16*1]	; 1. ENC

	aesenc		XDATA0, [KEYS0 + 16*2]	; 2. ENC
	aesenc		XDATA1, [KEYS1 + 16*2]	; 2. ENC
	aesenc		XDATA2, [KEYS2 + 16*2]	; 2. ENC
	aesenc		XDATA3, [KEYS3 + 16*2]	; 2. ENC

	aesenc		XDATA0, XKEY0_3		; 3. ENC
	aesenc		XDATA1, XKEY1_3		; 3. ENC
	aesenc		XDATA2, XKEY2_3		; 3. ENC
	aesenc		XDATA3, XKEY3_3		; 3. ENC

	aesenc		XDATA0, [KEYS0 + 16*4]	; 4. ENC
	aesenc		XDATA1, [KEYS1 + 16*4]	; 4. ENC
	aesenc		XDATA2, [KEYS2 + 16*4]	; 4. ENC
	aesenc		XDATA3, [KEYS3 + 16*4]	; 4. ENC

	aesenc		XDATA0, [KEYS0 + 16*5]	; 5. ENC
	aesenc		XDATA1, [KEYS1 + 16*5]	; 5. ENC
	aesenc		XDATA2, [KEYS2 + 16*5]	; 5. ENC
	aesenc		XDATA3, [KEYS3 + 16*5]	; 5. ENC

	aesenc		XDATA0, XKEY0_6		; 6. ENC
	aesenc		XDATA1, XKEY1_6		; 6. ENC
	aesenc		XDATA2, XKEY2_6		; 6. ENC
	aesenc		XDATA3, XKEY3_6		; 6. ENC

	aesenc		XDATA0, [KEYS0 + 16*7]	; 7. ENC
	aesenc		XDATA1, [KEYS1 + 16*7]	; 7. ENC
	aesenc		XDATA2, [KEYS2 + 16*7]	; 7. ENC
	aesenc		XDATA3, [KEYS3 + 16*7]	; 7. ENC

	aesenc		XDATA0, [KEYS0 + 16*8]	; 8. ENC
	aesenc		XDATA1, [KEYS1 + 16*8]	; 8. ENC
	aesenc		XDATA2, [KEYS2 + 16*8]	; 8. ENC
	aesenc		XDATA3, [KEYS3 + 16*8]	; 8. ENC

	aesenc		XDATA0, XKEY0_9		; 9. ENC
	aesenc		XDATA1, XKEY1_9		; 9. ENC
	aesenc		XDATA2, XKEY2_9		; 9. ENC
	aesenc		XDATA3, XKEY3_9		; 9. ENC

	aesenc    	XDATA0, [KEYS0 + 16*10]	; 10. ENC
	aesenc    	XDATA1, [KEYS1 + 16*10]	; 10. ENC
	aesenc    	XDATA2, [KEYS2 + 16*10]	; 10. ENC
	aesenc    	XDATA3, [KEYS3 + 16*10]	; 10. ENC

	aesenc		XDATA0, [KEYS0 + 16*11]	; 11. ENC
	aesenc		XDATA1, [KEYS1 + 16*11]	; 11. ENC
	aesenc		XDATA2, [KEYS2 + 16*11]	; 11. ENC
	aesenc		XDATA3, [KEYS3 + 16*11]	; 11. ENC

	aesenc		XDATA0, [KEYS0 + 16*12]	; 12. ENC
	aesenc		XDATA1, [KEYS1 + 16*12]	; 12. ENC
	aesenc		XDATA2, [KEYS2 + 16*12]	; 12. ENC
	aesenc		XDATA3, [KEYS3 + 16*12]	; 12. ENC

	aesenc		XDATA0, [KEYS0 + 16*13]	; 13. ENC
	aesenc		XDATA1, [KEYS1 + 16*13]	; 13. ENC
	aesenc		XDATA2, [KEYS2 + 16*13]	; 13. ENC
	aesenc		XDATA3, [KEYS3 + 16*13]	; 13. ENC

	aesenclast	XDATA0, [KEYS0 + 16*14]	; 14. ENC
	aesenclast	XDATA1, [KEYS1 + 16*14]	; 14. ENC
	aesenclast	XDATA2, [KEYS2 + 16*14]	; 14. ENC
	aesenclast	XDATA3, [KEYS3 + 16*14]	; 14. ENC

%ifndef CBC_MAC
        ;; No cipher text write back for CBC-MAC
	MOVDQ		[OUT0 + IDX], XDATA0	; write back ciphertext
	MOVDQ		[OUT1 + IDX], XDATA1	; write back ciphertex
	MOVDQ		[OUT2 + IDX], XDATA2	; write back ciphertex
	MOVDQ		[OUT3 + IDX], XDATA3	; write back ciphertex
%endif

	add	IDX, 16
	cmp	LEN, IDX
	jne	main_loop

done:
	;; update IV
	movdqa	[ARG + _aesarg_IV + 16*0], XDATA0
	movdqa	[ARG + _aesarg_IV + 16*1], XDATA1
	movdqa	[ARG + _aesarg_IV + 16*2], XDATA2
	movdqa	[ARG + _aesarg_IV + 16*3], XDATA3

	;; update IN and OUT
	add	IN0, LEN
	mov	[ARG + _aesarg_in + 8*0], IN0
	add	IN1, LEN
	mov	[ARG + _aesarg_in + 8*1], IN1
	add	IN2, LEN
	mov	[ARG + _aesarg_in + 8*2], IN2
	add	IN3, LEN
	mov	[ARG + _aesarg_in + 8*3], IN3

%ifndef CBC_MAC
        ;; No OUT pointer updates for CBC-MAC
	add	OUT0, LEN
	mov	[ARG + _aesarg_out + 8*0], OUT0
	add	OUT1, LEN
	mov	[ARG + _aesarg_out + 8*1], OUT1
	add	OUT2, LEN
	mov	[ARG + _aesarg_out + 8*2], OUT2
	add	OUT3, LEN
	mov	[ARG + _aesarg_out + 8*3], OUT3
%endif
	pop	rbp

%ifdef SAFE_DATA
	clear_all_xmms_sse_asm
%endif ;; SAFE_DATA

	ret

mksection stack-noexec
