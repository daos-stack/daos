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

; routine to do AES cbc decrypt on 16n bytes doing AES by 4

; XMM registers are clobbered. Saving/restoring must be done at a higher level

; void aes_cbc_dec_256_sse(void    *in,
;                          UINT128 *IV,
;                          UINT128  keys[15],
;                          void    *out,
;                          UINT64   len_bytes);
;
; arg 1: rcx: pointer to input (cipher text)
; arg 2: rdx: pointer to IV
; arg 3: r8:  pointer to keys
; arg 4: r9:  pointer to output (plain text)
; arg 5: sp:  length in bytes (multiple of 16)
;

%include "include/os.asm"
%include "include/clear_regs.asm"

%ifndef AES_CBC_DEC_256
%define AES_CBC_DEC_256 aes_cbc_dec_256_sse
%endif

%define MOVDQ	movdqu

%ifdef LINUX
%define IN		rdi
%define IV		rsi
%define KEYS		rdx
%define OUT		rcx
%define LEN		r8
%else
%define IN		rcx
%define IV		rdx
%define KEYS		r8
%define OUT		r9
%define LEN		r10
%endif

%define IDX		rax
%define TMP		IDX
%define XDATA0		xmm0
%define XDATA1		xmm1
%define XDATA2		xmm2
%define XDATA3		xmm3
%define XKEY0		xmm4
%define XKEY2		xmm5
%define XKEY4		xmm6
%define XKEY6		xmm7
%define XKEY10		xmm8
%define XIV		xmm9
%define XSAVED0		xmm10
%define XSAVED1		xmm11
%define XSAVED2		xmm12
%define XSAVED3		xmm13
%define XKEY_A		xmm14
%define XKEY_B		xmm15

%define IV_TMP		XSAVED3

mksection .text

MKGLOBAL(AES_CBC_DEC_256,function,internal)
AES_CBC_DEC_256:
%ifndef LINUX
	mov	LEN, [rsp + 8*5]
%endif

	mov	TMP, LEN
	and	TMP, 3*16
	jz	initial_4
	cmp	TMP, 2*16
	jb	initial_1
	ja	initial_3

initial_2:
	; load cipher text
	movdqu	XDATA0, [IN + 0*16]
	movdqu	XDATA1, [IN + 1*16]

	movdqa	XKEY0, [KEYS + 0*16]

	; save cipher text
	movdqa	XSAVED0, XDATA0
	movdqa	XIV,     XDATA1

	pxor	XDATA0, XKEY0		; 0. ARK
	pxor	XDATA1, XKEY0

	movdqa	XKEY2, [KEYS + 2*16]

	aesdec	XDATA0, [KEYS + 1*16]	; 1. DEC
	aesdec	XDATA1, [KEYS + 1*16]

	mov	IDX, 2*16

	aesdec	XDATA0, XKEY2		; 2. DEC
	aesdec	XDATA1, XKEY2

	movdqa	XKEY4, [KEYS + 4*16]

	aesdec	XDATA0, [KEYS + 3*16]	; 3. DEC
	aesdec	XDATA1, [KEYS + 3*16]

	movdqu	IV_TMP, [IV]

	aesdec	XDATA0, XKEY4		; 4. DEC
	aesdec	XDATA1, XKEY4

	movdqa	XKEY6, [KEYS + 6*16]

	aesdec	XDATA0, [KEYS + 5*16]	; 5. DEC
	aesdec	XDATA1, [KEYS + 5*16]

	aesdec	XDATA0, XKEY6		; 6. DEC
	aesdec	XDATA1, XKEY6

	movdqa	XKEY_B, [KEYS + 8*16]

	aesdec	XDATA0, [KEYS + 7*16]	; 7. DEC
	aesdec	XDATA1, [KEYS + 7*16]

	aesdec	XDATA0, XKEY_B		; 8. DEC
	aesdec	XDATA1, XKEY_B

	movdqa	XKEY10, [KEYS + 10*16]

	aesdec	XDATA0, [KEYS + 9*16]	; 9. DEC
	aesdec	XDATA1, [KEYS + 9*16]

	aesdec	XDATA0, XKEY10		; 10. DEC
	aesdec	XDATA1, XKEY10

	aesdec	XDATA0, [KEYS + 11*16]	; 11. DEC
	aesdec	XDATA1, [KEYS + 11*16]

	aesdec	XDATA0, [KEYS + 12*16]	; 12. DEC
	aesdec	XDATA1, [KEYS + 12*16]

	aesdec	XDATA0, [KEYS + 13*16]	; 13. DEC
	aesdec	XDATA1, [KEYS + 13*16]

	aesdeclast	XDATA0, [KEYS + 14*16]	; 14. DEC
	aesdeclast	XDATA1, [KEYS + 14*16]

	pxor	XDATA0, IV_TMP
	pxor	XDATA1, XSAVED0

	movdqu	[OUT + 0*16], XDATA0
	movdqu	[OUT + 1*16], XDATA1

	cmp	LEN, 2*16
	je	done
	jmp	main_loop

	align 16
initial_1:
	; load cipher text
	movdqu	XDATA0, [IN + 0*16]

	movdqa	XKEY0, [KEYS + 0*16]

	; save cipher text
	movdqa	XIV,     XDATA0

	pxor	XDATA0, XKEY0		; 0. ARK

	movdqa	XKEY2, [KEYS + 2*16]

	aesdec	XDATA0, [KEYS + 1*16]	; 1. DEC

	mov	IDX, 1*16

	aesdec	XDATA0, XKEY2		; 2. DEC

	movdqa	XKEY4, [KEYS + 4*16]

	aesdec	XDATA0, [KEYS + 3*16]	; 3. DEC

	movdqu	IV_TMP, [IV]

	aesdec	XDATA0, XKEY4		; 4. DEC

	movdqa	XKEY6, [KEYS + 6*16]

	aesdec	XDATA0, [KEYS + 5*16]	; 5. DEC

	aesdec	XDATA0, XKEY6		; 6. DEC

	movdqa	XKEY_B, [KEYS + 8*16]

	aesdec	XDATA0, [KEYS + 7*16]	; 7. DEC

	aesdec	XDATA0, XKEY_B		; 8. DEC

	movdqa	XKEY10, [KEYS + 10*16]

	aesdec	XDATA0, [KEYS + 9*16]	; 9. DEC

	aesdec	XDATA0, XKEY10		; 10. DEC

	aesdec	XDATA0, [KEYS + 11*16]	; 11. DEC

	aesdec	XDATA0, [KEYS + 12*16]	; 12. DEC

	aesdec	XDATA0, [KEYS + 13*16]	; 13. DEC

	aesdeclast	XDATA0, [KEYS + 14*16]	; 14. DEC

	pxor	XDATA0, IV_TMP

	movdqu	[OUT + 0*16], XDATA0

	cmp	LEN, 1*16
	je	done
	jmp	main_loop

initial_3:
	; load cipher text
	movdqu	XDATA0, [IN + 0*16]
	movdqu	XDATA1, [IN + 1*16]
	movdqu	XDATA2, [IN + 2*16]

	movdqa	XKEY0, [KEYS + 0*16]

	; save cipher text
	movdqa	XSAVED0, XDATA0
	movdqa	XSAVED1, XDATA1
	movdqa	XIV,     XDATA2

	movdqa	XKEY_A, [KEYS + 1*16]

	pxor	XDATA0, XKEY0		; 0. ARK
	pxor	XDATA1, XKEY0
	pxor	XDATA2, XKEY0

	movdqa	XKEY2, [KEYS + 2*16]

	aesdec	XDATA0, XKEY_A		; 1. DEC
	aesdec	XDATA1, XKEY_A
	aesdec	XDATA2, XKEY_A

	movdqa	XKEY_A, [KEYS + 3*16]
	mov	IDX, 3*16

	aesdec	XDATA0, XKEY2		; 2. DEC
	aesdec	XDATA1, XKEY2
	aesdec	XDATA2, XKEY2

	movdqa	XKEY4, [KEYS + 4*16]

	aesdec	XDATA0, XKEY_A		; 3. DEC
	aesdec	XDATA1, XKEY_A
	aesdec	XDATA2, XKEY_A

	movdqa	XKEY_A, [KEYS + 5*16]
	movdqu	IV_TMP, [IV]

	aesdec	XDATA0, XKEY4		; 4. DEC
	aesdec	XDATA1, XKEY4
	aesdec	XDATA2, XKEY4

	movdqa	XKEY6, [KEYS + 6*16]

	aesdec	XDATA0, XKEY_A		; 5. DEC
	aesdec	XDATA1, XKEY_A
	aesdec	XDATA2, XKEY_A

	movdqa	XKEY_A, [KEYS + 7*16]

	aesdec	XDATA0, XKEY6		; 6. DEC
	aesdec	XDATA1, XKEY6
	aesdec	XDATA2, XKEY6

	movdqa	XKEY_B, [KEYS + 8*16]

	aesdec	XDATA0, XKEY_A		; 7. DEC
	aesdec	XDATA1, XKEY_A
	aesdec	XDATA2, XKEY_A

	movdqa	XKEY_A, [KEYS + 9*16]

	aesdec	XDATA0, XKEY_B		; 8. DEC
	aesdec	XDATA1, XKEY_B
	aesdec	XDATA2, XKEY_B

	movdqa	XKEY10, [KEYS + 10*16]

	aesdec	XDATA0, XKEY_A		; 9. DEC
	aesdec	XDATA1, XKEY_A
	aesdec	XDATA2, XKEY_A

	movdqa	XKEY_A, [KEYS + 11*16]

	aesdec	XDATA0, XKEY10		; 10. DEC
	aesdec	XDATA1, XKEY10
	aesdec	XDATA2, XKEY10

	movdqa	XKEY_B, [KEYS + 12*16]

	aesdec	XDATA0, XKEY_A		; 11. DEC
	aesdec	XDATA1, XKEY_A
	aesdec	XDATA2, XKEY_A

	movdqa	XKEY_A, [KEYS + 13*16]

	aesdec	XDATA0, XKEY_B		; 12. DEC
	aesdec	XDATA1, XKEY_B
	aesdec	XDATA2, XKEY_B

	movdqa	XKEY_B, [KEYS + 14*16]

	aesdec	XDATA0, XKEY_A		; 13. DEC
	aesdec	XDATA1, XKEY_A
	aesdec	XDATA2, XKEY_A

	aesdeclast	XDATA0, XKEY_B		; 14. DEC
	aesdeclast	XDATA1, XKEY_B
	aesdeclast	XDATA2, XKEY_B

	pxor	XDATA0, IV_TMP
	pxor	XDATA1, XSAVED0
	pxor	XDATA2, XSAVED1

	movdqu	[OUT + 0*16], XDATA0
	movdqu	[OUT + 1*16], XDATA1
	movdqu	[OUT + 2*16], XDATA2

	cmp	LEN, 3*16
	je	done
	jmp	main_loop

	align 16
initial_4:
	; load cipher text
	movdqu	XDATA0, [IN + 0*16]
	movdqu	XDATA1, [IN + 1*16]
	movdqu	XDATA2, [IN + 2*16]
	movdqu	XDATA3, [IN + 3*16]

	movdqa	XKEY0, [KEYS + 0*16]

	; save cipher text
	movdqa	XSAVED0, XDATA0
	movdqa	XSAVED1, XDATA1
	movdqa	XSAVED2, XDATA2
	movdqa	XIV,     XDATA3

	movdqa	XKEY_A, [KEYS + 1*16]

	pxor	XDATA0, XKEY0		; 0. ARK
	pxor	XDATA1, XKEY0
	pxor	XDATA2, XKEY0
	pxor	XDATA3, XKEY0

	movdqa	XKEY2, [KEYS + 2*16]

	aesdec	XDATA0, XKEY_A		; 1. DEC
	aesdec	XDATA1, XKEY_A
	aesdec	XDATA2, XKEY_A
	aesdec	XDATA3, XKEY_A

	movdqa	XKEY_A, [KEYS + 3*16]

	mov	IDX, 4*16

	aesdec	XDATA0, XKEY2		; 2. DEC
	aesdec	XDATA1, XKEY2
	aesdec	XDATA2, XKEY2
	aesdec	XDATA3, XKEY2

	movdqa	XKEY4, [KEYS + 4*16]

	aesdec	XDATA0, XKEY_A		; 3. DEC
	aesdec	XDATA1, XKEY_A
	aesdec	XDATA2, XKEY_A
	aesdec	XDATA3, XKEY_A

	movdqa	XKEY_A, [KEYS + 5*16]

	movdqu	IV_TMP, [IV]

	aesdec	XDATA0, XKEY4		; 4. DEC
	aesdec	XDATA1, XKEY4
	aesdec	XDATA2, XKEY4
	aesdec	XDATA3, XKEY4

	movdqa	XKEY6, [KEYS + 6*16]

	aesdec	XDATA0, XKEY_A		; 5. DEC
	aesdec	XDATA1, XKEY_A
	aesdec	XDATA2, XKEY_A
	aesdec	XDATA3, XKEY_A

	movdqa	XKEY_A, [KEYS + 7*16]

	aesdec	XDATA0, XKEY6		; 6. DEC
	aesdec	XDATA1, XKEY6
	aesdec	XDATA2, XKEY6
	aesdec	XDATA3, XKEY6

	movdqa	XKEY_B, [KEYS + 8*16]

	aesdec	XDATA0, XKEY_A		; 7. DEC
	aesdec	XDATA1, XKEY_A
	aesdec	XDATA2, XKEY_A
	aesdec	XDATA3, XKEY_A

	movdqa	XKEY_A, [KEYS + 9*16]

	aesdec	XDATA0, XKEY_B		; 8. DEC
	aesdec	XDATA1, XKEY_B
	aesdec	XDATA2, XKEY_B
	aesdec	XDATA3, XKEY_B

	movdqa	XKEY10, [KEYS + 10*16]

	aesdec	XDATA0, XKEY_A		; 9. DEC
	aesdec	XDATA1, XKEY_A
	aesdec	XDATA2, XKEY_A
	aesdec	XDATA3, XKEY_A

	movdqa	XKEY_A, [KEYS + 11*16]

	aesdec	XDATA0, XKEY10	; 10. DEC
	aesdec	XDATA1, XKEY10
	aesdec	XDATA2, XKEY10
	aesdec	XDATA3, XKEY10

	movdqa	XKEY_B, [KEYS + 12*16]

	aesdec	XDATA0, XKEY_A		; 11. DEC
	aesdec	XDATA1, XKEY_A
	aesdec	XDATA2, XKEY_A
	aesdec	XDATA3, XKEY_A

	movdqa	XKEY_A, [KEYS + 13*16]

	aesdec	XDATA0, XKEY_B		; 12. DEC
	aesdec	XDATA1, XKEY_B
	aesdec	XDATA2, XKEY_B
	aesdec	XDATA3, XKEY_B

	movdqa	XKEY_B, [KEYS + 14*16]

	aesdec	XDATA0, XKEY_A		; 13. DEC
	aesdec	XDATA1, XKEY_A
	aesdec	XDATA2, XKEY_A
	aesdec	XDATA3, XKEY_A

	aesdeclast	XDATA0, XKEY_B		; 14. DEC
	aesdeclast	XDATA1, XKEY_B
	aesdeclast	XDATA2, XKEY_B
	aesdeclast	XDATA3, XKEY_B

	pxor	XDATA0, IV_TMP
	pxor	XDATA1, XSAVED0
	pxor	XDATA2, XSAVED1
	pxor	XDATA3, XSAVED2

	movdqu	[OUT + 0*16], XDATA0
	movdqu	[OUT + 1*16], XDATA1
	movdqu	[OUT + 2*16], XDATA2
	movdqu	[OUT + 3*16], XDATA3

	cmp	LEN, 4*16
	jz	done
	jmp	main_loop

	align 16
main_loop:
	; load cipher text
	movdqu	XDATA0, [IN + IDX + 0*16]
	movdqu	XDATA1, [IN + IDX + 1*16]
	movdqu	XDATA2, [IN + IDX + 2*16]
	movdqu	XDATA3, [IN + IDX + 3*16]

	; save cipher text
	movdqa	XSAVED0, XDATA0
	movdqa	XSAVED1, XDATA1
	movdqa	XSAVED2, XDATA2
	movdqa	XSAVED3, XDATA3

	movdqa	XKEY_A, [KEYS + 1*16]

	pxor	XDATA0, XKEY0		; 0. ARK
	pxor	XDATA1, XKEY0
	pxor	XDATA2, XKEY0
	pxor	XDATA3, XKEY0

	add	IDX, 4*16

	aesdec	XDATA0, XKEY_A		; 1. DEC
	aesdec	XDATA1, XKEY_A
	aesdec	XDATA2, XKEY_A
	aesdec	XDATA3, XKEY_A

	movdqa	XKEY_A, [KEYS + 3*16]

	aesdec	XDATA0, XKEY2		; 2. DEC
	aesdec	XDATA1, XKEY2
	aesdec	XDATA2, XKEY2
	aesdec	XDATA3, XKEY2

	aesdec	XDATA0, XKEY_A		; 3. DEC
	aesdec	XDATA1, XKEY_A
	aesdec	XDATA2, XKEY_A
	aesdec	XDATA3, XKEY_A

	movdqa	XKEY_A, [KEYS + 5*16]

	aesdec	XDATA0, XKEY4		; 4. DEC
	aesdec	XDATA1, XKEY4
	aesdec	XDATA2, XKEY4
	aesdec	XDATA3, XKEY4

	aesdec	XDATA0, XKEY_A		; 5. DEC
	aesdec	XDATA1, XKEY_A
	aesdec	XDATA2, XKEY_A
	aesdec	XDATA3, XKEY_A

	movdqa	XKEY_A, [KEYS + 7*16]

	aesdec	XDATA0, XKEY6		; 6. DEC
	aesdec	XDATA1, XKEY6
	aesdec	XDATA2, XKEY6
	aesdec	XDATA3, XKEY6

	movdqa	XKEY_B, [KEYS + 8*16]

	aesdec	XDATA0, XKEY_A		; 7. DEC
	aesdec	XDATA1, XKEY_A
	aesdec	XDATA2, XKEY_A
	aesdec	XDATA3, XKEY_A

	movdqa	XKEY_A, [KEYS + 9*16]

	aesdec	XDATA0, XKEY_B		; 8. DEC
	aesdec	XDATA1, XKEY_B
	aesdec	XDATA2, XKEY_B
	aesdec	XDATA3, XKEY_B

	aesdec	XDATA0, XKEY_A		; 9. DEC
	aesdec	XDATA1, XKEY_A
	aesdec	XDATA2, XKEY_A
	aesdec	XDATA3, XKEY_A

	movdqa	XKEY_A, [KEYS + 11*16]

	aesdec	XDATA0, XKEY10		; 10. DEC
	aesdec	XDATA1, XKEY10
	aesdec	XDATA2, XKEY10
	aesdec	XDATA3, XKEY10

	movdqa	XKEY_B, [KEYS + 12*16]

	aesdec	XDATA0, XKEY_A		; 11. DEC
	aesdec	XDATA1, XKEY_A
	aesdec	XDATA2, XKEY_A
	aesdec	XDATA3, XKEY_A

	movdqa	XKEY_A, [KEYS + 13*16]

	aesdec	XDATA0, XKEY_B		; 12. DEC
	aesdec	XDATA1, XKEY_B
	aesdec	XDATA2, XKEY_B
	aesdec	XDATA3, XKEY_B

	movdqa	XKEY_B, [KEYS + 14*16]

	aesdec	XDATA0, XKEY_A		; 13. DEC
	aesdec	XDATA1, XKEY_A
	aesdec	XDATA2, XKEY_A
	aesdec	XDATA3, XKEY_A

	aesdeclast	XDATA0, XKEY_B		; 14. DEC
	aesdeclast	XDATA1, XKEY_B
	aesdeclast	XDATA2, XKEY_B
	aesdeclast	XDATA3, XKEY_B

	pxor	XDATA0, XIV
	pxor	XDATA1, XSAVED0
	pxor	XDATA2, XSAVED1
	pxor	XDATA3, XSAVED2

	movdqu	[OUT + IDX + 0*16 - 4*16], XDATA0
	movdqu	[OUT + IDX + 1*16 - 4*16], XDATA1
	movdqu	[OUT + IDX + 2*16 - 4*16], XDATA2
	movdqu	[OUT + IDX + 3*16 - 4*16], XDATA3

	movdqa	XIV, XSAVED3

	CMP	IDX, LEN
	jne	main_loop

done:

%ifdef SAFE_DATA
	clear_all_xmms_sse_asm
%endif ;; SAFE_DATA

	ret

mksection stack-noexec
