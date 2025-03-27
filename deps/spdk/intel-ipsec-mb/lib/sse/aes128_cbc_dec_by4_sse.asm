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

; void aes_cbc_dec_128_sse(void    *in,
;                          UINT128 *IV,
;                          UINT128  keys[11],
;                          void    *out,
;                          UINT64   len_bytes);
;
; arg 1: IN:   pointer to input (cipher text)
; arg 2: IV:   pointer to IV
; arg 3: KEYS: pointer to keys
; arg 4: OUT:  pointer to output (plain text)
; arg 5: LEN:  length in bytes (multiple of 16)
;
%include "include/os.asm"
%include "include/clear_regs.asm"

%ifndef AES_CBC_DEC_128
%define AES_CBC_DEC_128 aes_cbc_dec_128_sse
%endif

%ifdef CBCS
%define OFFSET 160
%else
%define OFFSET 16
%endif

%define MOVDQ	movdqu

%ifdef LINUX
%define IN		rdi
%define IV		rsi
%define KEYS		rdx
%define OUT		rcx
%define BYTES		r8
%define NEXT_IV         r9
%else
%define IN		rcx
%define IV		rdx
%define KEYS		r8
%define OUT		r9
%define NEXT_IV         r11
%endif

%define LEN             rax
%define IDX             r10
%define TMP		IDX
%define TMP2            r11
%define XDATA0		xmm0
%define XDATA1		xmm1
%define XDATA2		xmm2
%define XDATA3		xmm3
%define XKEY0		xmm4
%define XKEY2		xmm5
%define XKEY4		xmm6
%define XKEY6		xmm7
%define XKEY8		xmm8
%define XKEY10		xmm9
%define XIV		xmm10
%define XSAVED0		xmm11
%define XSAVED1		xmm12
%define XSAVED2		xmm13
%define XSAVED3		xmm14
%define XKEY		xmm15

%define IV_TMP		XSAVED3

mksection .text

MKGLOBAL(AES_CBC_DEC_128,function,internal)
AES_CBC_DEC_128:
%ifndef LINUX
	mov	LEN, [rsp + 8*5]
%else
        mov     LEN, BYTES
%endif
%ifdef CBCS
        ;; convert CBCS length to standard number of CBC blocks
        ;; ((len + 9 blocks) / 160) = num blocks to decrypt
        mov     TMP2, rdx
        xor     rdx, rdx        ;; store and zero rdx for div
        add     LEN, 9*16
        mov     TMP, 160
        div     TMP             ;; divide by 160
        shl     LEN, 4          ;; multiply by 16 to get num bytes
        mov     rdx, TMP2
%endif
	mov	TMP, LEN
	and	TMP, 3*16
	jz	initial_4
	cmp	TMP, 2*16
	jb	initial_1
	ja	initial_3

initial_2:
	; load cipher text
	movdqu	XDATA0, [IN + 0*OFFSET]
	movdqu	XDATA1, [IN + 1*OFFSET]

	movdqa	XKEY0, [KEYS + 0*16]

	; save cipher text
	movdqa	XSAVED0, XDATA0
	movdqa	XIV,     XDATA1

	pxor	XDATA0, XKEY0		; 0. ARK
	pxor	XDATA1, XKEY0

	movdqa	XKEY2, [KEYS + 2*16]

	aesdec	XDATA0, [KEYS + 1*16]	; 1. DEC
	aesdec	XDATA1, [KEYS + 1*16]

	mov	IDX, 2*OFFSET

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

	movdqa	XKEY8, [KEYS + 8*16]

	aesdec	XDATA0, [KEYS + 7*16]	; 7. DEC
	aesdec	XDATA1, [KEYS + 7*16]

	aesdec	XDATA0, XKEY8		; 8. DEC
	aesdec	XDATA1, XKEY8

	movdqa	XKEY10, [KEYS + 10*16]

	aesdec	XDATA0, [KEYS + 9*16]	; 9. DEC
	aesdec	XDATA1, [KEYS + 9*16]

	aesdeclast	XDATA0, XKEY10		; 10. DEC
	aesdeclast	XDATA1, XKEY10

	pxor	XDATA0, IV_TMP
	pxor	XDATA1, XSAVED0

	movdqu	[OUT + 0*OFFSET], XDATA0
	movdqu	[OUT + 1*OFFSET], XDATA1

	sub	LEN, 2*16
	jz	done
	jmp	main_loop

	align 16
initial_1:
	; load cipher text
	movdqu	XDATA0, [IN + 0*OFFSET]

	movdqa	XKEY0, [KEYS + 0*16]

	; save cipher text
	movdqa	XIV,     XDATA0

	pxor	XDATA0, XKEY0		; 0. ARK

	movdqa	XKEY2, [KEYS + 2*16]

	aesdec	XDATA0, [KEYS + 1*16]	; 1. DEC

	mov	IDX, 1*OFFSET

	aesdec	XDATA0, XKEY2		; 2. DEC

	movdqa	XKEY4, [KEYS + 4*16]

	aesdec	XDATA0, [KEYS + 3*16]	; 3. DEC

	movdqu	IV_TMP, [IV]

	aesdec	XDATA0, XKEY4		; 4. DEC

	movdqa	XKEY6, [KEYS + 6*16]

	aesdec	XDATA0, [KEYS + 5*16]	; 5. DEC

	aesdec	XDATA0, XKEY6		; 6. DEC

	movdqa	XKEY8, [KEYS + 8*16]

	aesdec	XDATA0, [KEYS + 7*16]	; 7. DEC

	aesdec	XDATA0, XKEY8		; 8. DEC

	movdqa	XKEY10, [KEYS + 10*16]

	aesdec	XDATA0, [KEYS + 9*16]	; 9. DEC

	aesdeclast	XDATA0, XKEY10		; 10. DEC

	pxor	XDATA0, IV_TMP

	movdqu	[OUT + 0*OFFSET], XDATA0

	sub	LEN, 1*16
	jz	done
	jmp	main_loop

initial_3:
	; load cipher text
	movdqu	XDATA0, [IN + 0*OFFSET]
	movdqu	XDATA1, [IN + 1*OFFSET]
	movdqu	XDATA2, [IN + 2*OFFSET]

	movdqa	XKEY0, [KEYS + 0*16]

	; save cipher text
	movdqa	XSAVED0, XDATA0
	movdqa	XSAVED1, XDATA1
	movdqa	XIV,     XDATA2

	movdqa	XKEY, [KEYS + 1*16]

	pxor	XDATA0, XKEY0		; 0. ARK
	pxor	XDATA1, XKEY0
	pxor	XDATA2, XKEY0

	movdqa	XKEY2, [KEYS + 2*16]

	aesdec	XDATA0, XKEY		; 1. DEC
	aesdec	XDATA1, XKEY
	aesdec	XDATA2, XKEY

	movdqa	XKEY, [KEYS + 3*16]

	mov	IDX, 3*OFFSET

	aesdec	XDATA0, XKEY2		; 2. DEC
	aesdec	XDATA1, XKEY2
	aesdec	XDATA2, XKEY2

	movdqa	XKEY4, [KEYS + 4*16]

	aesdec	XDATA0, XKEY		; 3. DEC
	aesdec	XDATA1, XKEY
	aesdec	XDATA2, XKEY

	movdqa	XKEY, [KEYS + 5*16]
	movdqu	IV_TMP, [IV]

	aesdec	XDATA0, XKEY4		; 4. DEC
	aesdec	XDATA1, XKEY4
	aesdec	XDATA2, XKEY4

	movdqa	XKEY6, [KEYS + 6*16]

	aesdec	XDATA0, XKEY		; 5. DEC
	aesdec	XDATA1, XKEY
	aesdec	XDATA2, XKEY

	movdqa	XKEY, [KEYS + 7*16]

	aesdec	XDATA0, XKEY6		; 6. DEC
	aesdec	XDATA1, XKEY6
	aesdec	XDATA2, XKEY6

	movdqa	XKEY8, [KEYS + 8*16]

	aesdec	XDATA0, XKEY		; 7. DEC
	aesdec	XDATA1, XKEY
	aesdec	XDATA2, XKEY

	movdqa	XKEY, [KEYS + 9*16]

	aesdec	XDATA0, XKEY8		; 8. DEC
	aesdec	XDATA1, XKEY8
	aesdec	XDATA2, XKEY8

	movdqa	XKEY10, [KEYS + 10*16]

	aesdec	XDATA0, XKEY		; 9. DEC
	aesdec	XDATA1, XKEY
	aesdec	XDATA2, XKEY

	aesdeclast	XDATA0, XKEY10	; 10. DEC
	aesdeclast	XDATA1, XKEY10
	aesdeclast	XDATA2, XKEY10

	pxor	XDATA0, IV_TMP
	pxor	XDATA1, XSAVED0
	pxor	XDATA2, XSAVED1

	movdqu	[OUT + 0*OFFSET], XDATA0
	movdqu	[OUT + 1*OFFSET], XDATA1
	movdqu	[OUT + 2*OFFSET], XDATA2

	sub	LEN, 3*16
	jz	done
	jmp	main_loop

	align 16
initial_4:
	; load cipher text
	movdqu	XDATA0, [IN + 0*OFFSET]
	movdqu	XDATA1, [IN + 1*OFFSET]
	movdqu	XDATA2, [IN + 2*OFFSET]
	movdqu	XDATA3, [IN + 3*OFFSET]

	movdqa	XKEY0, [KEYS + 0*16]

	; save cipher text
	movdqa	XSAVED0, XDATA0
	movdqa	XSAVED1, XDATA1
	movdqa	XSAVED2, XDATA2
	movdqa	XIV,     XDATA3

	movdqa	XKEY, [KEYS + 1*16]

	pxor	XDATA0, XKEY0		; 0. ARK
	pxor	XDATA1, XKEY0
	pxor	XDATA2, XKEY0
	pxor	XDATA3, XKEY0

	movdqa	XKEY2, [KEYS + 2*16]

	aesdec	XDATA0, XKEY		; 1. DEC
	aesdec	XDATA1, XKEY
	aesdec	XDATA2, XKEY
	aesdec	XDATA3, XKEY

	movdqa	XKEY, [KEYS + 3*16]

	mov	IDX, 4*OFFSET

	aesdec	XDATA0, XKEY2		; 2. DEC
	aesdec	XDATA1, XKEY2
	aesdec	XDATA2, XKEY2
	aesdec	XDATA3, XKEY2

	movdqa	XKEY4, [KEYS + 4*16]

	aesdec	XDATA0, XKEY		; 3. DEC
	aesdec	XDATA1, XKEY
	aesdec	XDATA2, XKEY
	aesdec	XDATA3, XKEY

	movdqa	XKEY, [KEYS + 5*16]

	movdqu	IV_TMP, [IV]

	aesdec	XDATA0, XKEY4		; 4. DEC
	aesdec	XDATA1, XKEY4
	aesdec	XDATA2, XKEY4
	aesdec	XDATA3, XKEY4

	movdqa	XKEY6, [KEYS + 6*16]

	aesdec	XDATA0, XKEY		; 5. DEC
	aesdec	XDATA1, XKEY
	aesdec	XDATA2, XKEY
	aesdec	XDATA3, XKEY

	movdqa	XKEY, [KEYS + 7*16]

	aesdec	XDATA0, XKEY6		; 6. DEC
	aesdec	XDATA1, XKEY6
	aesdec	XDATA2, XKEY6
	aesdec	XDATA3, XKEY6

	movdqa	XKEY8, [KEYS + 8*16]

	aesdec	XDATA0, XKEY		; 7. DEC
	aesdec	XDATA1, XKEY
	aesdec	XDATA2, XKEY
	aesdec	XDATA3, XKEY

	movdqa	XKEY, [KEYS + 9*16]

	aesdec	XDATA0, XKEY8		; 8. DEC
	aesdec	XDATA1, XKEY8
	aesdec	XDATA2, XKEY8
	aesdec	XDATA3, XKEY8

	movdqa	XKEY10, [KEYS + 10*16]

	aesdec	XDATA0, XKEY		; 9. DEC
	aesdec	XDATA1, XKEY
	aesdec	XDATA2, XKEY
	aesdec	XDATA3, XKEY

	aesdeclast	XDATA0, XKEY10	; 10. DEC
	aesdeclast	XDATA1, XKEY10
	aesdeclast	XDATA2, XKEY10
	aesdeclast	XDATA3, XKEY10

	pxor	XDATA0, IV_TMP
	pxor	XDATA1, XSAVED0
	pxor	XDATA2, XSAVED1
	pxor	XDATA3, XSAVED2

	movdqu	[OUT + 0*OFFSET], XDATA0
	movdqu	[OUT + 1*OFFSET], XDATA1
	movdqu	[OUT + 2*OFFSET], XDATA2
	movdqu	[OUT + 3*OFFSET], XDATA3

	sub	LEN, 4*16
	jz	done
	jmp	main_loop

	align 16
main_loop:
	; load cipher text
	movdqu	XDATA0, [IN + IDX + 0*OFFSET]
	movdqu	XDATA1, [IN + IDX + 1*OFFSET]
	movdqu	XDATA2, [IN + IDX + 2*OFFSET]
	movdqu	XDATA3, [IN + IDX + 3*OFFSET]

	; save cipher text
	movdqa	XSAVED0, XDATA0
	movdqa	XSAVED1, XDATA1
	movdqa	XSAVED2, XDATA2
	movdqa	XSAVED3, XDATA3

	movdqa	XKEY, [KEYS + 1*16]

	pxor	XDATA0, XKEY0		; 0. ARK
	pxor	XDATA1, XKEY0
	pxor	XDATA2, XKEY0
	pxor	XDATA3, XKEY0

	add	IDX, 4*OFFSET

	aesdec	XDATA0, XKEY		; 1. DEC
	aesdec	XDATA1, XKEY
	aesdec	XDATA2, XKEY
	aesdec	XDATA3, XKEY

	movdqa	XKEY, [KEYS + 3*16]

	aesdec	XDATA0, XKEY2		; 2. DEC
	aesdec	XDATA1, XKEY2
	aesdec	XDATA2, XKEY2
	aesdec	XDATA3, XKEY2

	aesdec	XDATA0, XKEY		; 3. DEC
	aesdec	XDATA1, XKEY
	aesdec	XDATA2, XKEY
	aesdec	XDATA3, XKEY

	movdqa	XKEY, [KEYS + 5*16]

	aesdec	XDATA0, XKEY4		; 4. DEC
	aesdec	XDATA1, XKEY4
	aesdec	XDATA2, XKEY4
	aesdec	XDATA3, XKEY4

	aesdec	XDATA0, XKEY		; 5. DEC
	aesdec	XDATA1, XKEY
	aesdec	XDATA2, XKEY
	aesdec	XDATA3, XKEY

	movdqa	XKEY, [KEYS + 7*16]

	aesdec	XDATA0, XKEY6		; 6. DEC
	aesdec	XDATA1, XKEY6
	aesdec	XDATA2, XKEY6
	aesdec	XDATA3, XKEY6

	aesdec	XDATA0, XKEY		; 7. DEC
	aesdec	XDATA1, XKEY
	aesdec	XDATA2, XKEY
	aesdec	XDATA3, XKEY

	movdqa	XKEY, [KEYS + 9*16]

	aesdec	XDATA0, XKEY8		; 8. DEC
	aesdec	XDATA1, XKEY8
	aesdec	XDATA2, XKEY8
	aesdec	XDATA3, XKEY8

	aesdec	XDATA0, XKEY		; 9. DEC
	aesdec	XDATA1, XKEY
	aesdec	XDATA2, XKEY
	aesdec	XDATA3, XKEY

	aesdeclast	XDATA0, XKEY10		; 10. DEC
	aesdeclast	XDATA1, XKEY10
	aesdeclast	XDATA2, XKEY10
	aesdeclast	XDATA3, XKEY10

	pxor	XDATA0, XIV
	pxor	XDATA1, XSAVED0
	pxor	XDATA2, XSAVED1
	pxor	XDATA3, XSAVED2

	movdqu	[OUT + IDX + 0*OFFSET - 4*OFFSET], XDATA0
	movdqu	[OUT + IDX + 1*OFFSET - 4*OFFSET], XDATA1
	movdqu	[OUT + IDX + 2*OFFSET - 4*OFFSET], XDATA2
	movdqu	[OUT + IDX + 3*OFFSET - 4*OFFSET], XDATA3

	movdqa	XIV, XSAVED3

        sub     LEN, 4*16
	jnz	main_loop

done:
%ifdef CBCS
%ifndef LINUX
        mov	NEXT_IV, [rsp + 8*6]
%endif
        ;; store last cipher block as next_iv
        movdqu  [NEXT_IV], XIV
%endif

%ifdef SAFE_DATA
	clear_all_xmms_sse_asm
%endif ;; SAFE_DATA

        ret

mksection stack-noexec
